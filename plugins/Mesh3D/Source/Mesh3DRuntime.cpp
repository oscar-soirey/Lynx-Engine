// =============================================================================
// Plugin Mesh3D : runtime module (editor AND game)
// -----------------------------------------------------------------------------
//   - MeshComponent : a 3D object on any actor. Model : an .fbx of assets/
//     (geometry + its first material), or a primitive shape (Cube, Sphere,
//     Cylinder, Cone, Plane). Color, texture (.png, .lsprite with the
//     PixelSprite plugin...), roughness / metallic, offset / rotation / scale.
//   - MeshActor : place it in the level, same settings in Details.
//   - JS : Mesh3D.set(actor, { shape: "Sphere", color: {x,y,z,w}, ... })
//
// Only the public HRL API is used (HRL_CreateMesh3D, HRL_GetVertex3DFromFBX,
// HRL_CreateMaterialFromFBX, built-in 3D shader). 1 unit = 1 voxel.
// =============================================================================

#include <plugins/LynxPlugin.h>

#include <hrl/hrl.h>
#include <json/json.hpp>

#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{
	constexpr uint32_t kInvalid = HRL_INVALID_ID;
	constexpr float kPi = 3.14159265358979f;

	// ---- Geometry -------------------------------------------------------------

	struct Geometry
	{
		std::vector<HRL_Vertex3D> vertices;
		std::vector<HRL_uint> indices;
	};

	void Tangents(HRL_Vertex3D& v)
	{
		// Any vector perpendicular to the normal (the default material has no normal map).
		const float* n = v.normal;
		float t[3] = { -n[1], n[0], 0.f };
		if (std::fabs(n[2]) > 0.9f) { t[0] = 1.f; t[1] = 0.f; t[2] = 0.f; }
		const float len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
		for (int i = 0; i < 3; ++i) v.tangent[i] = t[i] / len;
		v.bitangent[0] = n[1] * v.tangent[2] - n[2] * v.tangent[1];
		v.bitangent[1] = n[2] * v.tangent[0] - n[0] * v.tangent[2];
		v.bitangent[2] = n[0] * v.tangent[1] - n[1] * v.tangent[0];
	}

	HRL_Vertex3D Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
	{
		HRL_Vertex3D out{};
		out.position[0] = x; out.position[1] = y; out.position[2] = z;
		out.normal[0] = nx; out.normal[1] = ny; out.normal[2] = nz;
		out.uv[0] = u; out.uv[1] = v;
		Tangents(out);
		return out;
	}

	void Quad(Geometry& g, const HRL_Vertex3D& a, const HRL_Vertex3D& b, const HRL_Vertex3D& c, const HRL_Vertex3D& d)
	{
		const HRL_uint base = static_cast<HRL_uint>(g.vertices.size());
		g.vertices.insert(g.vertices.end(), { a, b, c, d });
		g.indices.insert(g.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
	}

	// Unit shapes centered on the origin (size 1).
	Geometry Cube()
	{
		Geometry g;
		const float h = 0.5f;
		// +Z, -Z, +X, -X, +Y, -Y (counter-clockwise seen from outside)
		Quad(g, Vertex(-h, -h, h, 0, 0, 1, 0, 0), Vertex(h, -h, h, 0, 0, 1, 1, 0), Vertex(h, h, h, 0, 0, 1, 1, 1), Vertex(-h, h, h, 0, 0, 1, 0, 1));
		Quad(g, Vertex(h, -h, -h, 0, 0, -1, 0, 0), Vertex(-h, -h, -h, 0, 0, -1, 1, 0), Vertex(-h, h, -h, 0, 0, -1, 1, 1), Vertex(h, h, -h, 0, 0, -1, 0, 1));
		Quad(g, Vertex(h, -h, h, 1, 0, 0, 0, 0), Vertex(h, -h, -h, 1, 0, 0, 1, 0), Vertex(h, h, -h, 1, 0, 0, 1, 1), Vertex(h, h, h, 1, 0, 0, 0, 1));
		Quad(g, Vertex(-h, -h, -h, -1, 0, 0, 0, 0), Vertex(-h, -h, h, -1, 0, 0, 1, 0), Vertex(-h, h, h, -1, 0, 0, 1, 1), Vertex(-h, h, -h, -1, 0, 0, 0, 1));
		Quad(g, Vertex(-h, h, h, 0, 1, 0, 0, 0), Vertex(h, h, h, 0, 1, 0, 1, 0), Vertex(h, h, -h, 0, 1, 0, 1, 1), Vertex(-h, h, -h, 0, 1, 0, 0, 1));
		Quad(g, Vertex(-h, -h, -h, 0, -1, 0, 0, 0), Vertex(h, -h, -h, 0, -1, 0, 1, 0), Vertex(h, -h, h, 0, -1, 0, 1, 1), Vertex(-h, -h, h, 0, -1, 0, 0, 1));
		return g;
	}

	Geometry Plane()
	{
		// Facing the 2D camera (+Z), like a sprite.
		Geometry g;
		const float h = 0.5f;
		Quad(g, Vertex(-h, -h, 0, 0, 0, 1, 0, 0), Vertex(h, -h, 0, 0, 0, 1, 1, 0), Vertex(h, h, 0, 0, 0, 1, 1, 1), Vertex(-h, h, 0, 0, 0, 1, 0, 1));
		return g;
	}

	Geometry Sphere(int segments = 32, int rings = 16)
	{
		Geometry g;
		for (int r = 0; r <= rings; ++r)
		{
			const float v = static_cast<float>(r) / rings;
			const float phi = v * kPi;
			for (int s = 0; s <= segments; ++s)
			{
				const float u = static_cast<float>(s) / segments;
				const float theta = u * 2.f * kPi;
				const float x = std::sin(phi) * std::cos(theta);
				const float y = std::cos(phi);
				const float z = std::sin(phi) * std::sin(theta);
				g.vertices.push_back(Vertex(x * 0.5f, y * 0.5f, z * 0.5f, x, y, z, u, 1.f - v));
			}
		}
		for (int r = 0; r < rings; ++r)
			for (int s = 0; s < segments; ++s)
			{
				const HRL_uint a = static_cast<HRL_uint>(r * (segments + 1) + s);
				const HRL_uint b = a + static_cast<HRL_uint>(segments + 1);
				g.indices.insert(g.indices.end(), { a, a + 1, b, b, a + 1, b + 1 });
			}
		return g;
	}

	/** Cylinder (top_radius 0.5) or cone (top_radius 0), height 1 along Y. */
	Geometry Cylinder(float top_radius, int segments = 32)
	{
		Geometry g;
		const float h = 0.5f, rb = 0.5f, rt = top_radius;
		const float slope = (rb - rt);   // normal Y component of the side (height 1)
		for (int s = 0; s <= segments; ++s)
		{
			const float u = static_cast<float>(s) / segments;
			const float t = u * 2.f * kPi;
			const float c = std::cos(t), sn = std::sin(t);
			const float len = std::sqrt(1.f + slope * slope);
			g.vertices.push_back(Vertex(c * rb, -h, sn * rb, c / len, slope / len, sn / len, u, 0.f));
			g.vertices.push_back(Vertex(c * rt, h, sn * rt, c / len, slope / len, sn / len, u, 1.f));
		}
		for (int s = 0; s < segments; ++s)
		{
			const HRL_uint a = static_cast<HRL_uint>(s * 2);
			g.indices.insert(g.indices.end(), { a, a + 1, a + 2, a + 2, a + 1, a + 3 });
		}
		// Caps
		auto cap = [&](float y, float radius, float ny)
		{
			if (radius <= 0.f)
				return;
			const HRL_uint center = static_cast<HRL_uint>(g.vertices.size());
			g.vertices.push_back(Vertex(0, y, 0, 0, ny, 0, 0.5f, 0.5f));
			for (int s = 0; s <= segments; ++s)
			{
				const float t = static_cast<float>(s) / segments * 2.f * kPi;
				g.vertices.push_back(Vertex(std::cos(t) * radius, y, std::sin(t) * radius, 0, ny, 0,
				                            0.5f + std::cos(t) * 0.5f, 0.5f + std::sin(t) * 0.5f));
			}
			for (int s = 0; s < segments; ++s)
			{
				const HRL_uint a = center + 1 + static_cast<HRL_uint>(s);
				if (ny > 0.f)
					g.indices.insert(g.indices.end(), { center, a + 1, a });
				else
					g.indices.insert(g.indices.end(), { center, a, a + 1 });
			}
		};
		cap(h, rt, 1.f);
		cap(-h, rb, -1.f);
		return g;
	}

	const Geometry* PrimitiveGeometry(const std::string& shape)
	{
		static std::unordered_map<std::string, Geometry> cache;
		if (auto it = cache.find(shape); it != cache.end())
			return &it->second;
		Geometry g;
		if (shape == "Cube") g = Cube();
		else if (shape == "Sphere") g = Sphere();
		else if (shape == "Cylinder") g = Cylinder(0.5f);
		else if (shape == "Cone") g = Cylinder(0.f);
		else if (shape == "Plane") g = Plane();
		else return nullptr;
		return &cache.emplace(shape, std::move(g)).first->second;
	}

	// ---- FBX (geometry + material, read once per file) -----------------------

	struct FbxModel
	{
		bool valid = false;
		std::vector<char> bytes;          // the file (materials made from it)
		std::vector<HRL_Vertex3D> vertices;
		HRL_id material = kInvalid;       // shared by the meshes without a texture override
	};

	FbxModel* LoadFbx(const std::string& path)
	{
		static std::unordered_map<std::string, std::unique_ptr<FbxModel>> cache;
		if (auto it = cache.find(path); it != cache.end())
			return it->second->valid ? it->second.get() : nullptr;

		auto model = std::make_unique<FbxModel>();
		const auto data = lynx::fs::ReadBinary(path);
		if (!data.empty())
		{
			model->bytes.assign(data.begin(), data.end());
			size_t count = 0;
			HRL_Vertex3D* vertices = HRL_GetVertex3DFromFBX(model->bytes.data(), model->bytes.size(), &count);
			if (vertices && count > 0)
			{
				model->vertices.assign(vertices, vertices + count);
				model->valid = true;
			}
			if (vertices)
				HRL_FreeVertex3DFromFBX(vertices);
		}
		if (!model->valid)
			std::cout << "[Mesh3D] could not load the FBX model \"" << path << "\"\n";
		FbxModel* out = model->valid ? model.get() : nullptr;
		cache.emplace(path, std::move(model));
		return out;
	}

	HRL_id WhiteTexture()
	{
		// 1 x 1 white PNG : the albedo of the plain colors.
		static const unsigned char kWhitePng[] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
			0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
			0x89, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0x0f, 0x04, 0x00,
			0x09, 0xfb, 0x03, 0xfd, 0xfb, 0x5e, 0x6b, 0x2b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
			0xae, 0x42, 0x60, 0x82
		};
		static HRL_id texture = kInvalid;
		if (texture == kInvalid || !HRL_IsValidTexture(texture))
			texture = HRL_CreateTexture(reinterpret_cast<const char*>(kWhitePng), sizeof(kWhitePng));
		return texture;
	}
}


