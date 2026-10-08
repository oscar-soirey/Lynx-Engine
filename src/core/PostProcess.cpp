#include "PostProcess.h"

#include "Engine.h"
#include "Lighting2D.h"
#include "Filesystem.h"
#include "../gameplay/PlayerController.h"

#include <hrl/hrl.h>
#include <json/json.hpp>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace lynx::postprocess
{
	namespace
	{
		using PT = ParamType;

		// Uniforms du shader HRL_DEFAULT_POST_PROCESS_SHADER (valeurs par
		// defaut du shader, sauf la vignette : 0.4 comme avant).
		const std::vector<ParamInfo> kParams = {
			{ "exposure", "Exposure", "Color", PT::Float, {0.f}, -5.f, 5.f, "EV : +1 = twice as bright.", false },
			{ "brightness", "Brightness", "Color", PT::Float, {1.f}, 0.f, 3.f, "Multiplies the color.", false },
			{ "contrast", "Contrast", "Color", PT::Float, {1.f}, 0.f, 3.f, "", false },
			{ "saturation", "Saturation", "Color", PT::Float, {1.f}, 0.f, 3.f, "0 = black and white.", false },
			{ "hueShift", "Hue shift", "Color", PT::Float, {0.f}, -180.f, 180.f, "Degrees.", false },
			{ "tintColor", "Tint", "Color", PT::Color, {1.f, 1.f, 1.f}, 0.f, 1.f, "Multiplies the color.", false },
			{ "invertColor", "Invert colors", "Color", PT::Toggle, {0.f}, 0.f, 1.f, "", true },
			{ "gamma", "Gamma", "Color", PT::Float, {2.2f}, 0.5f, 4.f, "Used when Gamma correction is on.", false },
			{ "gammaCorrectionEnabled", "Gamma correction", "Color", PT::Toggle, {0.f}, 0.f, 1.f, "", false },
			{ "toneMappingEnabled", "Tone mapping", "Color", PT::Toggle, {0.f}, 0.f, 1.f, "HDR -> screen (filmic curve).", false },

			{ "bloomStrength", "Bloom", "Effects", PT::Float, {1.f}, 0.f, 5.f, "Glow of the bright pixels (emissive voxels...).", false },
			{ "sharpenStrength", "Sharpen", "Effects", PT::Float, {0.f}, 0.f, 1.f, "", false },
			{ "chromaticAberration", "Chromatic aberration", "Effects", PT::Float, {0.f}, 0.f, 10.f, "Pixels.", false },
			{ "filmGrainStrength", "Film grain", "Effects", PT::Float, {0.f}, 0.f, 1.f, "", false },
			{ "filmGrainScale", "Film grain scale", "Effects", PT::Float, {1.f}, 0.1f, 4.f, "", false },

			{ "vignetteStrength", "Vignette", "Vignette", PT::Float, {0.4f}, 0.f, 1.f, "", false },
			{ "vignetteRadius", "Radius", "Vignette", PT::Float, {0.75f}, 0.f, 2.f, "Normalized radius.", false },
			{ "vignetteSoftness", "Softness", "Vignette", PT::Float, {0.25f}, 0.f, 1.f, "", false },
			{ "vignetteColor", "Color", "Vignette", PT::Color, {0.f, 0.f, 0.f}, 0.f, 1.f, "", false },

			{ "fadeAmount", "Fade", "Fade", PT::Float, {0.f}, 0.f, 1.f, "0 = the scene, 1 = the fade color (transitions).", false },
			{ "fadeColor", "Fade color", "Fade", PT::Color, {0.f, 0.f, 0.f}, 0.f, 1.f, "", false },

			{ "hdrDisplayCompressionEnabled", "HDR display compression", "HDR", PT::Toggle, {1.f}, 0.f, 1.f, "Soft roll-off of the very bright colors.", false },
			{ "hdrDisplayKnee", "Knee", "HDR", PT::Float, {0.9f}, 0.f, 1.f, "Where the compression starts.", false },
			{ "hdrDisplayCompression", "Compression", "HDR", PT::Float, {0.75f}, 0.f, 1.f, "", false },
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

		uint32_t g_material = HRL_INVALID_ID;   // the first default one (GetMaterial)

		// Per viewport : the default post process of HRL (2D lighting off : exactly
		// as before), and the same shader with the 2D lighting (on). One of them is
		// the post process of the viewport.
		struct ViewPost
		{
			uint32_t viewport = HRL_INVALID_ID;
			uint32_t material = HRL_INVALID_ID;       // HRL_DEFAULT_POST_PROCESS_SHADER
			uint32_t lit_material = HRL_INVALID_ID;   // + 2D lighting
			uint32_t post = HRL_INVALID_ID;
			bool lit = false;
		};
		std::vector<ViewPost> g_views;
		uint32_t g_lit_shader = HRL_INVALID_ID;
		bool g_lighting2d = false;   // the shader with the 2D lighting compiled
		bool g_installed = false;

		// Fragment shader of HRL_DEFAULT_POST_PROCESS_SHADER (copied from HRL), with
		// the 2D lighting (Lighting2D.cpp) inserted : one pass, the lighting is applied
		// on the scene color before exposure, bloom and tone mapping. Keep in sync
		// with HRL when its default post process changes.
		const char* kPostVertex = R"(#version 330 core
layout(location=0) in vec2 apos;
out vec2 uv;
void main()
{
    uv = apos * 0.5 + 0.5;
    gl_Position = vec4(apos, 0.0, 1.0);
}
)";

		const char* kPostFragment = R"GLSL(#version 330 core

in vec2 uv;
out vec4 frag_color;

// Common
uniform sampler2D uScene;
uniform sampler2D uBrightScene;
uniform vec2 uScreenSize;
uniform float uTime;

// Color controls
uniform float brightness = 1.0;
uniform float contrast = 1.0;
uniform float saturation = 1.0;
uniform float gamma = 2.2;
uniform float exposure = 0.0;
uniform float hueShift = 0.0;          // degrees
uniform vec3 tintColor = vec3(1.0);
uniform bool invertColor = false;

// Bloom
// 1.0 means HDR bright pixels keep their natural energy in the bloom.
// Set to 5.0, for example, to amplify the glow fivefold.
uniform float bloomStrength = 1.0;

// Screen-space effects
uniform float sharpenStrength = 0.0;   // 0 = disabled, 1 = full strength
uniform float chromaticAberration = 0.0; // pixels
uniform float filmGrainStrength = 0.0; // 0..1
uniform float filmGrainScale = 1.0;

// Optional tone mapping / display transform. Disabled by default so a post-process
// with no explicit color controls preserves the scene color exactly.
uniform int toneMappingEnabled = 0;
uniform int gammaCorrectionEnabled = 0;

// Global HDR display compression. Scene lighting stays HDR until after bloom;
// this is the single place where values above the display range are compressed.
// Values below the knee are untouched, while highlights approach 1 smoothly.
uniform int hdrDisplayCompressionEnabled = 1;
uniform float hdrDisplayKnee = 0.90;
uniform float hdrDisplayCompression = 0.75;

// Vignette
uniform float vignetteStrength = 0.0;  // 0..1
uniform float vignetteRadius = 0.75;   // normalized radius
uniform float vignetteSoftness = 0.25;
uniform vec3 vignetteColor = vec3(0.0);

// Fade / color overlay
uniform float fadeAmount = 0.0;        // 0..1
uniform vec3 fadeColor = vec3(0.0);

// sigma faible -> blur serre
float weights[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540540, 0.0162162);

vec4 ApplyGaussianBlur(sampler2D tex)
{
    vec2 texOffset = vec2(1.0 / textureSize(tex, 0));
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;

    // kernel 17x17
    for (int x = -4; x <= 4; x++)
    {
        for (int y = -4; y <= 4; y++)
        {
            float w = weights[abs(x)] * weights[abs(y)];
            result += texture(tex, uv + vec2(texOffset.x * x, texOffset.y * y)).rgb * w;
            totalWeight += w;
        }
    }

    return vec4(result / totalWeight, 1.0);
}

vec3 SampleChromaticScene()
{
    if (chromaticAberration <= 0.0001)
        return texture(uScene, uv).rgb;

    vec2 centered = uv - vec2(0.5);
    float lenCenter = length(centered);
    vec2 direction = lenCenter > 0.0001 ? centered / lenCenter : vec2(0.0);
    vec2 offset = direction * (chromaticAberration / max(uScreenSize, vec2(1.0)));

    float r = texture(uScene, clamp(uv + offset, 0.0, 1.0)).r;
    float g = texture(uScene, uv).g;
    float b = texture(uScene, clamp(uv - offset, 0.0, 1.0)).b;
    return vec3(r, g, b);
}

vec3 ApplySharpen(vec3 color)
{
    if (sharpenStrength <= 0.0001)
        return color;

    vec2 texel = 1.0 / max(uScreenSize, vec2(1.0));
    vec3 left  = texture(uScene, clamp(uv + vec2(-texel.x, 0.0), 0.0, 1.0)).rgb;
    vec3 right = texture(uScene, clamp(uv + vec2( texel.x, 0.0), 0.0, 1.0)).rgb;
    vec3 up    = texture(uScene, clamp(uv + vec2(0.0,  texel.y), 0.0, 1.0)).rgb;
    vec3 down  = texture(uScene, clamp(uv + vec2(0.0, -texel.y), 0.0, 1.0)).rgb;

    vec3 blur = (left + right + up + down) * 0.25;
    return max(vec3(0.0), color + (color - blur) * sharpenStrength);
}

vec3 ApplyHueShift(vec3 color, float degrees)
{
    if (abs(degrees) <= 0.0001)
        return color;

    float angle = radians(degrees);
    float c = cos(angle);
    float s = sin(angle);

    // Rodrigues rotation around the luminance axis.
    const vec3 axis = normalize(vec3(1.0, 1.0, 1.0));
    return color * c + cross(axis, color) * s + axis * dot(axis, color) * (1.0 - c);
}

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 CompressHDRForDisplay(vec3 color)
{
    color = max(color, vec3(0.0));
    float peak = max(color.r, max(color.g, color.b));
    float knee = clamp(hdrDisplayKnee, 0.0, 0.999);
    float compression = max(hdrDisplayCompression, 0.0001);

    if (peak <= knee)
        return color;

    // Smooth asymptotic shoulder. Scaling the whole RGB vector together
    // preserves the emissive/light color instead of independently clipping
    // channels toward white. The long shoulder keeps spatial lighting
    // gradients visible instead of producing a bright plateau around emitters.
    float excess = peak - knee;
    float limitedPeak = knee + (1.0 - knee) * (excess / (excess + compression));
    return color * (limitedPeak / max(peak, 1e-6));
}

void main()
{
    // Scene sample, optionally with radial chromatic aberration.
    vec3 color = SampleChromaticScene();

    // Exposure is expressed in stops: +1 doubles the linear HDR light, -1 halves it.
    color *= exp2(exposure);

    // Brightness in linear HDR space.
    color *= brightness;

    // Bloom must be combined before tone mapping so bright HDR highlights are
    // compressed together with the main image.
    vec3 bloom = ApplyGaussianBlur(uBrightScene).rgb * bloomStrength;
    color += bloom;

    // Apply an optional tone mapper when explicitly requested. Otherwise use
    // one global soft HDR shoulder so lighting gradients stay smooth instead
    // of locally clipping every pixel above 1.0 into the same white value.
    if (toneMappingEnabled != 0)
    {
        vec3 x = max(color, vec3(0.0));
        const float a = 2.51;
        const float b = 0.03;
        const float c = 2.43;
        const float d = 0.59;
        const float e = 0.14;
        color = clamp((x * (a * x + b)) / max(x * (c * x + d) + e, vec3(1e-5)), 0.0, 1.0);
    }
    else if (hdrDisplayCompressionEnabled != 0)
    {
        color = CompressHDRForDisplay(color);
    }

    // Contrast.
    color = (color - 0.5) * contrast + 0.5;

    // Saturation.
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, saturation);

    // Hue rotation.
    color = ApplyHueShift(color, hueShift);

    // Gamma correction is also opt-in.
    if (gammaCorrectionEnabled != 0)
        color = pow(max(color, vec3(0.0)), vec3(1.0 / max(gamma, 0.0001)));

    // Tint.
    color *= tintColor;

    // Optional sharpening of the scene image.
    color = ApplySharpen(color);

    // Color inversion.
    color = mix(color, 1.0 - color, float(invertColor));

    // Vignette. Radius is normalized to the distance from the center to a screen edge.
    // Strength accepts both the documented 0..1 range and percentage-style values
    // such as 100.0 (which is clamped to full strength).
    if (vignetteStrength > 0.0001)
    {
        vec2 centered = (uv - vec2(0.5)) * 2.0;
        float dist = length(centered);
        float radius = max(vignetteRadius, 0.0001);
        float softness = max(vignetteSoftness, 0.0001);
        float mask = smoothstep(radius, radius + softness, dist);
        float strength = clamp(vignetteStrength, 0.0, 1.0);
        color = mix(color, vignetteColor, clamp(mask * strength, 0.0, 1.0));
    }

    // Animated film grain.
    if (filmGrainStrength > 0.0001)
    {
        vec2 grainUV = uv * max(filmGrainScale, 0.0001) * uScreenSize;
        float noise = Hash12(grainUV + vec2(uTime * 17.13, uTime * 9.71));
        float grain = (noise - 0.5) * filmGrainStrength;
        color += grain;
    }

    // Final color fade.
    color = mix(color, fadeColor, clamp(fadeAmount, 0.0, 1.0));

    frag_color = vec4(max(color, vec3(0.0)), 1.0);
}
)GLSL";

		uint32_t CreateShader()
		{
			std::string frag = kPostFragment;
			const size_t main_pos = frag.find("void main()");
			const std::string call = "vec3 color = SampleChromaticScene();";
			const size_t call_pos = frag.find(call);
			if (main_pos == std::string::npos || call_pos == std::string::npos)
				return HRL_INVALID_ID;
			frag.insert(call_pos + call.size(), "\n    color = ApplyLighting2D(color);");
			// HRL gives its bright-pass texture (uBrightScene) to its own shader only :
			// here the bloom takes the HDR part (> 1) of the scene itself.
			const std::string bright = "ApplyGaussianBlur(uBrightScene)";
			const size_t bright_pos = frag.find(bright);
			if (bright_pos == std::string::npos)
				return HRL_INVALID_ID;
			frag.replace(bright_pos, bright.size(), "BrightBlur()");
			frag.insert(main_pos, lighting2d::ShaderCode() + R"(
// Bloom source : the part of the scene above 1 (HDR), blurred (same kernel as ApplyGaussianBlur).
vec4 BrightBlur()
{
    vec2 texOffset = vec2(1.0 / textureSize(uScene, 0));
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;
    for (int x = -4; x <= 4; x++)
        for (int y = -4; y <= 4; y++)
        {
            float w = weights[abs(x)] * weights[abs(y)];
            result += max(texture(uScene, uv + vec2(texOffset.x * x, texOffset.y * y)).rgb - vec3(1.0), vec3(0.0)) * w;
            totalWeight += w;
        }
    return vec4(result / totalWeight, 1.0);
}
)");
			return HRL_CreateShader(kPostVertex, std::strlen(kPostVertex), frag.c_str(), frag.size());
		}
		bool g_dirty = false;

		int Components(const ParamInfo& p)
		{
			return p.type == ParamType::Color ? 3 : 1;
		}

		void ApplyOneTo(uint32_t material, const ParamInfo& p)
		{
			const Value& value = Values()[p.name];
			switch (p.type)
			{
			case ParamType::Float:
				HRL_MaterialSetFloat(material, p.name, value.v[0]);
				break;
			case ParamType::Toggle:
				if (p.is_bool)
					HRL_MaterialSetBool(material, p.name, value.v[0] != 0.f ? HRL_TRUE : HRL_FALSE);
				else
					HRL_MaterialSetInt(material, p.name, value.v[0] != 0.f ? 1 : 0);
				break;
			case ParamType::Color:
				HRL_MaterialSetVec3(material, p.name, value.v[0], value.v[1], value.v[2]);
				break;
			}
		}

		void ApplyOne(const ParamInfo& p)
		{
			for (const ViewPost& v : g_views)
			{
				ApplyOneTo(v.material, p);
				if (v.lit_material != HRL_INVALID_ID)
					ApplyOneTo(v.lit_material, p);
			}
		}

		void AttachTo(PlayerController* player)
		{
			if (!player || player->GetViewportBackend() == HRL_INVALID_ID)
				return;
			const uint32_t viewport = player->GetViewportBackend();
			for (const ViewPost& v : g_views)
				if (v.viewport == viewport)
					return;
			ViewPost view;
			view.viewport = viewport;
			view.material = HRL_CreateMaterial(HRL_DEFAULT_POST_PROCESS_SHADER);
			if (view.material == HRL_INVALID_ID)
				return;
			if (g_lit_shader != HRL_INVALID_ID)
				view.lit_material = HRL_CreateMaterial(g_lit_shader);
			for (const ParamInfo& p : kParams)
			{
				ApplyOneTo(view.material, p);
				if (view.lit_material != HRL_INVALID_ID)
					ApplyOneTo(view.lit_material, p);
			}
			if (g_material == HRL_INVALID_ID)
				g_material = view.material;
			view.post = HRL_CreatePostProcess(viewport, view.material, 1);
			g_views.push_back(view);
		}
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
		// The default post process of HRL + the 2D lighting (one pass) ; if it does
		// not compile, the default post process alone.
		g_lit_shader = CreateShader();
		g_lighting2d = g_lit_shader != HRL_INVALID_ID;
		if (!g_lighting2d)
			std::cout << "[POST PROCESS] shader with the 2D lighting not compiled : no 2D lighting\n";

		// Same post process on the viewport of every player, also the ones
		// the game creates later (Engine::CreatePlayer).
		for (PlayerController* player : engine->GetPlayers())
			AttachTo(player);
		engine->ED_player_created.Subscribe([](PlayerController* player) { AttachTo(player); });

		Load();
	}

	uint32_t GetMaterial()
	{
		return g_material;
	}

	uint32_t GetMaterialFor(uint32_t viewport)
	{
		for (const ViewPost& v : g_views)
			if (v.viewport == viewport)
				return v.lit_material;
		return HRL_INVALID_ID;
	}

	void SetLighting2DActive(bool active)
	{
		for (ViewPost& v : g_views)
		{
			if (v.lit == active || v.lit_material == HRL_INVALID_ID || !HRL_IsValidViewport(v.viewport))
				continue;
			if (v.post != HRL_INVALID_ID && HRL_IsValidPostProcess(v.post))
				HRL_DeletePostProcess(v.post);
			v.post = HRL_CreatePostProcess(v.viewport, active ? v.lit_material : v.material, 1);
			v.lit = active;
		}
	}

	bool HasLighting2D()
	{
		return g_lighting2d;
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
		// Une couleur donnee par une seule valeur : gris.
		if (p->type == ParamType::Color && count == 1)
			value.v[1] = value.v[2] = value.v[0];

		ApplyOne(*p);
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

	void Apply()
	{
		for (const ParamInfo& p : kParams)
			ApplyOne(p);
	}

	bool Load(const std::string& path)
	{
		// Valeurs par defaut, puis celles du fichier.
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
			{
				std::cout << "[POST PROCESS] " << path << " : invalid JSON, default values used\n";
			}
		}

		g_dirty = false;
		Apply();
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
			std::cout << "[POST PROCESS] could not write " << disk.generic_string() << "\n";
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
}
