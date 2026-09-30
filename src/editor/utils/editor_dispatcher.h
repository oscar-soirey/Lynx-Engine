#pragma once

#include <unordered_map>
#include <functional>

#include "../common.h"

namespace hn::editor
{
	template<typename ... Args>
	class EDITOR_API EditorDispatcher {
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

		void Call(Args... args) const
		{
			for (const auto [id, fn] : listeners_)
			{
				fn(args...);
			}
		}

		void SetUserData(void* data)
		{
			user_data_ = data;
		}

		void* GetUserData()
		{
			return user_data_;
		}

	private:
		std::unordered_map<int, std::function<void(Args...)>> listeners_={};
		int internal_id_=0;
		void* user_data_=nullptr;
	};
}