#pragma once

// =============================================================================
// Profiler
// -----------------------------------------------------------------------------
// One macro feeds two profilers :
//
//   - Tracy (https://github.com/wolfpld/tracy) : the client is compiled in
//     lynx.dll, "on demand" (TRACY_ON_DEMAND) : nothing is recorded until the
//     Tracy profiler (tracy-profiler.exe) connects. CMake option
//     LYNX_PROFILER_TRACY (ON by default).
//   - The built-in profiler : the "Profiler" window of the editor (frame times,
//     zone tree with inclusive and self times, hot spots, frame spikes,
//     counters), main thread only, nothing to install.
//
// Usage, in the engine, the editor or a game :
//
//     void Enemy::Tick(double dt)
//     {
//         LYNX_PROFILE_FUNCTION();              // zone named after the function
//         ...
//         {
//             LYNX_PROFILE_SCOPE("Pathfinding"); // a string literal
//             ...
//         }
//         // A name built at run time (class name...) : only computed while a
//         // profiler records, then interned (a hash per call : not for the
//         // hottest loops, see LYNX_PROFILE_SCOPE_PTR with a cached name).
//         LYNX_PROFILE_SCOPE_DYNAMIC(GetTypeName() + ".Think");
//     }
//
//     LYNX_PROFILE_COUNTER("Enemies", count);   // value shown in the window
//     LYNX_PROFILE_COUNT("Raycasts", 1);        // summed over each frame
//
// The host (editor, runtime) calls LYNX_PROFILE_FRAME() once per frame.
// Zones are a tree : the same name under two parents gives two rows (the
// "Hot spots" view of the window merges them by name).
// From JavaScript : Profiler.begin("name") / Profiler.end(), Profiler.scope("name", fn).
// =============================================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "Common.h"

#ifdef TRACY_ENABLE
	#include <tracy/Tracy.hpp>
#endif

namespace lynx::profiler
{
	// The built-in profiler records only when enabled (the editor enables it
	// while its Profiler window is open). Tracy has its own switch : the
	// connection of the Tracy profiler.
	LYNX_API void SetEnabled(bool enabled);
	LYNX_API bool IsEnabled();

	// true when Tracy is compiled in ; connected : a Tracy profiler is recording.
	LYNX_API bool TracyAvailable();
	LYNX_API bool TracyConnected();

	// One of the profilers records : worth computing a dynamic zone name.
	inline bool Active() { return IsEnabled() || TracyConnected(); }

	// Fine zones (one per component type, per JS call...) : a little cost per
	// zone while recording. On by default ; the window has a checkbox.
	LYNX_API void SetDetailed(bool detailed);
	LYNX_API bool IsDetailed();
	inline bool Detailed() { return IsDetailed() && Active(); }

	// A stable copy of a name (kept until the end of the program). The same
	// text always gives the same pointer.
	LYNX_API const char* Intern(std::string_view name);
	inline const char* Intern(const std::string& name) { return Intern(std::string_view(name)); }
	inline const char* Intern(const char* name) { return name ? Intern(std::string_view(name)) : nullptr; }

	// Zones of the built-in profiler (use the macros instead). BeginZone
	// returns a token (0 : not recorded) given back to EndZone : a zone left
	// open inside (a JS Profiler.begin without end) is closed with it.
	LYNX_API int BeginZone(const char* name);
	LYNX_API void EndZone(int token);
	LYNX_API void EndFrame();

	// Counters shown in the window. SetCounter : a value (actors...).
	// AddCount : added up during each frame, then shown (calls, raycasts...).
	LYNX_API void SetCounter(const char* name, double value);
	LYNX_API void AddCount(const char* name, double amount = 1.0);

	// A frame slower than this (ms) is a spike : the zones of that frame are
	// kept (window "Last spike"). 0 : no capture.
	LYNX_API void SetSpikeThreshold(double ms);
	LYNX_API double GetSpikeThreshold();

	struct ZoneStats
	{
		std::string name;
		int depth = 0;              // 0 : a root zone
		int parent = -1;            // index in the same vector, -1 : root
		double last_ms = 0.0;       // inclusive time during the last frame
		double average_ms = 0.0;    // smoothed over ~1 s
		double max_ms = 0.0;        // worst frame of the last ~2 s
		double self_last_ms = 0.0;  // last frame, without the child zones
		double self_average_ms = 0.0;
		int calls = 0;              // calls during the last frame
		bool has_children = false;
	};

	struct Counter
	{
		std::string name;
		double value = 0.0;
		bool per_frame = false;     // AddCount (sum of the last frame)
	};

