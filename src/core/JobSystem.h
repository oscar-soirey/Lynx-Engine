#pragma once

// =============================================================================
// Job system (worker threads)
// -----------------------------------------------------------------------------
// A pool of worker threads, created by Engine::Create() (one per core, minus
// the main thread). The engine uses it for the heavy loops that work on plain
// data (voxel physics, texture files...), the editor for its disk I/O, and
// the games can use it too (C++).
//
// THREADING RULES OF THE ENGINE
//   Main thread only (the thread that created the engine and owns the OpenGL
//   context) :
//     - every HRL_* call (OpenGL context : textures, meshes, voxels, lights...)
//     - JavaScript (one QuickJS runtime), actors, components, the level, ECS
//     - audio, widgets, ImGui, plugins
//   A job reads / writes ONLY data it was given (copies, buffers, its own
//   slice of an array). Results that need HRL go back to the main thread :
//   lynx::jobs::RunOnMainThread(...).
//
// Usage :
//
//     // Blocking parallel loop (the calling thread works too) :
//     lynx::jobs::ParallelFor(0, count, 64, [&](int i) { out[i] = Heavy(in[i]); });
//
//     // Fire and forget / wait later :
//     lynx::jobs::Handle h = lynx::jobs::Submit([data]() { WriteFile(data); });
//     ...
//     h.Wait();            // or h.IsDone() each frame
//
//     // From a job : back to the main thread (start of the next frame) :
//     lynx::jobs::RunOnMainThread([pixels]() { HRL_CreateTexture(...); });
//
// LYNX_JOB_THREADS (environment variable) : number of workers (0 : no worker,
// every job runs on the calling thread : handy to rule out a threading bug).
// =============================================================================

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "Common.h"

namespace lynx::jobs
{
	namespace detail
	{
		struct JobState;
		LYNX_API bool IsDone(const JobState& state);
		LYNX_API void Wait(JobState& state);
	}

	/**
	 * Starts the workers (Engine::Create does it). `workers` < 0 : automatic
	 * (cores - 1, LYNX_JOB_THREADS overrides it), 0 : no worker. The calling
	 * thread becomes the main thread. A second call does nothing.
	 */
	LYNX_API void Init(int workers = -1);

	/** Finishes the queued jobs, then stops the workers (Engine::Destroy). */
	LYNX_API void Shutdown();

	LYNX_API bool IsInitialized();

	/** Worker threads (0 : the jobs run on the calling thread). */
	LYNX_API int GetWorkerCount();

	/** The thread that owns the engine and the OpenGL context. */
	LYNX_API bool IsMainThread();
	LYNX_API bool IsWorkerThread();

	/** A submitted job : done or not, wait for it. Copyable, empty by default. */
	class Handle
	{
	public:
		Handle() = default;
		explicit Handle(std::shared_ptr<detail::JobState> state) : state_(std::move(state)) {}

		bool IsValid() const { return state_ != nullptr; }
		/** true for an empty handle. */
		bool IsDone() const { return !state_ || detail::IsDone(*state_); }
		/** Blocks until the job is done (runs other queued jobs meanwhile). */
		void Wait() const { if (state_) detail::Wait(*state_); }
		void Reset() { state_.reset(); }

	private:
		std::shared_ptr<detail::JobState> state_;
	};

	/**
	 * Runs `job` on a worker (on the calling thread right away when there is
	 * no worker). Never call HRL / JavaScript / the level from it.
	 */
	LYNX_API Handle Submit(std::function<void()> job);

	/**
	 * Calls fn(first, last) on slices of [begin, end) of at most `grain`
	 * indices, on the workers AND the calling thread ; returns when every
	 * slice is done. Can be nested (a job may call it).
	 */
	LYNX_API void ParallelForRange(int begin, int end, int grain, const std::function<void(int, int)>& fn);

	/** fn(i) for each i of [begin, end) (see ParallelForRange). */
	template <typename F>
	void ParallelFor(int begin, int end, int grain, F&& fn)
	{
		ParallelForRange(begin, end, grain, [&fn](int first, int last)
		{
			for (int i = first; i < last; ++i)
				fn(i);
		});
	}

	/**
	 * `fn` runs on the main thread, at the start of the next frame
	 * (Engine::ProgressOneFrame calls PumpMainThread). Any thread.
	 */
	LYNX_API void RunOnMainThread(std::function<void()> fn);

	/** Runs what RunOnMainThread queued. Main thread (the engine does it). */
	LYNX_API void PumpMainThread();
}

// Debug builds : stops at once when HRL / JS / the level is used from a job.
#ifndef NDEBUG
	#include <cassert>
	#define LYNX_ASSERT_MAIN_THREAD() \
		assert((!::lynx::jobs::IsInitialized() || ::lynx::jobs::IsMainThread()) && "main thread only (HRL / OpenGL, JS, level)")
#else
	#define LYNX_ASSERT_MAIN_THREAD() ((void)0)
#endif