/**
 * A 3D object attached to an actor : follows its location / rotation / scale
 * (plus offset, rotation, scale of its own).
 */
class MeshComponent : public lynx::Component
{
public:
	/** .fbx in assets/ (has priority over `shape`). */
	std::string model;
	/** Cube, Sphere, Cylinder, Cone, Plane (used when `model` is empty). */
	std::string shape = "Cube";

	lynx::vec4 color{ 1.f, 1.f, 1.f, 1.f };
	/** Texture (assets/, .png or .lsprite...) : replaces the texture of the model. */
	std::string texture;
	float roughness = 0.6f;
	float metallic = 0.f;
	bool two_sided = false;
	bool visible = true;

	lynx::vec3 offset{ 0.f, 0.f, 0.f };
	lynx::vec3 rotation{ 0.f, 0.f, 0.f };   // degrees : pitch, yaw, roll
	lynx::vec3 scale{ 1.f, 1.f, 1.f };

	~MeshComponent() override
	{
		DestroyMesh();
		if (own_material_ != kInvalid && HRL_IsValidMaterial(own_material_))
			HRL_DeleteMaterial(own_material_);
	}

	/** Applies the fields now (otherwise : next frame). */
	void Refresh()
	{
		Build();
		ApplyMaterial();
		Sync();
	}

protected:
	void OnAttach() override { Refresh(); }

