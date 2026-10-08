#include "Profiler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace lynx::profiler
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr size_t kFrameHistory = 300;     // frames kept for the graph
		constexpr double kSmoothing = 0.05;       // average : ~20 frames
		constexpr double kMaxWindowSeconds = 2.0; // max over the last 2 s
		constexpr size_t kMaxZones = 4096;        // beyond : "(too many zones)"
		constexpr const char* kOverflowName = "(too many zones)";

		// ---- Names ---------------------------------------------------------
		// Interned names : one pointer per text, kept forever (a zone name of
		// an unloaded DLL stays valid, two modules share the same zones).
		std::mutex g_intern_mutex;
		std::unordered_set<std::string> g_interned;

		// ---- Zones (main thread only, no lock) -----------------------------
		struct Zone
		{
			const char* name = nullptr;   // interned
			int parent = -1;
			int depth = 0;
			int order = 0;
			bool has_children = false;
			// current frame
			double frame_ms = 0.0;
			double frame_self_ms = 0.0;
			int frame_calls = 0;
			// published
			double last_ms = 0.0;
			double last_self_ms = 0.0;
			int last_calls = 0;
			double average_ms = 0.0;
			double self_average_ms = 0.0;
			double max_ms = 0.0;
			double max_age = 0.0;          // seconds since max_ms was measured
			bool has_average = false;
		};

		struct ZoneKey
		{
			int parent;
			const char* name;
			bool operator==(const ZoneKey& o) const { return parent == o.parent && name == o.name; }
		};

		struct ZoneKeyHash
		{
			size_t operator()(const ZoneKey& k) const
			{
				return std::hash<const void*>()(k.name) ^ (std::hash<int>()(k.parent) * 0x9E3779B97F4A7C15ull);
			}
		};

		struct Open
		{
			int zone;
			Clock::time_point start;
			double child_ms;
			int serial;   // the token of BeginZone
		};

		struct SpikeEntry
		{
			int zone;
			double ms;
			double self_ms;
			int calls;
		};

		struct CounterSlot
		{
			const char* name;
			double value = 0.0;
			double frame_sum = 0.0;
			bool per_frame = false;
			int order = 0;
		};

		std::atomic<bool> g_enabled{ false };
		std::atomic<bool> g_detailed{ true };
		std::atomic<bool> g_restart{ true };   // no giant first frame after a pause
		std::atomic<double> g_spike_threshold{ 50.0 };

		// Only the thread that calls EndFrame (the main loop) is recorded :
		// the other threads are for Tracy.
		std::atomic<bool> g_main_known{ false };
		std::thread::id g_main_thread;

		std::vector<Zone> g_zones;
		std::unordered_map<ZoneKey, int, ZoneKeyHash> g_zone_index;
		std::unordered_map<const char*, const char*> g_name_cache;   // literal -> interned
		std::vector<Open> g_stack;
		int g_next_order = 0;
		int g_next_serial = 0;

		std::unordered_map<const char*, CounterSlot> g_counters;     // key : interned
		int g_next_counter = 0;

		Clock::time_point g_last_frame;
		bool g_has_last_frame = false;

		// ---- Published (read by the UI, any thread) ------------------------
		std::mutex g_mutex;
		std::vector<Zone> g_pub_zones;
		std::deque<float> g_frames;
		double g_average_frame = 0.0;
		std::vector<CounterSlot> g_pub_counters;
		std::vector<SpikeEntry> g_spike;
		double g_spike_ms = 0.0;
		Clock::time_point g_spike_time;
		bool g_has_spike = false;
		int g_spike_count = 0;
		std::vector<Zone> g_spike_zones;   // the zones as they were at the spike
		bool g_reset_requested = false;

		bool OnMainThread()
		{
			return g_main_known.load(std::memory_order_acquire) && std::this_thread::get_id() == g_main_thread;
		}

		const char* InternedName(const char* name)
		{
			auto it = g_name_cache.find(name);
			if (it != g_name_cache.end())
				return it->second;
			const char* interned = Intern(std::string_view(name));
			g_name_cache.emplace(name, interned);
			return interned;
		}

		int FindOrAddZone(int parent, const char* interned)
		{
			const ZoneKey key{ parent, interned };
			auto it = g_zone_index.find(key);
			if (it != g_zone_index.end())
				return it->second;

			if (g_zones.size() >= kMaxZones && interned != kOverflowName)
				return FindOrAddZone(parent, kOverflowName);

			Zone zone;
			zone.name = interned;
			zone.parent = parent;
			zone.depth = parent >= 0 ? g_zones[static_cast<size_t>(parent)].depth + 1 : 0;
			zone.order = g_next_order++;
			if (parent >= 0)
				g_zones[static_cast<size_t>(parent)].has_children = true;

			const int index = static_cast<int>(g_zones.size());
			g_zones.push_back(zone);
			g_zone_index.emplace(key, index);
			return index;
		}

		void CloseTop(Clock::time_point now)
		{
			const Open open = g_stack.back();
			g_stack.pop_back();
			const double ms = std::chrono::duration<double, std::milli>(now - open.start).count();

			Zone& zone = g_zones[static_cast<size_t>(open.zone)];
			zone.frame_ms += ms;
			zone.frame_self_ms += std::max(0.0, ms - open.child_ms);
			++zone.frame_calls;

			if (!g_stack.empty())
				g_stack.back().child_ms += ms;
		}

		void ClearAll()
		{
			g_zones.clear();
			g_zone_index.clear();
			g_name_cache.clear();   // literals of an unloaded DLL : addresses reused
			g_stack.clear();
			g_next_order = 0;
			g_counters.clear();
			g_next_counter = 0;
			g_has_last_frame = false;
		}

		// Tree order (parent, then its children in order of first appearance).
		template <typename Include, typename Fill>
		void BuildTree(const std::vector<Zone>& zones, Include include, Fill fill, std::vector<ZoneStats>& out)
		{
			out.clear();
			std::vector<std::vector<int>> children(zones.size() + 1);   // last : roots
			for (size_t i = 0; i < zones.size(); ++i)
			{
				if (!include(static_cast<int>(i)))
					continue;
				const int parent = zones[i].parent;
				children[parent >= 0 ? static_cast<size_t>(parent) : zones.size()].push_back(static_cast<int>(i));
			}
			for (auto& list : children)
				std::sort(list.begin(), list.end(), [&](int a, int b) { return zones[static_cast<size_t>(a)].order < zones[static_cast<size_t>(b)].order; });

			std::function<void(int, int, int)> visit = [&](int zone_index, int out_parent, int depth)
			{
				const Zone& z = zones[static_cast<size_t>(zone_index)];
				ZoneStats s;
				s.name = z.name ? z.name : "";
				s.depth = depth;
				s.parent = out_parent;
				fill(zone_index, s);
				const int me = static_cast<int>(out.size());
				out.push_back(std::move(s));
				const auto& kids = children[static_cast<size_t>(zone_index)];
				out[static_cast<size_t>(me)].has_children = !kids.empty();
				for (int child : kids)
					visit(child, me, depth + 1);
			};
			for (int root : children[zones.size()])
				visit(root, -1, 0);
		}
	}


	const char* Intern(std::string_view name)
	{
		std::lock_guard<std::mutex> lock(g_intern_mutex);
		auto it = g_interned.emplace(name).first;   // nodes are stable : c_str() stays valid
		return it->c_str();
	}


	void SetEnabled(bool enabled)
	{
		if (g_enabled.exchange(enabled) == enabled)
			return;
		g_restart.store(true);
	}

	bool IsEnabled()
	{
		return g_enabled.load(std::memory_order_relaxed);
	}

	void SetDetailed(bool detailed)
	{
		g_detailed.store(detailed);
	}

	bool IsDetailed()
	{
		return g_detailed.load(std::memory_order_relaxed);
	}


	int BeginZone(const char* name)
	{
		if (!g_enabled.load(std::memory_order_relaxed) || !name || !OnMainThread())
			return 0;

		const int parent = g_stack.empty() ? -1 : g_stack.back().zone;
		const int zone = FindOrAddZone(parent, InternedName(name));
		g_next_serial = g_next_serial >= 0x3FFFFFFF ? 1 : g_next_serial + 1;
		g_stack.push_back({ zone, Clock::now(), 0.0, g_next_serial });
		return g_next_serial;
	}


	void EndZone(int token)
	{
		if (token <= 0 || !OnMainThread())
			return;

		// Not open any more : dropped (EndFrame, Reset, pause) or already closed.
		size_t index = g_stack.size();
		while (index > 0 && g_stack[index - 1].serial != token)
			--index;
		if (index == 0)
			return;

		// Zones opened inside and never closed (Profiler.begin without end)
		// end with their parent.
		const Clock::time_point now = Clock::now();
		while (g_stack.size() >= index)
			CloseTop(now);
	}


	void SetCounter(const char* name, double value)
	{
		if (!name || !g_enabled.load(std::memory_order_relaxed) || !OnMainThread())
			return;
		const char* key = InternedName(name);
		auto [it, added] = g_counters.try_emplace(key);
		if (added)
		{
			it->second.name = key;
			it->second.order = g_next_counter++;
		}
		it->second.value = value;
		it->second.per_frame = false;
	}

	void AddCount(const char* name, double amount)
	{
		if (!name || !g_enabled.load(std::memory_order_relaxed) || !OnMainThread())
			return;
		const char* key = InternedName(name);
		auto [it, added] = g_counters.try_emplace(key);
		if (added)
		{
			it->second.name = key;
			it->second.order = g_next_counter++;
		}
		it->second.frame_sum += amount;
		it->second.per_frame = true;
	}


	void SetSpikeThreshold(double ms)
	{
		g_spike_threshold.store(std::max(0.0, ms));
	}

	double GetSpikeThreshold()
	{
		return g_spike_threshold.load();
	}


	void EndFrame()
	{
		if (!g_main_known.load(std::memory_order_acquire))
		{
			g_main_thread = std::this_thread::get_id();
			g_main_known.store(true, std::memory_order_release);
		}

		bool reset = false;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			reset = g_reset_requested;
			g_reset_requested = false;
		}
		if (reset)
			ClearAll();

		// A zone left open across frames is dropped.
		g_stack.clear();

		if (!g_enabled.load(std::memory_order_relaxed))
		{
			g_has_last_frame = false;
			return;
		}
		if (g_restart.exchange(false))
		{
			g_has_last_frame = false;
			for (Zone& zone : g_zones)
			{
				zone.frame_ms = zone.frame_self_ms = 0.0;
				zone.frame_calls = 0;
			}
			for (auto& [key, counter] : g_counters)
				counter.frame_sum = 0.0;
		}

		const Clock::time_point now = Clock::now();
		if (!g_has_last_frame)
		{
			g_last_frame = now;
			g_has_last_frame = true;
			for (Zone& zone : g_zones)
			{
				zone.frame_ms = zone.frame_self_ms = 0.0;
				zone.frame_calls = 0;
			}
			return;
		}

		const double frame_ms = std::chrono::duration<double, std::milli>(now - g_last_frame).count();
		const double seconds = frame_ms / 1000.0;
		g_last_frame = now;

		const double threshold = g_spike_threshold.load();
		const bool spike = threshold > 0.0 && frame_ms >= threshold;
		std::vector<SpikeEntry> spike_entries;

		for (size_t i = 0; i < g_zones.size(); ++i)
		{
			Zone& zone = g_zones[i];
			if (spike && zone.frame_calls > 0)
				spike_entries.push_back({ static_cast<int>(i), zone.frame_ms, zone.frame_self_ms, zone.frame_calls });

			zone.last_ms = zone.frame_ms;
			zone.last_self_ms = zone.frame_self_ms;
			zone.last_calls = zone.frame_calls;
			if (!zone.has_average && zone.frame_calls > 0)
			{
				zone.average_ms = zone.frame_ms;   // first measure : no slow start from 0
				zone.self_average_ms = zone.frame_self_ms;
				zone.has_average = true;
			}
			else
			{
				zone.average_ms += (zone.frame_ms - zone.average_ms) * kSmoothing;
				zone.self_average_ms += (zone.frame_self_ms - zone.self_average_ms) * kSmoothing;
			}
			zone.max_age += seconds;
			if (zone.frame_ms >= zone.max_ms || zone.max_age > kMaxWindowSeconds)
			{
				zone.max_ms = zone.frame_ms;
				zone.max_age = 0.0;
			}
			zone.frame_ms = zone.frame_self_ms = 0.0;
			zone.frame_calls = 0;
		}

		std::vector<CounterSlot> counters;
		counters.reserve(g_counters.size());
		for (auto& [key, counter] : g_counters)
		{
			if (counter.per_frame)
			{
				counter.value = counter.frame_sum;
				counter.frame_sum = 0.0;
			}
			counters.push_back(counter);
		}
		std::sort(counters.begin(), counters.end(), [](const CounterSlot& a, const CounterSlot& b) { return a.order < b.order; });

		std::lock_guard<std::mutex> lock(g_mutex);
		g_pub_zones = g_zones;
		g_pub_counters = std::move(counters);
		g_frames.push_back(static_cast<float>(frame_ms));
		while (g_frames.size() > kFrameHistory)
			g_frames.pop_front();
		g_average_frame = g_average_frame <= 0.0 ? frame_ms : g_average_frame + (frame_ms - g_average_frame) * kSmoothing;

		if (spike)
		{
			g_spike = std::move(spike_entries);
			g_spike_zones = g_zones;
			g_spike_ms = frame_ms;
			g_spike_time = now;
			g_has_spike = true;
			++g_spike_count;
		}
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

		const auto& zones = g_pub_zones;
		BuildTree(zones,
		          [](int) { return true; },
		          [&](int i, ZoneStats& s)
		          {
			          const Zone& z = zones[static_cast<size_t>(i)];
			          s.last_ms = z.last_ms;
			          s.average_ms = z.average_ms;
			          s.max_ms = z.max_ms;
			          s.self_last_ms = z.last_self_ms;
			          s.self_average_ms = z.self_average_ms;
			          s.calls = z.last_calls;
		          },
		          out.zones);

		out.counters.clear();
		for (const CounterSlot& c : g_pub_counters)
			out.counters.push_back({ c.name ? c.name : "", c.value, c.per_frame });

		out.spike.valid = g_has_spike;
		out.spike.count = g_spike_count;
		out.spike.frame_ms = g_spike_ms;
		out.spike.seconds_ago = g_has_spike ? std::chrono::duration<double>(Clock::now() - g_spike_time).count() : 0.0;
		out.spike.zones.clear();
		if (g_has_spike)
		{
			std::unordered_map<int, const SpikeEntry*> in_spike;
			for (const SpikeEntry& e : g_spike)
				in_spike[e.zone] = &e;
			const auto& szones = g_spike_zones;
			BuildTree(szones,
			          [&](int i) { return in_spike.count(i) > 0; },
			          [&](int i, ZoneStats& s)
			          {
				          const SpikeEntry& e = *in_spike[i];
				          s.last_ms = s.average_ms = s.max_ms = e.ms;
				          s.self_last_ms = s.self_average_ms = e.self_ms;
				          s.calls = e.calls;
			          },
			          out.spike.zones);
		}
	}


	void Reset()
	{
		// The zones belong to the main thread : cleared now from it, else at
		// its next EndFrame.
		const bool main = OnMainThread();
		if (main)
			ClearAll();

		std::lock_guard<std::mutex> lock(g_mutex);
		g_reset_requested = !main;
		g_pub_zones.clear();
		g_pub_counters.clear();
		g_frames.clear();
		g_average_frame = 0.0;
		g_spike.clear();
		g_spike_zones.clear();
		g_has_spike = false;
		g_spike_count = 0;
		g_spike_ms = 0.0;
	}
}
