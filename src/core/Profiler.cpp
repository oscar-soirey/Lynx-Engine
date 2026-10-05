#include "Profiler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace lynx::profiler
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr size_t kFrameHistory = 300;     // frames kept for the graph
		constexpr double kSmoothing = 0.05;       // average : ~20 frames
		constexpr double kMaxWindowSeconds = 2.0; // max over the last 2 s

		struct Open
		{
			const char* name;
			Clock::time_point start;
		};

		struct Zone
		{
			std::string name;
			int depth = 0;
			int order = 0;
			// current frame
			double frame_ms = 0.0;
			int frame_calls = 0;
			// published
			double last_ms = 0.0;
			int last_calls = 0;
			double average_ms = 0.0;
			double max_ms = 0.0;
			double max_age = 0.0;      // seconds since max_ms was measured
			bool seen = false;
			bool has_average = false;
		};

		std::atomic<bool> g_enabled{ false };
		std::mutex g_mutex;   // GetSnapshot (UI) against EndFrame / zones (main thread)

		// Only the thread that calls EndFrame (the main loop) is recorded :
		// the other threads are for Tracy.
		std::thread::id g_main_thread;
		bool g_main_known = false;

		std::vector<Open> g_stack;
		// Keyed by the name pointer (string literals : no string compare per
		// zone). The same name from two modules (lynx.dll, the game DLL) gives
		// two keys : merged by name in GetSnapshot.
		std::unordered_map<const char*, Zone> g_zones;
		int g_next_order = 0;

		std::deque<float> g_frames;
		Clock::time_point g_last_frame;
		bool g_has_last_frame = false;
		double g_average_frame = 0.0;

		bool OnMainThread()
		{
			return g_main_known && std::this_thread::get_id() == g_main_thread;
		}
	}


	void SetEnabled(bool enabled)
	{
		if (g_enabled.exchange(enabled) == enabled)
			return;
		std::lock_guard<std::mutex> lock(g_mutex);
		g_stack.clear();
		g_has_last_frame = false;   // no giant first frame after a pause
	}

	bool IsEnabled()
	{
		return g_enabled.load(std::memory_order_relaxed);
	}


	bool BeginZone(const char* name)
	{
		if (!g_enabled.load(std::memory_order_relaxed) || !name || !OnMainThread())
			return false;

		// Order and depth are taken when the zone opens : a parent comes
		// before its children in the list.
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			Zone& zone = g_zones[name];
			if (!zone.seen)
			{
				zone.seen = true;
				zone.name = name;
				zone.depth = static_cast<int>(g_stack.size());
				zone.order = g_next_order++;
			}
		}

		g_stack.push_back({ name, Clock::now() });
		return true;
	}


	void EndZone()
	{
		if (g_stack.empty())
			return;
		const Open open = g_stack.back();
		g_stack.pop_back();
		const double ms = std::chrono::duration<double, std::milli>(Clock::now() - open.start).count();

		std::lock_guard<std::mutex> lock(g_mutex);
		Zone& zone = g_zones[open.name];
		zone.frame_ms += ms;
		++zone.frame_calls;
	}


	void EndFrame()
	{
		if (!g_main_known)
		{
			g_main_thread = std::this_thread::get_id();
			g_main_known = true;
		}
		if (!g_enabled.load(std::memory_order_relaxed))
			return;

		const Clock::time_point now = Clock::now();
		std::lock_guard<std::mutex> lock(g_mutex);

		if (g_has_last_frame)
		{
			const double frame_ms = std::chrono::duration<double, std::milli>(now - g_last_frame).count();
			const double seconds = frame_ms / 1000.0;

			g_frames.push_back(static_cast<float>(frame_ms));
			while (g_frames.size() > kFrameHistory)
				g_frames.pop_front();
			g_average_frame = g_average_frame <= 0.0 ? frame_ms : g_average_frame + (frame_ms - g_average_frame) * kSmoothing;

			for (auto& [key, zone] : g_zones)
			{
				zone.last_ms = zone.frame_ms;
				zone.last_calls = zone.frame_calls;
				if (!zone.has_average && zone.frame_calls > 0)
				{
					zone.average_ms = zone.frame_ms;   // first measure : no slow start from 0
					zone.has_average = true;
				}
				else
				{
					zone.average_ms += (zone.frame_ms - zone.average_ms) * kSmoothing;
				}
				zone.max_age += seconds;
				if (zone.frame_ms >= zone.max_ms || zone.max_age > kMaxWindowSeconds)
				{
					zone.max_ms = zone.frame_ms;
					zone.max_age = 0.0;
				}
				zone.frame_ms = 0.0;
				zone.frame_calls = 0;
			}
		}

		g_last_frame = now;
		g_has_last_frame = true;
		g_stack.clear();   // a zone left open across frames is dropped
	}


	bool TracyAvailable()
	{
#ifdef TRACY_ENABLE
		return true;
#else
		return false;
#endif
	}

	bool TracyConnected()
	{
#if defined(TRACY_ENABLE) && defined(TRACY_ON_DEMAND)
		return TracyIsConnected;
#else
		return false;
#endif
	}


	void GetSnapshot(Snapshot& out)
	{
		std::lock_guard<std::mutex> lock(g_mutex);

		out.frame_ms.assign(g_frames.begin(), g_frames.end());
		out.average_frame_ms = g_average_frame;
		out.max_frame_ms = g_frames.empty() ? 0.0 : *std::max_element(g_frames.begin(), g_frames.end());

		// Merge the zones of the same name (several modules), keep the order.
		std::vector<const Zone*> zones;
		for (const auto& [key, zone] : g_zones)
			zones.push_back(&zone);
		std::sort(zones.begin(), zones.end(), [](const Zone* a, const Zone* b) { return a->order < b->order; });

		out.zones.clear();
		for (const Zone* zone : zones)
		{
			auto it = std::find_if(out.zones.begin(), out.zones.end(),
			                       [&](const ZoneStats& s) { return s.name == zone->name; });
			if (it == out.zones.end())
			{
				out.zones.push_back({ zone->name, zone->depth, zone->last_ms, zone->average_ms, zone->max_ms, zone->last_calls });
			}
			else
			{
				it->last_ms += zone->last_ms;
				it->average_ms += zone->average_ms;
				it->max_ms = std::max(it->max_ms, zone->max_ms);
				it->calls += zone->last_calls;
			}
		}
	}


	void Reset()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_zones.clear();
		g_frames.clear();
		g_next_order = 0;
		g_average_frame = 0.0;
		g_has_last_frame = false;
	}
}
