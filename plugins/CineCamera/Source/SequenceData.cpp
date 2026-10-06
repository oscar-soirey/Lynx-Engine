#include "SequenceData.h"

#include <json/json.hpp>

#include <algorithm>
#include <cmath>

namespace sequence
{
	using Json = nlohmann::json;

	namespace
	{
		V3 ReadV3(const Json& j, const char* key, V3 fallback = {})
		{
			const auto it = j.find(key);
			if (it == j.end() || !it->is_array() || it->size() < 3)
				return fallback;
			return { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
		}

		Json WriteV3(const V3& v)
		{
			return Json::array({ v.x, v.y, v.z });
		}

		float Num(const Json& j, const char* key, float fallback = 0.f)
		{
			const auto it = j.find(key);
			return it != j.end() && it->is_number() ? it->get<float>() : fallback;
		}

		std::string Str(const Json& j, const char* key, const std::string& fallback = {})
		{
			const auto it = j.find(key);
			return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
		}

		V3 Lerp(const V3& a, const V3& b, float k)
		{
			return { a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k };
		}

		template<typename T>
		void SortByTime(std::vector<T>& keys)
		{
			std::stable_sort(keys.begin(), keys.end(), [](const T& a, const T& b) { return a.t < b.t; });
		}
	}

	void Sequence::Sort()
	{
		SortByTime(cuts);
		SortByTime(events);
		SortByTime(fades);
		for (Track& track : tracks)
			SortByTime(track.keys);
	}

	int Sequence::CutAt(float t) const
	{
		int found = -1;
		for (int i = 0; i < static_cast<int>(cuts.size()); ++i)
			if (cuts[i].t <= t + 1e-5f)
				found = i;
		return found;
	}

	bool Sequence::FadeAt(float t, float& value, V3& color) const
	{
		if (fades.empty())
			return false;
		if (t <= fades.front().t)
		{
			value = fades.front().value;
			color = fades.front().color;
			return true;
		}
		for (size_t i = 0; i + 1 < fades.size(); ++i)
		{
			const Fade& a = fades[i];
			const Fade& b = fades[i + 1];
			if (t >= a.t && t <= b.t)
			{
				const float k = b.t > a.t ? (t - a.t) / (b.t - a.t) : 1.f;
				value = a.value + (b.value - a.value) * k;
				color = Lerp(a.color, b.color, k);
				return true;
			}
		}
		value = fades.back().value;
		color = fades.back().color;
		return true;
	}

	bool Sample(const Track& track, float t, V3& location, V3& rotation, V3& scale)
	{
		const auto& keys = track.keys;
		if (keys.empty())
			return false;

		if (t <= keys.front().t)
		{
			location = keys.front().location; rotation = keys.front().rotation; scale = keys.front().scale;
			return true;
		}
		if (t >= keys.back().t)
		{
			location = keys.back().location; rotation = keys.back().rotation; scale = keys.back().scale;
			return true;
		}

		for (size_t i = 0; i + 1 < keys.size(); ++i)
		{
			const TransformKey& a = keys[i];
			const TransformKey& b = keys[i + 1];
			if (t < a.t || t > b.t)
				continue;

			float k = b.t > a.t ? (t - a.t) / (b.t - a.t) : 1.f;
			if (a.interp == "step")
				k = 0.f;
			else if (a.interp == "smooth")
				k = k * k * (3.f - 2.f * k);

			location = Lerp(a.location, b.location, k);
			rotation = Lerp(a.rotation, b.rotation, k);
			scale = Lerp(a.scale, b.scale, k);
			return true;
		}
		return false;
	}

	bool Parse(const std::string& text, Sequence& out, std::string& error)
	{
		out = Sequence{};
		const Json j = Json::parse(text, nullptr, false, true);
		if (!j.is_object())
		{
			error = "not a JSON object";
			return false;
		}

		out.length = std::max(0.1f, Num(j, "length", 5.f));

		if (j.contains("cuts") && j["cuts"].is_array())
			for (const Json& c : j["cuts"])
				out.cuts.push_back({ Num(c, "t"), Str(c, "camera"), Num(c, "blend") });

		if (j.contains("tracks") && j["tracks"].is_array())
			for (const Json& tr : j["tracks"])
			{
				Track track;
				track.actor = Str(tr, "actor");
				if (tr.contains("keys") && tr["keys"].is_array())
					for (const Json& k : tr["keys"])
					{
						TransformKey key;
						key.t = Num(k, "t");
						key.location = ReadV3(k, "location");
						key.rotation = ReadV3(k, "rotation");
						key.scale = ReadV3(k, "scale", { 1.f, 1.f, 1.f });
						key.interp = Str(k, "interp", "smooth");
						track.keys.push_back(key);
					}
				out.tracks.push_back(track);
			}

		if (j.contains("events") && j["events"].is_array())
			for (const Json& e : j["events"])
				out.events.push_back({ Num(e, "t"), Str(e, "name"), Str(e, "script") });

		if (j.contains("fades") && j["fades"].is_array())
			for (const Json& f : j["fades"])
				out.fades.push_back({ Num(f, "t"), Num(f, "value"), ReadV3(f, "color") });

		out.Sort();
		return true;
	}

	std::string Serialize(const Sequence& s)
	{
		Json j = Json::object();
		j["length"] = s.length;

		Json cuts = Json::array();
		for (const Cut& c : s.cuts)
			cuts.push_back({ { "t", c.t }, { "camera", c.camera }, { "blend", c.blend } });
		j["cuts"] = cuts;

		Json tracks = Json::array();
		for (const Track& tr : s.tracks)
		{
			Json keys = Json::array();
			for (const TransformKey& k : tr.keys)
				keys.push_back({ { "t", k.t }, { "location", WriteV3(k.location) }, { "rotation", WriteV3(k.rotation) },
				                 { "scale", WriteV3(k.scale) }, { "interp", k.interp } });
			tracks.push_back({ { "actor", tr.actor }, { "keys", keys } });
		}
		j["tracks"] = tracks;

		Json events = Json::array();
		for (const Event& e : s.events)
		{
			Json je = { { "t", e.t }, { "name", e.name } };
			if (!e.script.empty())
				je["script"] = e.script;
			events.push_back(je);
		}
		j["events"] = events;

		Json fades = Json::array();
		for (const Fade& f : s.fades)
			fades.push_back({ { "t", f.t }, { "value", f.value }, { "color", WriteV3(f.color) } });
		j["fades"] = fades;

		return j.dump(2) + "\n";
	}
}
