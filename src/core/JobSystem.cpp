#include "JobSystem.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "Profiler.h"

namespace lynx::jobs
{
	namespace detail
	{
		struct JobState
		{
			std::atomic<bool> done{ false };
			std::mutex mutex;
			std::condition_variable cv;

			void Finish()
			{
				{
					std::lock_guard<std::mutex> lock(mutex);
					done.store(true, std::memory_order_release);
				}
				cv.notify_all();
			}
		};
	}

	namespace
	{
		using Task = std::function<void()>;

		std::vector<std::thread> g_workers;
		std::mutex g_mutex;                 // g_queue, g_stop
		std::condition_variable g_cv;
		std::deque<Task> g_queue;
		bool g_stop = false;

		std::atomic<bool> g_initialized{ false };
		std::thread::id g_main_thread;
		thread_local bool t_worker = false;

		std::mutex g_main_mutex;            // g_main_queue
		std::vector<Task> g_main_queue;

		void Report(const char* where, const std::exception_ptr& error)
		{
			try
			{
				if (error)
					std::rethrow_exception(error);
			}
			catch (const std::exception& e)
			{
				std::cerr << "[JOBS] exception in " << where << " : " << e.what() << "\n";
			}
			catch (...)
			{
				std::cerr << "[JOBS] unknown exception in " << where << "\n";
			}
		}

		void RunTask(Task& task)
		{
			try
			{
				task();
			}
			catch (...)
			{
				Report("a job", std::current_exception());
			}
		}

		// One queued job on the calling thread. false : the queue is empty.
		bool TryRunOne()
		{
			Task task;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				if (g_queue.empty())
					return false;
				task = std::move(g_queue.front());
				g_queue.pop_front();
			}
			RunTask(task);
			return true;
		}

		void WorkerMain(int index)
		{
			t_worker = true;

			// Tracy copies the name ; kept static anyway (some backends do not).
			static char names[64][32];
			std::snprintf(names[index % 64], sizeof(names[0]), "Lynx Worker %d", index + 1);
			LYNX_PROFILE_THREAD(names[index % 64]);
			(void)names;

			for (;;)
			{
				Task task;
				{
					std::unique_lock<std::mutex> lock(g_mutex);
					g_cv.wait(lock, [] { return g_stop || !g_queue.empty(); });
					// Stop : the queue is finished first (pending file writes...).
					if (g_queue.empty())
						return;
					task = std::move(g_queue.front());
					g_queue.pop_front();
				}
				RunTask(task);
			}
		}

		int AutomaticWorkerCount()
		{
			if (const char* env = std::getenv("LYNX_JOB_THREADS"))
			{
				char* end = nullptr;
				const long value = std::strtol(env, &end, 10);
				if (end != env && value >= 0)
					return static_cast<int>(std::min<long>(value, 63));
			}
			const unsigned cores = std::thread::hardware_concurrency();
			if (cores <= 1)
				return 0;
			// The main thread keeps its core (HRL / OpenGL, JS, gameplay).
			return static_cast<int>(std::min(cores - 1u, 31u));
		}

		// Shared by the threads of one ParallelForRange.
		struct Batch
		{
			const std::function<void(int, int)>* fn = nullptr;
			int begin = 0, end = 0, grain = 1, chunks = 0;
			std::atomic<int> next{ 0 };
			std::atomic<int> done{ 0 };
			std::mutex mutex;
			std::condition_variable cv;
			std::exception_ptr error;       // the first one, rethrown by the caller