	void Update(float) override
	{
		if (model != built_model_ || (model.empty() && shape != built_shape_) ||
		    (mesh_ != kInvalid && !HRL_IsValidMesh(mesh_)))
			Build();
		ApplyMaterial();
		Sync();
	}

	// After the gameplay : the final location of the frame (no lag).
	void LateUpdate(float) override { Sync(); }

private:
	void DestroyMesh()
	{
		if (mesh_ != kInvalid && HRL_IsValidMesh(mesh_))
			HRL_DeleteMesh(mesh_);
		mesh_ = kInvalid;
	}

	void Build()
	{
		DestroyMesh();
		built_model_ = model;
		built_shape_ = shape;
		applied_.reset();
		fbx_ = nullptr;

		const uint32_t scene = lynx::Engine::GetScene();
		if (!HRL_IsValidScene(scene))
			return;

		if (!model.empty())
		{
			fbx_ = LoadFbx(model);
			if (fbx_)
				mesh_ = HRL_CreateMesh3D(scene, fbx_->vertices.data(), fbx_->vertices.size(), nullptr, 0);
		}
		else if (const Geometry* g = PrimitiveGeometry(shape))
		{
			mesh_ = HRL_CreateMesh3D(scene, g->vertices.data(), g->vertices.size(), g->indices.data(), g->indices.size());
		}
		else if (!shape.empty())
			std::cout << "[Mesh3D] unknown shape \"" << shape << "\" (Cube, Sphere, Cylinder, Cone, Plane)\n";

		if (mesh_ != kInvalid)
			if (lynx::Actor* owner = GetOwner())
				HRL_SetMeshUserHandle(mesh_, owner);   // picking in the editor
	}

