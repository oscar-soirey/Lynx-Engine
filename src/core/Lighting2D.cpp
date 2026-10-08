#include "Lighting2D.h"

#include "CameraState.h"
#include "Engine.h"
#include "Filesystem.h"
#include "Voxels.h"
#include "../gameplay/Light2DComponent.h"
#include "../gameplay/PlayerController.h"

#include <hrl/hrl.h>
#include <json/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace lynx::lighting2d
{
	namespace
	{
		using PT = ParamType;

		const std::vector<ParamInfo> kParams = {
			{ "enabled", "Enabled", "Lighting", PT::Toggle, {0.f}, 0.f, 1.f,
			  "2D lighting of the project (off : the 2D lights are not drawn).", false },
			{ "ambientColor", "Ambient color", "Lighting", PT::Color, {1.f, 1.f, 1.f}, 0.f, 1.f,
			  "Color of the scene outside the 2D lights. White : the normal lighting is kept as it is.", false },
			{ "ambientIntensity", "Ambient intensity", "Lighting", PT::Float, {1.f}, 0.f, 2.f,
			  "1 (default) : the normal lighting (HRL lights, emissive voxels) is kept, the 2D lights add to it. "
			  "Lower : darker outside the 2D lights (0.1-0.3 : night).", false },
			{ "intensity", "Lights intensity", "Lighting", PT::Float, {1.f}, 0.f, 4.f,
			  "Multiplies every 2D light.", false },

			{ "shadows", "Shadows", "Shadows", PT::Toggle, {1.f}, 0.f, 1.f,
			  "Voxels cast shadows (per light : Cast shadows).", false },
			{ "shadowQuality", "Quality (steps)", "Shadows", PT::Float, {32.f}, 8.f, 96.f,
			  "Samples per shadow ray : more = exact thin walls, slower.", false },
			{ "edgeDepth", "Lit edge depth", "Shadows", PT::Float, {2.f}, 0.f, 8.f,
			  "Voxels lit inside the walls that face a light (glowing edges of the terrain).", false },
			{ "liquidOpacity", "Liquid opacity", "Shadows", PT::Float, {0.35f}, 0.f, 1.f,
			  "How much water / lava voxels block the light (per voxel).", false },
			{ "gasOpacity", "Gas opacity", "Shadows", PT::Float, {0.1f}, 0.f, 1.f,
			  "How much smoke / steam voxels block the light.", false },
			{ "decorOpacity", "Decor opacity", "Shadows", PT::Float, {0.f}, 0.f, 1.f,
			  "Voxels without collision (background) : 0 = light goes through.", false },

			{ "pixelSnap", "Pixel-art light", "Style", PT::Toggle, {0.f}, 0.f, 1.f,
			  "Light computed per voxel : square light, matches the pixel art.", false },
			{ "bands", "Banding", "Style", PT::Float, {0.f}, 0.f, 16.f,
			  "0 : smooth ; 3-8 : light in steps (cel / retro look).", false },
			{ "keepBright", "Keep emissive bright", "Style", PT::Toggle, {1.f}, 0.f, 1.f,
			  "Emissive (HDR) pixels stay bright in the dark.", false },
		};

		struct Value { float v[3]; };

		std::unordered_map<std::string, Value>& Values()
		{
			static std::unordered_map<std::string, Value> values = []
			{
				std::unordered_map<std::string, Value> out;
				for (const ParamInfo& p : kParams)
					out[p.name] = { { p.defaults[0], p.defaults[1], p.defaults[2] } };
				return out;
			}();
			return values;
		}

		int Components(const ParamInfo& p)
		{
			return p.type == ParamType::Color ? 3 : 1;
		}

		float Get1(const char* name)
		{
			return Values()[name].v[0];
		}

		// ---------------------------------------------------------------------
		// Shader (HRL post process : vertex "apos" -> uv, scene in uScene)
		// ---------------------------------------------------------------------

		std::string FragmentSource()
		{
			std::string light_uniforms;
			std::string arrays[4];
			const char* suffix[4] = { "A", "B", "C", "D" };
			for (int i = 0; i < kMaxLights; ++i)
				for (int k = 0; k < 4; ++k)
				{
					const std::string name = "uL" + std::to_string(i) + suffix[k];
					light_uniforms += "uniform vec4 " + name + ";\n";
					arrays[k] += (i ? ", " : "") + name;
				}

			// Inserted in the post process shader (PostProcess.cpp) : uv, uScene and
			// uScreenSize come from it ; ApplyLighting2D is called on the scene color.
			std::string s = R"(
// ---- 2D lighting (core/Lighting2D.cpp) ----
uniform sampler2D uOcc;

uniform int uEnabled = 0;
uniform int uValid = 0;
uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamFwd;
uniform float uTanHalfFov = 0.2;
uniform vec2 uVoxelOrigin = vec2(0.0);
uniform float uVoxelScale = 1.0;
uniform vec4 uOccRect = vec4(0.0, 0.0, 1.0, 1.0);   // origin (voxels), size (voxels)

uniform vec3 uAmbient = vec3(1.0);
uniform float uIntensity = 1.0;
uniform int uShadows = 1;
uniform float uShadowSteps = 32.0;
uniform float uEdgeDepth = 2.0;
uniform float uBands = 0.0;
uniform int uPixelSnap = 0;
uniform int uKeepBright = 1;
uniform int uLightCount = 0;
)";
			s += light_uniforms;
			s += R"(
// Opacity of the voxels at a voxel position (0 outside the copied area).
float Occ(vec2 v)
{
    vec2 t = (v - uOccRect.xy) / uOccRect.zw;
    if (t.x < 0.0 || t.y < 0.0 || t.x > 1.0 || t.y > 1.0)
        return 0.0;
    return texture(uOcc, t).r;
}

// Light that reaches p from l (1 : nothing in between). Beer-Lambert through
// the voxels : a solid voxel lets ~2% through, a liquid more.
float Ray(vec2 p, vec2 target)
{
    vec2 d = target - p;
    float len = length(d);
    if (len < 1.0)
        return 1.0;
    vec2 dir = d / len;
    // From 0.8 voxel (the lit face of a wall is not in its own shadow) - or
    // uEdgeDepth inside a wall : its edge facing the light glows - to 0.5
    // voxel before the light.
    float t0 = Occ(p) > 0.5 ? max(0.8, uEdgeDepth) : 0.8;
    if (t0 >= len - 0.5)
        return 1.0;
    float t1 = max(t0, len - 0.5);
    int steps = int(clamp((t1 - t0) * 1.5, 2.0, uShadowSteps));
    float stepLen = (t1 - t0) / float(steps);
    float density = 0.0;
    for (int s = 0; s < 96; ++s)
    {
        if (s >= steps)
            break;
        float t = t0 + (float(s) + 0.5) * stepLen;
        density += Occ(p + dir * t);
        if (density * stepLen > 2.0)
            return 0.0;
    }
    return exp(-density * stepLen * 4.0);
}

float Shadow(vec2 p, vec2 l, float sourceRadius)
{
    if (sourceRadius < 0.01)
        return Ray(p, l);
    // Soft edges : three rays to the sides of the source (penumbra).
    vec2 d = normalize(l - p + vec2(1e-5));
    vec2 side = vec2(-d.y, d.x) * sourceRadius;
    return (Ray(p, l - side) + Ray(p, l) + Ray(p, l + side)) / 3.0;
}

vec3 ApplyLighting2D(vec3 scene)
{
    if (uEnabled == 0 || uValid == 0)
        return scene;

    // World position of the pixel on the voxel plane (Z = 0).
    vec2 ndc = uv * 2.0 - 1.0;
    float aspect = uScreenSize.x / max(uScreenSize.y, 1.0);
    vec3 ray = normalize(uCamFwd + ndc.x * aspect * uTanHalfFov * uCamRight + ndc.y * uTanHalfFov * uCamUp);
    if (abs(ray.z) < 1e-6)
        return scene;
    float hit = -uCamPos.z / ray.z;
    if (hit < 0.0)
        return scene;
    vec2 world = uCamPos.xy + ray.xy * hit;
    vec2 vox = (world - uVoxelOrigin) / uVoxelScale;
    if (uPixelSnap != 0)
        vox = floor(vox) + 0.5;

)";
			s += "    vec4 LA[" + std::to_string(kMaxLights) + "] = vec4[](" + arrays[0] + ");\n";
			s += "    vec4 LB[" + std::to_string(kMaxLights) + "] = vec4[](" + arrays[1] + ");\n";
			s += "    vec4 LC[" + std::to_string(kMaxLights) + "] = vec4[](" + arrays[2] + ");\n";
			s += "    vec4 LD[" + std::to_string(kMaxLights) + "] = vec4[](" + arrays[3] + ");\n";
			s += R"(
    vec3 lights = vec3(0.0);
    for (int i = 0; i < )" + std::to_string(kMaxLights) + R"(; ++i)
    {
        if (i >= uLightCount)
            break;
        vec4 A = LA[i];   // position (voxels), radius, falloff
        vec4 B = LB[i];   // color * intensity, shadow strength (< 0 : no shadow)
        vec4 C = LC[i];   // cone direction, cos outer, cos inner (cos outer < -1.5 : no cone)
        vec4 D = LD[i];   // source radius

        vec2 d = vox - A.xy;
        float dist = length(d);
        if (dist >= A.z)
            continue;

        float att = pow(clamp(1.0 - dist / A.z, 0.0, 1.0), A.w);
        if (C.z > -1.5 && dist > 0.001)
            att *= smoothstep(C.z, C.w, dot(d / dist, C.xy));
        if (att <= 0.001)
            continue;

        if (uShadows != 0 && B.w > 0.0)
            att *= mix(1.0, Shadow(vox, A.xy, D.x), B.w);

        lights += B.rgb * att;
    }

    lights *= uIntensity;
    if (uBands > 0.5)
    {
        // Steps of brightness, the hue is kept (cel / retro look).
        float lum = max(lights.r, max(lights.g, lights.b));
        if (lum > 1e-4)
            lights *= (floor(lum * uBands + 0.5) / uBands) / lum;
    }
    vec3 light = uAmbient + lights;

    vec3 color = scene * light;
    if (uKeepBright != 0)
        color += max(scene - vec3(1.0), vec3(0.0));   // emissive (HDR) pixels keep their glow
    return color;
}
// ---- end of the 2D lighting ----
)";
			return s;
		}

		// ---------------------------------------------------------------------
		// Per viewport
		// ---------------------------------------------------------------------

		struct View
		{
			uint32_t viewport = HRL_INVALID_ID;
			uint32_t material = HRL_INVALID_ID;
			uint32_t texture = HRL_INVALID_ID;
			int rect[4] = { 0, 0, 0, 0 };   // occlusion area : x, y, w, h (voxels)
			int step = 1;                   // voxels per texel
			std::vector<uint8_t> occ;       // last uploaded opacity
			int frames_since_upload = 1000;
			int last_light_count = 0;
		};

		bool g_installed = false;
		bool g_dirty = false;
		std::vector<View> g_views;
		int g_drawn = 0;
		float g_time = 0.f;
		std::string g_error;
		uint8_t g_opacity[256] = {};
		bool g_opacity_valid = false;

		constexpr float kDegToRad = 3.14159265358979f / 180.f;

		void ComputeOpacityTable()
		{
			const uint32_t solid = voxels::GetSolidMask();
			const float liquid = std::clamp(Get1("liquidOpacity"), 0.f, 1.f);
			const float gas = std::clamp(Get1("gasOpacity"), 0.f, 1.f);
			const float decor = std::clamp(Get1("decorOpacity"), 0.f, 1.f);

			g_opacity[0] = 0;
			for (int t = 1; t < 256; ++t)
			{
				const voxels::VoxelType* type = voxels::GetType(static_cast<uint8_t>(t));
				float o = 1.f;
				if (type)
				{
					if (type->physics.behavior == voxels::VoxelBehavior::Liquid)
						o = liquid;
					else if (type->physics.behavior == voxels::VoxelBehavior::Gas)
						o = gas;
					else if ((type->flags & solid) == 0)
						o = decor;
				}
				g_opacity[t] = static_cast<uint8_t>(std::lround(o * 255.f));
			}
			g_opacity_valid = true;
		}

		// Uncompressed 8 bit grey TGA, bottom-left origin (HRL / stb_image read it ;
		// HRL flips on upload : the first row is the bottom one, v = 0).
		std::vector<char> MakeTga(const std::vector<uint8_t>& pixels, int w, int h)
		{
			std::vector<char> out(18 + pixels.size());
			out[2] = 3;   // grey, uncompressed
			out[12] = static_cast<char>(w & 0xFF);
			out[13] = static_cast<char>((w >> 8) & 0xFF);
			out[14] = static_cast<char>(h & 0xFF);
			out[15] = static_cast<char>((h >> 8) & 0xFF);
			out[16] = 8;   // bits per pixel
			out[17] = 0;   // bottom-left origin
			std::copy(pixels.begin(), pixels.end(), out.begin() + 18);
			return out;
		}

		void ApplySettings(View& v)
		{
			if (v.material == HRL_INVALID_ID)
				return;
			const auto& values = Values();
			const float* amb = values.at("ambientColor").v;
			const float ai = Get1("ambientIntensity");
			HRL_MaterialSetInt(v.material, "uEnabled", Get1("enabled") != 0.f ? 1 : 0);
			HRL_MaterialSetVec3(v.material, "uAmbient", amb[0] * ai, amb[1] * ai, amb[2] * ai);
			HRL_MaterialSetFloat(v.material, "uIntensity", Get1("intensity"));
			HRL_MaterialSetInt(v.material, "uShadows", Get1("shadows") != 0.f ? 1 : 0);
			HRL_MaterialSetFloat(v.material, "uShadowSteps", std::clamp(Get1("shadowQuality"), 2.f, 96.f));
			HRL_MaterialSetFloat(v.material, "uEdgeDepth", std::max(0.f, Get1("edgeDepth")));
			HRL_MaterialSetFloat(v.material, "uBands", std::max(0.f, Get1("bands")));
			HRL_MaterialSetInt(v.material, "uPixelSnap", Get1("pixelSnap") != 0.f ? 1 : 0);
			HRL_MaterialSetInt(v.material, "uKeepBright", Get1("keepBright") != 0.f ? 1 : 0);
		}

		void ApplyAll()
		{
			for (View& v : g_views)
				ApplySettings(v);
			g_opacity_valid = false;   // opacities may have changed
		}

		// The 2D lighting is computed in the post process shader (one pass : the
		// post process reads the scene once, lighting then bloom / tone mapping).
		void AttachTo(PlayerController* player)
		{
			if (!player)
				return;
			const uint32_t viewport = player->GetViewportBackend();
			const uint32_t material = postprocess::GetMaterialFor(viewport);
			if (viewport == HRL_INVALID_ID || material == HRL_INVALID_ID)
				return;
			for (View& v : g_views)
				if (v.viewport == viewport)
				{
					if (v.material != material)
					{
						v.material = material;   // recreated : settings and texture again
						v.rect[2] = v.rect[3] = 0;
						ApplySettings(v);
					}
					return;
				}

			View view;
			view.viewport = viewport;
			view.material = material;
			ApplySettings(view);
			g_views.push_back(std::move(view));
		}

		struct Basis
		{
			vec3 fwd, right, up;
		};

		Basis CameraBasis(const vec3& rot)
		{
			// Same convention as LightComponent::GetDirection / the camera :
			// yaw 0 = +X, yaw -90 = -Z, pitch up = +Y ; roll around the forward axis.
			const float pitch = rot.x * kDegToRad;
			const float yaw = rot.y * kDegToRad;
			const float roll = rot.z * kDegToRad;
			Basis b;
			b.fwd = vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
			const vec3 world_up(0.f, 1.f, 0.f);
			auto cross = [](const vec3& a, const vec3& c) {
				return vec3(a.y * c.z - a.z * c.y, a.z * c.x - a.x * c.z, a.x * c.y - a.y * c.x);
			};
			auto normalize = [](vec3 v) {
				const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
				return l > 1e-6f ? vec3(v.x / l, v.y / l, v.z / l) : vec3(1.f, 0.f, 0.f);
			};
			b.right = normalize(cross(b.fwd, world_up));
			b.up = normalize(cross(b.right, b.fwd));
			if (roll != 0.f)
			{
				const float c = std::cos(roll), s = std::sin(roll);
				const vec3 r = b.right, u = b.up;
				b.right = vec3(r.x * c + u.x * s, r.y * c + u.y * s, r.z * c + u.z * s);
				b.up = vec3(u.x * c - r.x * s, u.y * c - r.y * s, u.z * c - r.z * s);
			}
			return b;
		}

		struct Candidate
		{
			const Light2DComponent* light;
			vec2 pos;        // voxels
			float radius;    // voxels
			float score;
		};

		// Rebuilds the occlusion texture of the view when the area moved or the voxels changed.
		void UpdateOcclusion(View& v, uint32_t scene, int x0, int y0, int x1, int y1)
		{
			constexpr int kMaxTexels = 320;
			const int w_vox = std::max(1, x1 - x0);
			const int h_vox = std::max(1, y1 - y0);
			const int step = std::max(1, (std::max(w_vox, h_vox) + kMaxTexels - 1) / kMaxTexels);
			// Area snapped to 16 * step voxels : the texture moves less often.
			const int snap = 16 * step;
			auto floor_to = [](int a, int m) { return (a >= 0 ? a / m : -((-a + m - 1) / m)) * m; };
			const int rx = floor_to(x0, snap);
			const int ry = floor_to(y0, snap);
			const int rw = floor_to(x1 - rx + snap - 1, snap);
			const int rh = floor_to(y1 - ry + snap - 1, snap);
			const int tw = std::max(1, rw / step);
			const int th = std::max(1, rh / step);

			const bool moved = rx != v.rect[0] || ry != v.rect[1] || rw != v.rect[2] || rh != v.rect[3] || step != v.step;
			// The voxels change (destruction, falling sand) : read again every few frames.
			if (!moved && v.frames_since_upload < 3 && g_opacity_valid)
			{
				++v.frames_since_upload;
				return;
			}
			if (!g_opacity_valid)
				ComputeOpacityTable();

			std::vector<uint8_t> occ(static_cast<size_t>(tw) * th, 0);
			for (int ty = 0; ty < th; ++ty)
				for (int tx = 0; tx < tw; ++tx)
				{
					uint8_t best = 0;
					// one texel = step x step voxels : the most opaque one
					for (int sy = 0; sy < step && best < 255; ++sy)
						for (int sx = 0; sx < step && best < 255; ++sx)
						{
							const uint32_t type = HRL_GetVoxelType(scene, rx + tx * step + sx, ry + ty * step + sy);
							best = std::max(best, g_opacity[type & 0xFF]);
						}
					occ[static_cast<size_t>(ty) * tw + tx] = best;
				}

			v.frames_since_upload = 0;
			if (!moved && occ == v.occ && v.texture != HRL_INVALID_ID)
				return;

			const std::vector<char> tga = MakeTga(occ, tw, th);
			if (v.texture == HRL_INVALID_ID || !HRL_IsValidTexture(v.texture))
				v.texture = HRL_CreateTexture(tga.data(), tga.size());
			else
				HRL_ReloadTexture(v.texture, tga.data(), tga.size());
			if (v.texture == HRL_INVALID_ID)
				return;
			// Linear : smooth shadow edges between voxels.
			HRL_SetTextureMinFilter(v.texture, HRL_FILTER_LINEAR);
			HRL_SetTextureMagFilter(v.texture, HRL_FILTER_LINEAR);
			HRL_MaterialSetTexture(v.material, "uOcc", v.texture);
			HRL_MaterialSetVec4(v.material, "uOccRect", static_cast<float>(rx), static_cast<float>(ry),
			                    static_cast<float>(tw * step), static_cast<float>(th * step));

			v.rect[0] = rx; v.rect[1] = ry; v.rect[2] = rw; v.rect[3] = rh;
			v.step = step;
			v.occ = std::move(occ);
		}

		void UpdateView(View& v, PlayerController* player, uint32_t scene)
		{
			if (v.material == HRL_INVALID_ID)
				return;

			uint32_t camera = camera_state::GetViewportCamera(v.viewport);
			if (camera == HRL_INVALID_ID && player)
				camera = player->GetViewCamera();
			camera_state::State cam;
			const bool valid = camera != HRL_INVALID_ID && camera_state::Get(camera, cam) && cam.has_location &&
			                   std::fabs(cam.location.z) > 1e-4f;
			HRL_MaterialSetInt(v.material, "uValid", valid ? 1 : 0);
			if (!valid)
				return;

			const Basis b = CameraBasis(cam.rotation);
			const float tan_half = std::tan(std::clamp(cam.fov, 1.f, 170.f) * 0.5f * kDegToRad);
			HRL_MaterialSetVec3(v.material, "uCamPos", cam.location.x, cam.location.y, cam.location.z);
			HRL_MaterialSetVec3(v.material, "uCamFwd", b.fwd.x, b.fwd.y, b.fwd.z);
			HRL_MaterialSetVec3(v.material, "uCamRight", b.right.x, b.right.y, b.right.z);
			HRL_MaterialSetVec3(v.material, "uCamUp", b.up.x, b.up.y, b.up.z);
			HRL_MaterialSetFloat(v.material, "uTanHalfFov", tan_half);

			// World <-> voxels
			float ox = 0.f, oy = 0.f, sx = 1.f, sy = 0.f;
			HRL_VoxelToWorldCoordinates(scene, 0.f, 0.f, &ox, &oy);
			HRL_VoxelToWorldCoordinates(scene, 1.f, 0.f, &sx, &sy);
			const float scale = std::max(1e-5f, sx - ox);
			HRL_MaterialSetVec2(v.material, "uVoxelOrigin", ox, oy);
			HRL_MaterialSetFloat(v.material, "uVoxelScale", scale);

			// Area seen (generous aspect : the window size is not known here).
			constexpr float kAspect = 2.4f;
			float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
			bool any = false;
			for (int cy = -1; cy <= 1; cy += 2)
				for (int cx = -1; cx <= 1; cx += 2)
				{
					const vec3 d = vec3(b.fwd.x + cx * kAspect * tan_half * b.right.x + cy * tan_half * b.up.x,
					                    b.fwd.y + cx * kAspect * tan_half * b.right.y + cy * tan_half * b.up.y,
					                    b.fwd.z + cx * kAspect * tan_half * b.right.z + cy * tan_half * b.up.z);
					if (std::fabs(d.z) < 1e-6f)
						continue;
					const float t = -cam.location.z / d.z;
					if (t < 0.f)
						continue;
					const float wx = (cam.location.x + d.x * t - ox) / scale;
					const float wy = (cam.location.y + d.y * t - oy) / scale;
					min_x = std::min(min_x, wx); max_x = std::max(max_x, wx);
					min_y = std::min(min_y, wy); max_y = std::max(max_y, wy);
					any = true;
				}
			if (!any)
			{
				HRL_MaterialSetInt(v.material, "uValid", 0);
				return;
			}
			// Tilted cameras can see very far : keep a sane area around the center.
			const float cx = (min_x + max_x) * 0.5f, cy = (min_y + max_y) * 0.5f;
			const float half_w = std::min((max_x - min_x) * 0.5f, 600.f);
			const float half_h = std::min((max_y - min_y) * 0.5f, 600.f);
			min_x = cx - half_w; max_x = cx + half_w;
			min_y = cy - half_h; max_y = cy + half_h;

			// --- Lights : the ones that touch the view, best first ---------------
			std::vector<Candidate> candidates;
			float max_radius = 0.f;
			for (const Light2DComponent* l : Light2DComponent::GetAll())
			{
				if (!l->enabled || !l->has_cache || l->intensity <= 0.f || l->radius <= 0.f)
					continue;
				const float lx = (l->cached_location.x - ox) / scale;
				const float ly = (l->cached_location.y - oy) / scale;
				const float r = l->radius;
				if (lx + r < min_x || lx - r > max_x || ly + r < min_y || ly - r > max_y)
					continue;
				const float dx = lx - cx, dy = ly - cy;
				const float dist = std::sqrt(dx * dx + dy * dy);
				const float lum = l->intensity * std::max({ l->color.x, l->color.y, l->color.z, 0.01f });
				candidates.push_back({ l, vec2(lx, ly), r, lum * r / (1.f + dist) });
			}
			std::sort(candidates.begin(), candidates.end(),
			          [](const Candidate& a, const Candidate& c) { return a.score > c.score; });
			if (static_cast<int>(candidates.size()) > kMaxLights)
				candidates.resize(kMaxLights);
			for (const Candidate& c : candidates)
				if (c.light->cast_shadows)
					max_radius = std::max(max_radius, c.radius);

			const int count = static_cast<int>(candidates.size());
			for (int i = 0; i < count; ++i)
			{
				const Light2DComponent* l = candidates[i].light;
				const std::string n = "uL" + std::to_string(i);

				float flicker = 1.f;
				if (l->flicker > 0.f)
				{
					// two sines + a per-light phase : looks like a flame
					const float phase = static_cast<float>(reinterpret_cast<uintptr_t>(l) % 997) * 0.37f;
					const float f = 0.6f * std::sin(g_time * 13.f + phase) + 0.4f * std::sin(g_time * 31.7f + phase * 2.1f);
					flicker = std::max(0.f, 1.f + l->flicker * f * 0.5f);
				}
				const float k = l->intensity * flicker;

				HRL_MaterialSetVec4(v.material, (n + "A").c_str(), candidates[i].pos.x, candidates[i].pos.y,
				                    candidates[i].radius, std::clamp(l->falloff, 0.1f, 8.f));
				HRL_MaterialSetVec4(v.material, (n + "B").c_str(), l->color.x * k, l->color.y * k, l->color.z * k,
				                    l->cast_shadows ? std::clamp(l->shadow_strength, 0.f, 1.f) : -1.f);

				if (l->cone_angle < 359.f)
				{
					const float a = l->cached_direction * kDegToRad;
					const float half = std::clamp(l->cone_angle, 1.f, 359.f) * 0.5f * kDegToRad;
					const float soft = std::clamp(l->cone_softness, 0.f, 1.f);
					const float outer = std::cos(half);
					const float inner = std::cos(half * (1.f - soft * 0.9f));
					HRL_MaterialSetVec4(v.material, (n + "C").c_str(), std::cos(a), std::sin(a), outer,
					                    std::max(inner, outer + 1e-3f));
				}
				else
					HRL_MaterialSetVec4(v.material, (n + "C").c_str(), 1.f, 0.f, -2.f, -1.f);

				HRL_MaterialSetVec4(v.material, (n + "D").c_str(), std::max(0.f, l->source_radius), 0.f, 0.f, 0.f);
			}
			HRL_MaterialSetInt(v.material, "uLightCount", count);
			v.last_light_count = count;
			g_drawn += count;

			// --- Occlusion around the view (+ the reach of the shadow casting lights) ---
			if (Get1("shadows") != 0.f && max_radius > 0.f)
			{
				const float m = std::min(max_radius, 128.f) + 2.f;
				UpdateOcclusion(v, scene,
				                static_cast<int>(std::floor(min_x - m)), static_cast<int>(std::floor(min_y - m)),
				                static_cast<int>(std::ceil(max_x + m)), static_cast<int>(std::ceil(max_y + m)));
			}
		}
	}

	// =========================================================================

	std::string ShaderCode()
	{
		return FragmentSource();
	}

	const std::vector<ParamInfo>& GetParams()
	{
		return kParams;
	}

	const ParamInfo* FindParam(const std::string& name)
	{
		for (const ParamInfo& p : kParams)
			if (name == p.name)
				return &p;
		return nullptr;
	}

	void Install()
	{
		if (g_installed)
			return;
		Engine* engine = Engine::Get();
		if (!engine)
			return;
		g_installed = true;

		if (postprocess::HasLighting2D())
			std::cout << "[LIGHTING 2D] ready (in the post process shader)\n";
		else
		{
			g_error = "The post process shader with the 2D lighting could not be compiled (see the HRL errors "
			          "in the output) : the default post process is used, without 2D lighting.";
			std::cout << "[LIGHTING 2D] " << g_error << "\n";
		}

		for (PlayerController* player : engine->GetPlayers())
			AttachTo(player);

		Load();
	}

	void Shutdown()
	{
		g_views.clear();
		g_installed = false;
		g_opacity_valid = false;
	}

	void Update(float dt)
	{
		g_drawn = 0;
		if (!g_installed || !postprocess::HasLighting2D())
			return;
		g_time += dt;

		Engine* engine = Engine::Get();
		const uint32_t scene = Engine::GetScene();
		if (!engine || scene == HRL_INVALID_ID)
			return;

		// Viewports gone (players destroyed) : forget them.
		g_views.erase(std::remove_if(g_views.begin(), g_views.end(),
		                             [](const View& v) { return !HRL_IsValidViewport(v.viewport); }),
		              g_views.end());
		for (PlayerController* player : engine->GetPlayers())
			AttachTo(player);

		// Off : the default post process of HRL, exactly as without the 2D lighting.
		postprocess::SetLighting2DActive(IsEnabled());

		if (!IsEnabled())
		{
			// 2D lights in the level but the lighting is off : say it once (else "nothing happens").
			static bool warned = false;
			if (!warned && !Light2DComponent::GetAll().empty())
			{
				warned = true;
				std::cout << "[LIGHTING 2D] warning : the level has 2D lights but the 2D lighting is off "
				             "(window 2D Lighting > Enabled, or Lighting2D.set(\"enabled\", true))\n";
			}
			return;
		}

		for (View& v : g_views)
		{
			PlayerController* owner = nullptr;
			for (PlayerController* player : engine->GetPlayers())
				if (player->GetViewportBackend() == v.viewport)
					owner = player;
			UpdateView(v, owner, scene);
		}
	}

	bool IsEnabled()
	{
		return Get1("enabled") != 0.f;
	}

	bool GetValue(const std::string& name, float out[3])
	{
		auto& values = Values();
		const auto it = values.find(name);
		if (it == values.end())
			return false;
		for (int i = 0; i < 3; ++i)
			out[i] = it->second.v[i];
		return true;
	}

	float GetFloat(const std::string& name)
	{
		float v[3] = {};
		GetValue(name, v);
		return v[0];
	}

	bool SetValue(const std::string& name, const float* values, int count)
	{
		const ParamInfo* p = FindParam(name);
		if (!p || !values || count <= 0)
			return false;
		Value& value = Values()[name];
		const int n = std::min(count, Components(*p));
		for (int i = 0; i < n; ++i)
		{
			float v = values[i];
			if (p->type == ParamType::Toggle)
				v = v != 0.f ? 1.f : 0.f;
			if (value.v[i] != v)
				g_dirty = true;
			value.v[i] = v;
		}
		if (p->type == ParamType::Color && count == 1)
			value.v[1] = value.v[2] = value.v[0];
		ApplyAll();
		return true;
	}

	bool SetFloat(const std::string& name, float value)
	{
		return SetValue(name, &value, 1);
	}

	bool SetColor(const std::string& name, float r, float g, float b)
	{
		const float v[3] = { r, g, b };
		return SetValue(name, v, 3);
	}

	void Reset(const std::string& name)
	{
		for (const ParamInfo& p : kParams)
			if (name.empty() || name == p.name)
				SetValue(p.name, p.defaults, 3);
	}

	bool IsModified(const std::string& name)
	{
		const ParamInfo* p = FindParam(name);
		if (!p)
			return false;
		const Value& value = Values()[name];
		for (int i = 0; i < Components(*p); ++i)
			if (std::fabs(value.v[i] - p->defaults[i]) > 1e-5f)
				return true;
		return false;
	}

	bool Load(const std::string& path)
	{
		for (const ParamInfo& p : kParams)
			Values()[p.name] = { { p.defaults[0], p.defaults[1], p.defaults[2] } };

		bool ok = false;
		if (fs::Exists(path))
		{
			const auto data = fs::ReadBinary(path);
			const nlohmann::json json = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
			if (json.is_object())
			{
				ok = true;
				for (const ParamInfo& p : kParams)
				{
					const auto it = json.find(p.name);
					if (it == json.end())
						continue;
					Value& value = Values()[p.name];
					if (it->is_array())
					{
						for (int i = 0; i < Components(p) && i < static_cast<int>(it->size()); ++i)
							if ((*it)[i].is_number())
								value.v[i] = (*it)[i].get<float>();
					}
					else if (it->is_number())
						value.v[0] = it->get<float>();
					else if (it->is_boolean())
						value.v[0] = it->get<bool>() ? 1.f : 0.f;
				}
			}
			else
				std::cout << "[LIGHTING 2D] " << path << " : invalid JSON, default values used\n";
		}
		g_dirty = false;
		ApplyAll();
		return ok;
	}

	bool Save(const std::string& path)
	{
		nlohmann::json json = nlohmann::json::object();
		for (const ParamInfo& p : kParams)
		{
			const Value& value = Values()[p.name];
			if (p.type == ParamType::Color)
				json[p.name] = { value.v[0], value.v[1], value.v[2] };
			else if (p.type == ParamType::Toggle)
				json[p.name] = value.v[0] != 0.f;
			else
				json[p.name] = value.v[0];
		}
		const std::filesystem::path disk = std::filesystem::path("assets") / path;
		std::error_code error;
		std::filesystem::create_directories(disk.parent_path(), error);
		std::ofstream out(disk, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			std::cout << "[LIGHTING 2D] could not write " << disk.generic_string() << "\n";
			return false;
		}
		out << json.dump(2) << "\n";
		g_dirty = false;
		return true;
	}

	bool IsDirty()
	{
		return g_dirty;
	}

	int GetDrawnLightCount()
	{
		return g_drawn;
	}

	const std::string& GetError()
	{
		return g_error;
	}
}