			void Work()
			{
				for (;;)
				{
					const int chunk = next.fetch_add(1, std::memory_order_relaxed);
					if (chunk >= chunks)
						return;
					const int first = begin + chunk * grain;
					const int last = std::min(end, first + grain);
					try
					{
						(*fn)(first, last);
					}
					catch (...)
					{
						std::lock_guard<std::mutex> lock(mutex);
						if (!error)
							error = std::current_exception();
					}
					if (done.fetch_add(1, std::memory_order_acq_rel) + 1 == chunks)
					{
						std::lock_guard<std::mutex> lock(mutex);
						cv.notify_all();
					}
				}
			}
		};
	}

	namespace detail
	{
		bool IsDone(const JobState& state)
		{
			return state.done.load(std::memory_order_acquire);
		}

		void Wait(JobState& state)
		{
			while (!state.done.load(std::memory_order_acquire))
			{
				// Helps instead of sleeping (also : no dead lock when a job
				// waits for a job queued behind it).
				if (TryRunOne())
					continue;
				// Queue empty : the job is running on another thread (it was
				// queued before this call), it will call Finish().
				std::unique_lock<std::mutex> lock(state.mutex);
				state.cv.wait(lock, [&state] { return state.done.load(std::memory_order_acquire); });
			}
		}
	}

	void Init(int workers)
	{
		if (g_initialized.load(std::memory_order_acquire))
			return;

		const int count = workers < 0 ? AutomaticWorkerCount() : std::min(workers, 63);

		g_main_thread = std::this_thread::get_id();
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_stop = false;
		}
		g_workers.reserve(static_cast<size_t>(count));
		for (int i = 0; i < count; ++i)
			g_workers.emplace_back(WorkerMain, i);

		g_initialized.store(true, std::memory_order_release);
		std::cout << "[JOBS] " << count << " worker thread(s)\n";
	}

	void Shutdown()
	{
		if (!g_initialized.load(std::memory_order_acquire))
			return;

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_stop = true;
		}
		g_cv.notify_all();
		for (std::thread& t : g_workers)
			if (t.joinable())
				t.join();
		g_workers.clear();

		// No worker : what is left runs here (nothing is lost).
		while (TryRunOne())
			;

		{
			std::lock_guard<std::mutex> lock(g_main_mutex);
			g_main_queue.clear();
		}
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_stop = false;
		}
		g_initialized.store(false, std::memory_order_release);
	}

	bool IsInitialized()
	{
		return g_initialized.load(std::memory_order_acquire);
	}

	int GetWorkerCount()
	{
		return static_cast<int>(g_workers.size());
	}

	bool IsMainThread()
	{
		if (!g_initialized.load(std::memory_order_acquire))
			return !t_worker;
		return std::this_thread::get_id() == g_main_thread;
	}

	bool IsWorkerThread()
	{
		return t_worker;
	}

	Handle Submit(std::function<void()> job)
	{
		auto state = std::make_shared<detail::JobState>();
		if (!job)
		{
			state->Finish();
			return Handle(state);
		}

		if (g_workers.empty())
		{
			RunTask(job);
			state->Finish();
			return Handle(state);
		}

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_queue.emplace_back([job = std::move(job), state]() mutable
			{
				try
				{
					job();
				}
				catch (...)
				{
					Report("a job", std::current_exception());
				}
				state->Finish();
			});
		}
		g_cv.notify_one();
		return Handle(state);
	}

	void ParallelForRange(int begin, int end, int grain, const std::function<void(int, int)>& fn)
	{
		if (end <= begin || !fn)
			return;
		grain = std::max(1, grain);
		const long long count = static_cast<long long>(end) - begin;
		const int chunks = static_cast<int>((count + grain - 1) / grain);

		if (chunks == 1 || g_workers.empty())
		{
			fn(begin, end);
			return;
		}

		auto batch = std::make_shared<Batch>();
		batch->fn = &fn;
		batch->begin = begin;
		batch->end = end;
		batch->grain = grain;
		batch->chunks = chunks;

		// Helpers : they take slices until there is none left. One that starts
		// late (workers busy) finds nothing and leaves : `fn` is never called
		// after this function returns.
		const int helpers = std::min(static_cast<int>(g_workers.size()), chunks - 1);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			for (int i = 0; i < helpers; ++i)
				// Front : a parallel loop waits on them, a file write can wait.
				g_queue.emplace_front([batch]() { batch->Work(); });
		}
		if (helpers == 1)
			g_cv.notify_one();
		else
			g_cv.notify_all();

		batch->Work();

		{
			std::unique_lock<std::mutex> lock(batch->mutex);
			batch->cv.wait(lock, [&] { return batch->done.load(std::memory_order_acquire) >= chunks; });
		}

		if (batch->error)
			std::rethrow_exception(batch->error);
	}

	void RunOnMainThread(std::function<void()> fn)
	{
		if (!fn)
			return;
		std::lock_guard<std::mutex> lock(g_main_mutex);
		g_main_queue.push_back(std::move(fn));
	}

	void PumpMainThread()
	{
		std::vector<Task> tasks;
		{
			std::lock_guard<std::mutex> lock(g_main_mutex);
			if (g_main_queue.empty())
				return;
			tasks.swap(g_main_queue);
		}
		LYNX_PROFILE_SCOPE("Jobs : main thread callbacks");
		for (Task& task : tasks)
			RunTask(task);
	}
}