	struct Applied
	{
		lynx::vec4 color;
		std::string texture;
		float roughness, metallic;
		bool two_sided;
		uint32_t texture_revision;
	};

	void ApplyMaterial()
	{
		if (mesh_ == kInvalid)
			return;

		const uint32_t revision = texture.empty() ? 0u : lynx::GetTextureRevision(texture.c_str());
		const Applied now{ color, texture, roughness, metallic, two_sided, revision };
		if (applied_ && applied_->color.x == now.color.x && applied_->color.y == now.color.y &&
		    applied_->color.z == now.color.z && applied_->color.w == now.color.w && applied_->texture == now.texture &&
		    applied_->roughness == now.roughness && applied_->metallic == now.metallic &&
		    applied_->two_sided == now.two_sided && applied_->texture_revision == now.texture_revision)
			return;
		applied_ = now;

		// FBX model, no override : its own material (shared).
		const bool plain_fbx = fbx_ && texture.empty() && color.x == 1.f && color.y == 1.f && color.z == 1.f &&
		                       color.w == 1.f;
		if (plain_fbx)
		{
			if (fbx_->material == kInvalid)
				fbx_->material = HRL_CreateMaterialFromFBX(fbx_->bytes.data(), fbx_->bytes.size());
			if (fbx_->material != kInvalid)
			{
				HRL_SetMeshMaterial(mesh_, fbx_->material);
				return;
			}
		}

		// A material of its own : color, texture, roughness...
		if (own_material_ == kInvalid || !HRL_IsValidMaterial(own_material_))
			own_material_ = HRL_CreateMaterial(HRL_MESH_3D_SHADER);
		if (own_material_ == kInvalid)
			return;

		HRL_id albedo = kInvalid;
		if (!texture.empty())
		{
			albedo = lynx::RessourceTex(texture.c_str());
			if (albedo == kInvalid)
				std::cout << "[Mesh3D] texture not found : " << texture << "\n";
		}
		if (albedo == kInvalid)
			albedo = WhiteTexture();

		HRL_MaterialSetTexture(own_material_, HRL_T_ALBEDO, albedo);
		HRL_MaterialSetVec3(own_material_, "TintColor", color.x, color.y, color.z);
		HRL_MaterialSetFloat(own_material_, "BaseColorAlpha", color.w);
		HRL_MaterialSetFloat(own_material_, "OpacityValue", color.w);
		HRL_MaterialSetInt(own_material_, "OpacityUseValue", 1);
		HRL_MaterialSetFloat(own_material_, "RoughnessValue", roughness);
		HRL_MaterialSetInt(own_material_, "RoughnessUseValue", 1);
		HRL_MaterialSetFloat(own_material_, "MetallicValue", metallic);
		HRL_MaterialSetInt(own_material_, "MetallicUseValue", 1);
		HRL_MaterialSetFloat(own_material_, "SpecularValue", 0.5f);
		HRL_MaterialSetInt(own_material_, "SpecularUseValue", 1);
		HRL_MaterialSetBool(own_material_, HRL_MATERIAL_PARAM_TWO_SIDED, two_sided ? 1 : 0);
		HRL_SetMeshMaterial(mesh_, own_material_);
	}

