#pragma once

// =============================================================================
// "Profiler" window (editor)
// -----------------------------------------------------------------------------
// - Built-in profiler (core/Profiler.h) : frame times (graph), time of every
//   LYNX_PROFILE_SCOPE zone of the main thread (last frame, average, max).
//   It records only while this window is open.
// - Tracy : connection state, and "Open Tracy" (tracy-profiler.exe next to the
//   editor, in the Lynx tools folder of the launcher, or in the PATH).
// =============================================================================

namespace lynx::editor::profiler_window
{
	void Draw(bool* open);
}
