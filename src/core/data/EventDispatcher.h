#ifndef HGE_EVENT_DISPATCHER_H
#define HGE_EVENT_DISPATCHER_H

#include <unordered_map>
#include <functional>

namespace lynx
{
	template<typename ... Args>
	class HEventDispatcher {
	public:
		/**
		 * @param a function to register
		 * @return Id of the listener in the event dispatcher
		 */
		int Subscribe(std::function<void(Args...)> a)
		{
			listeners_.emplace(++internal_id_, a);
			return internal_id_;
		}

		/**
		 * If the id is not valid, it does nothing
		 * @param id Id of the function in the listener
		 */
		void Unsubscribe(int id)
		{
			auto it = listeners_.find(id);
			if (it == listeners_.end())
			{
				return;
			}
			listeners_.erase(it);
		}

		void Call(Args... args)
		{
			for (const auto [id, fn] : listeners_)
			{
				fn(args...);
			}
		}

	private:
		std::unordered_map<int, std::function<void(Args...)>> listeners_={};
		int internal_id_=0;
	};
}

#endif