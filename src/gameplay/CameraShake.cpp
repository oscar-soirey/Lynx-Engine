#include "CameraShake.h"

#include "../core/Filesystem.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include <json/json.hpp>

static lynx::CameraShake camera_shake_;

namespace lynx
{
	void SetCameraShake(CameraShake &camera_shake)
	{
		camera_shake_ = camera_shake;
	}

	CameraShake &GetCameraShake()
	{
		return camera_shake_;
	}

	bool LoadCameraShakeSettings(const std::string& path)
	{
		if (!fs::Exists(path))
			return false;

		const auto data = fs::ReadBinary(path);
		const nlohmann::json json = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
		if (!json.is_object())
		{
			std::cout << "[CAMERA SHAKE] " << path << " : invalid JSON, default values used\n";
			return false;
		}

		CameraShake& s = camera_shake_;
		auto number = [&](const char* key, float& value, float min, float max)
		{
			const auto it = json.find(key);
			if (it != json.end() && it->is_number())
				value = std::clamp(it->get<float>(), min, max);
		};
		auto boolean = [&](const char* key, bool& value)
		{
			const auto it = json.find(key);
			if (it != json.end() && it->is_boolean())
				value = it->get<bool>();
		};

		boolean("enabled", s.enabled);
		number("duration", s.duration, 0.01f, 10.f);
		number("position_amplitude", s.positionAmplitude, 0.f, 200.f);
		number("rotation_amplitude", s.rotationAmplitude, 0.f, 90.f);
		number("frequency", s.frequency, 0.1f, 200.f);
		number("falloff", s.falloff, 0.01f, 20.f);
		boolean("shake_x", s.shake_x);
		boolean("shake_y", s.shake_y);
		boolean("shake_roll", s.shake_roll);
		return true;
	}

	bool SaveCameraShakeSettings(const std::string& path)
	{
		const CameraShake& s = camera_shake_;
		nlohmann::json json = nlohmann::json::object();
		json["enabled"] = s.enabled;
		json["duration"] = s.duration;
		json["position_amplitude"] = s.positionAmplitude;
		json["rotation_amplitude"] = s.rotationAmplitude;
		json["frequency"] = s.frequency;
		json["falloff"] = s.falloff;
		json["shake_x"] = s.shake_x;
		json["shake_y"] = s.shake_y;
		json["shake_roll"] = s.shake_roll;

		const std::filesystem::path disk = std::filesystem::path("assets") / path;
		std::error_code error;
		std::filesystem::create_directories(disk.parent_path(), error);

		std::ofstream out(disk, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			std::cout << "[CAMERA SHAKE] could not write " << disk.generic_string() << "\n";
			return false;
		}
		out << json.dump(2) << "\n";
		return true;
	}
}