	void Sync()
	{
		lynx::Actor* owner = GetOwner();
		if (mesh_ == kInvalid || !owner || !HRL_IsValidMesh(mesh_))
			return;
		const lynx::transform& t = owner->transform;
		HRL_SetMeshLocation(mesh_, t.location.x + offset.x * t.scale.x, t.location.y + offset.y * t.scale.y,
		                    t.location.z + offset.z * t.scale.z);
		HRL_SetMeshRotation(mesh_, t.rotation.x + rotation.x, t.rotation.y + rotation.y, t.rotation.z + rotation.z);
		// No visibility in the mesh API : a hidden mesh has a null scale.
		const float k = visible ? 1.f : 0.f;
		HRL_SetMeshScale(mesh_, t.scale.x * scale.x * k, t.scale.y * scale.y * k, t.scale.z * scale.z * k);
	}

	uint32_t mesh_ = kInvalid;
	uint32_t own_material_ = kInvalid;
	FbxModel* fbx_ = nullptr;
	std::string built_model_;
	std::string built_shape_ = "\x01";
	std::optional<Applied> applied_;
};


/** A 3D object placed in the level (MeshComponent with its settings in Details). */
class MeshActor : public lynx::Actor
{
public:
	/** .fbx in assets/ (priority over shape). */
	std::string model;
	/** Cube, Sphere, Cylinder, Cone, Plane. */
	std::string shape = "Cube";
	lynx::vec4 color{ 1.f, 1.f, 1.f, 1.f };
	/** .png / .lsprite in assets/ (optional). */
	std::string texture;
	float roughness = 0.6f;
	float metallic = 0.f;
	bool two_sided = false;
	bool visible = true;

	MeshActor()
	{
		HPROPERTY(model, lynx::Exposed);
		HPROPERTY(shape, lynx::Exposed);
		HPROPERTY(color, lynx::Exposed);
		HPROPERTY(texture, lynx::Exposed);
		HPROPERTY(roughness, lynx::Exposed);
		HPROPERTY(metallic, lynx::Exposed);
		HPROPERTY(two_sided, lynx::Exposed);
		HPROPERTY(visible, lynx::Exposed);
	}

