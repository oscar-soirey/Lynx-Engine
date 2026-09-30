#ifndef HGE_ORDERED_MAP_H
#define HGE_ORDERED_MAP_H

#include <vector>
#include <stdexcept>
#include <algorithm>

namespace lynx
{
	template<typename F, typename S>
	struct ordered_map {
		ordered_map()=default;

		void emplace_back(F first, S second)
		{
			vec_.emplace_back(std::move(first), std::move(second));
		}

		S& operator[](const F& index)
		{
			auto it = std::find_if(vec_.begin(), vec_.end(),
					[&](const std::pair<F, S>& p) { return p.first == index; });

			if (it == vec_.end())
			{
				vec_.emplace_back(index, S{});
				return vec_.back().second;
			}

			return it->second;
		}

		const S& operator[](const F& index) const
		{
			auto it = std::find_if(vec_.begin(), vec_.end(),
					[&](const std::pair<F, S>& p) { return p.first == index; });

			if (it == vec_.end())
			{
				vec_.emplace_back(index, S{});
				return vec_.back().second;
			}

			return it->second;
		}

		void erase(const F& index)
		{
			auto it = find(index);
			if (it != vec_.end())
				vec_.erase(it);
		}

		bool contains(const F& index) const
		{
			return std::find_if(vec_.begin(), vec_.end(),
				[&](const std::pair<F, S>& p) { return p.first == index; }) != vec_.end();
		}

		auto find(const F& index)
		{
			return std::find_if(vec_.begin(), vec_.end(),
					[&](const std::pair<F, S>& p) { return p.first == index; });
		}

		auto find(const F& index) const
		{
			return std::find_if(vec_.begin(), vec_.end(),
					[&](const std::pair<F, S>& p) { return p.first == index; });
		}

		auto begin() { return vec_.begin(); }
		auto end()   { return vec_.end(); }
		auto begin() const { return vec_.begin(); }
		auto end()   const { return vec_.end(); }

		size_t size() const { return vec_.size(); }
		bool empty()  const { return vec_.empty(); }

	private:
		std::vector<std::pair<F, S>> vec_ = {};
	};
}

#endif