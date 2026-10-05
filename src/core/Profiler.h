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
//     time per zone), main thread only, nothing to install.
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
//     }
//
// The host (editor, runtime) calls LYNX_PROFILE_FRAME() once per frame.
// Names must be string literals (Tracy keeps the pointer).
// =============================================================================

#include <cstdint>
#include <string>
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

	// Zones of the built-in profiler (use the macros instead).
	LYNX_API bool BeginZone(const char* name);
	LYNX_API void EndZone();
	LYNX_API void EndFrame();

	// true when Tracy is compiled in ; connected : a Tracy profiler is recording.
	LYNX_API bool TracyAvailable();
	LYNX_API bool TracyConnected();

	struct ZoneStats
	{
		std::string name;
		int depth = 0;              // nesting level the first time it was seen
		double last_ms = 0.0;       // inclusive time during the last frame
		double average_ms = 0.0;    // smoothed over ~1 s
		double max_ms = 0.0;        // worst frame of the last ~2 s
		int calls = 0;              // calls during the last frame
	};

	struct Snapshot
	{
		std::vector<float> frame_ms;    // oldest first
		double average_frame_ms = 0.0;
		double max_frame_ms = 0.0;
		std::vector<ZoneStats> zones;   // in order of first appearance in a frame
	};

	LYNX_API void GetSnapshot(Snapshot& out);
	LYNX_API void Reset();

	// RAII zone of the built-in profiler.
	class Scope
	{
	public:
		explicit Scope(const char* name) : active_(BeginZone(name)) {}
		~Scope() { if (active_) EndZone(); }
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		bool active_;
	};
}

#define LYNX_PROFILE_CONCAT_INNER(a, b) a##b
#define LYNX_PROFILE_CONCAT(a, b) LYNX_PROFILE_CONCAT_INNER(a, b)

#ifdef TRACY_ENABLE
	#define LYNX_PROFILE_SCOPE(name) \
		ZoneScopedN(name); \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(name)

	#define LYNX_PROFILE_FUNCTION() \
		ZoneScoped; \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(__func__)

	#define LYNX_PROFILE_FRAME() \
		do { FrameMark; ::lynx::profiler::EndFrame(); } while (false)

	// Name of the current thread in Tracy.
	#define LYNX_PROFILE_THREAD(name) tracy::SetThreadName(name)

	// Value plotted over time in Tracy (fps, actor count...).
	#define LYNX_PROFILE_PLOT(name, value) TracyPlot(name, value)
#else
	#define LYNX_PROFILE_SCOPE(name) \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(name)

	#define LYNX_PROFILE_FUNCTION() \
		::lynx::profiler::Scope LYNX_PROFILE_CONCAT(lynx_profile_scope_, __LINE__)(__func__)

	#define LYNX_PROFILE_FRAME() ::lynx::profiler::EndFrame()
	#define LYNX_PROFILE_THREAD(name) ((void)0)
	#define LYNX_PROFILE_PLOT(name, value) ((void)0)
#endif