	void Init() override
	{
		Actor::Init();
		mesh_ = &AddComponent<MeshComponent>();
		Update(0.0);
	}

	void Update(double dt) override
	{
		Actor::Update(dt);
		if (!mesh_)
			return;
		mesh_->model = model;
		mesh_->shape = shape;
		mesh_->color = color;
		mesh_->texture = texture;
		mesh_->roughness = roughness;
		mesh_->metallic = metallic;
		mesh_->two_sided = two_sided;
		mesh_->visible = visible;
	}

	MeshComponent* GetMesh() const { return mesh_; }

private:
	MeshComponent* mesh_ = nullptr;
};


LYNX_PLUGIN(Mesh3D)

LYNX_LINK_MODULE(
	LYNX_MODULE_REGISTER(MeshActor);
)

LYNX_PLUGIN_STARTUP()
{
	using lynx::InterfaceArg;
	using lynx::InterfaceArgs;

	// Mesh3D._set(actor, json) : adds / changes the MeshComponent of an actor.
	lynx::RegisterScriptFunction("Mesh3D", "_set", [](const InterfaceArgs& a) -> InterfaceArg
	{
		lynx::Actor* actor = lynx::InterfaceArgActor(a, 0);
		if (!actor || a.size() < 2)
			return false;
		const auto* text = std::get_if<std::string>(&a[1]);
		const nlohmann::json j = text ? nlohmann::json::parse(*text, nullptr, false) : nlohmann::json();
		if (!j.is_object())
			return false;

		MeshComponent* mesh = actor->GetComponent<MeshComponent>();
		if (!mesh)
			mesh = &actor->AddComponent<MeshComponent>();

		auto vec = [&](const char* key, float* out, int n)
		{
			const auto it = j.find(key);
			if (it == j.end() || !it->is_object())
				return;
			const char* names[] = { "x", "y", "z", "w" };
			for (int i = 0; i < n; ++i)
				if (it->contains(names[i]) && (*it)[names[i]].is_number())
					out[i] = (*it)[names[i]].get<float>();
		};
		if (j.contains("model") && j["model"].is_string()) mesh->model = j["model"].get<std::string>();
		if (j.contains("shape") && j["shape"].is_string()) mesh->shape = j["shape"].get<std::string>();
		if (j.contains("texture") && j["texture"].is_string()) mesh->texture = j["texture"].get<std::string>();
		if (j.contains("roughness") && j["roughness"].is_number()) mesh->roughness = j["roughness"].get<float>();
		if (j.contains("metallic") && j["metallic"].is_number()) mesh->metallic = j["metallic"].get<float>();
		if (j.contains("twoSided") && j["twoSided"].is_boolean()) mesh->two_sided = j["twoSided"].get<bool>();
		if (j.contains("visible") && j["visible"].is_boolean()) mesh->visible = j["visible"].get<bool>();
		vec("color", &mesh->color.x, 4);
		vec("offset", &mesh->offset.x, 3);
		vec("rotation", &mesh->rotation.x, 3);
		vec("scale", &mesh->scale.x, 3);
		mesh->Refresh();
		return true;
	});
	lynx::RegisterScriptFunction("Mesh3D", "remove", [](const InterfaceArgs& a) -> InterfaceArg
	{
		lynx::Actor* actor = lynx::InterfaceArgActor(a, 0);
		if (!actor || !actor->GetComponent<MeshComponent>())
			return false;
		actor->RemoveComponent<MeshComponent>();
		return true;
	});
	lynx::RegisterScriptPrelude("<Mesh3D plugin>",
		"Mesh3D.set = function (actor, options) { return Mesh3D._set(actor, JSON.stringify(options || {})); };\n");
}