	struct Spike
	{
		bool valid = false;
		double frame_ms = 0.0;
		double seconds_ago = 0.0;
		int count = 0;              // spikes since the last Reset
		std::vector<ZoneStats> zones;   // tree order ; last_ms = time in that frame
	};

	struct Snapshot
	{
		std::vector<float> frame_ms;    // oldest first
		double average_frame_ms = 0.0;
		double max_frame_ms = 0.0;
		std::vector<ZoneStats> zones;   // tree order : a parent, then its children
		std::vector<Counter> counters;
		Spike spike;
	};

	LYNX_API void GetSnapshot(Snapshot& out);
	LYNX_API void Reset();

	// RAII zone of the built-in profiler (name : a literal, or interned).
	class Scope
	{
	public:
		explicit Scope(const char* name) : token_(name ? BeginZone(name) : 0) {}
		~Scope() { if (token_) EndZone(token_); }
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		int token_;
	};
}

#define LYNX_PROFILE_CONCAT_INNER(a, b) a##b
#define LYNX_PROFILE_CONCAT(a, b) LYNX_PROFILE_CONCAT_INNER(a, b)

#ifdef TRACY_ENABLE
	// A zone named by a stable pointer computed by the caller (nullptr : no zone).
	// Tracy zones get a variable named after the line (ZoneNamedN), not the
	// shared ___tracy_scoped_zone of ZoneScopedN : several zones can live in
	// the same block (one macro per line).
	#define LYNX_PROFILE_SCOPE_PTR(name_ptr) \
		const char* LYNX_PROFILE_CONCAT(lynx_profile_name_, __LINE__) = (name_ptr); \
		ZoneNamedN(LYNX_PROFILE_CONCAT(lynx_tracy_zone_, __LINE__), "Lynx", true); \
		if (LYNX_PROFILE_CONCAT(lynx_profile_name_, __LINE__)) \
			ZoneNameV(LYNX_PROFILE_CONCAT(lynx_tracy_zone_, __LINE__), LYNX_PROFILE_CONCAT(lynx_profile_name_, __LINE__), \
			          std::strlen(LYNX_PROFILE_CONCAT(lynx_profile_name_, __LINE__))); \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(LYNX_PROFILE_CONCAT(lynx_profile_name_, __LINE__))

	#define LYNX_PROFILE_SCOPE(name) \
		ZoneNamedN(LYNX_PROFILE_CONCAT(lynx_tracy_zone_, __LINE__), name, true); \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(name)

	#define LYNX_PROFILE_FUNCTION() \
		ZoneNamed(LYNX_PROFILE_CONCAT(lynx_tracy_zone_, __LINE__), true); \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(__func__)

	#define LYNX_PROFILE_SCOPE_DYNAMIC(name_expr) \
		LYNX_PROFILE_SCOPE_PTR(::lynx::profiler::Active() ? ::lynx::profiler::Intern(name_expr) : nullptr)

	#define LYNX_PROFILE_FRAME() \
		do { FrameMark; ::lynx::profiler::EndFrame(); } while (false)

	// Name of the current thread in Tracy.
	#define LYNX_PROFILE_THREAD(name) tracy::SetThreadName(name)

	// Value plotted over time in Tracy (fps, actor count...) and shown in the window.
	#define LYNX_PROFILE_PLOT(name, value) \
		do { TracyPlot(name, value); ::lynx::profiler::SetCounter(name, static_cast<double>(value)); } while (false)
#else
	#define LYNX_PROFILE_SCOPE_PTR(name_ptr) \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(name_ptr)

	#define LYNX_PROFILE_SCOPE(name) \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(name)

	#define LYNX_PROFILE_FUNCTION() \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(__func__)

	#define LYNX_PROFILE_SCOPE_DYNAMIC(name_expr) \
		LYNX_PROFILE_SCOPE_PTR(::lynx::profiler::Active() ? ::lynx::profiler::Intern(name_expr) : nullptr)

	#define LYNX_PROFILE_FRAME() ::lynx::profiler::EndFrame()
	#define LYNX_PROFILE_THREAD(name) ((void)0)
	#define LYNX_PROFILE_PLOT(name, value) ::lynx::profiler::SetCounter(name, static_cast<double>(value))
#endif

#define LYNX_PROFILE_COUNTER(name, value) ::lynx::profiler::SetCounter(name, static_cast<double>(value))
#define LYNX_PROFILE_COUNT(name, amount) ::lynx::profiler::AddCount(name, static_cast<double>(amount))
