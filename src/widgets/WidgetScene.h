#pragma once

#include "../core/Common.h"
#include <vector>
#include <cstdint>

namespace lynx
{
	class WidgetContainer;

	struct LYNX_API WidgetsScene {
		void Add(uint32_t w)
		{
			pure_widgets_.push_back(w);
		}
		void Add(WidgetContainer& w);

		void Delete();

		uint32_t Get(int index)
		{
			return pure_widgets_.at(index);
		}

		bool Empty() const;

	private:
		std::vector<uint32_t> pure_widgets_;
		std::vector<WidgetContainer*> containers_;
	};
}
