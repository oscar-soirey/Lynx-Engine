#include "PostProcess.h"

#include "Engine.h"
#include "Filesystem.h"
#include "../gameplay/PlayerController.h"

#include <hrl/hrl.h>
#include <json/json.hpp>

#include <algorithm>
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

		uint32_t g_material = HRL_INVALID_ID;
		bool g_installed = false;
		bool g_dirty = false;

		int Components(const ParamInfo& p)
		{
			return p.type == ParamType::Color ? 3 : 1;
		}

		void ApplyOne(const ParamInfo& p)
		{
			if (g_material == HRL_INVALID_ID)
				return;

			const Value& value = Values()[p.name];
			switch (p.type)
			{
			case ParamType::Float:
				HRL_MaterialSetFloat(g_material, p.name, value.v[0]);
				break;
			case ParamType::Toggle:
				if (p.is_bool)
					HRL_MaterialSetBool(g_material, p.name, value.v[0] != 0.f ? HRL_TRUE : HRL_FALSE);
				else
					HRL_MaterialSetInt(g_material, p.name, value.v[0] != 0.f ? 1 : 0);
				break;
			case ParamType::Color:
				HRL_MaterialSetVec3(g_material, p.name, value.v[0], value.v[1], value.v[2]);
				break;
			}
		}

		void AttachTo(PlayerController* player)
		{
			if (player && player->GetViewportBackend() != HRL_INVALID_ID && g_material != HRL_INVALID_ID)
				HRL_CreatePostProcess(player->GetViewportBackend(), g_material, 1);
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
		g_material = HRL_CreateMaterial(HRL_DEFAULT_POST_PROCESS_SHADER);

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
