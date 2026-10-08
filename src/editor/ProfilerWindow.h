#pragma once

// =============================================================================
// "Profiler" window (editor)
// -----------------------------------------------------------------------------
// - Built-in profiler (core/Profiler.h) : frame times (graph), time of every
//   LYNX_PROFILE_SCOPE zone of the main thread as a tree (average, self time,
//   last frame, max), hot spots by self time, the last frame spike, counters.
//   It records only while this window is open.
// - Tracy : connection state, and "Open Tracy" (tracy-profiler.exe next to the
//   editor, in the Lynx tools folder of the launcher, or in the PATH).
// =============================================================================

namespace lynx::editor::profiler_window
{
	void Draw(bool* open);
}
