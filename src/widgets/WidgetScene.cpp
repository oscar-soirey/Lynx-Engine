#include "WidgetScene.h"

#include <hrl/hrl.h>
#include "WidgetContainers.h"

namespace lynx
{
	void WidgetsScene::Add(WidgetContainer& w)
	{
		containers_.push_back(&w);
	}

	void WidgetsScene::Delete()
	{
		for (auto& c: containers_)
		{
			c->ClearChildren();
		}
		containers_.clear();

		for (auto& w: pure_widgets_)
		{
			HRL_DeleteWidget(w);
		}
		pure_widgets_.clear();
	}

	bool WidgetsScene::Empty() const
	{
		return pure_widgets_.size() == 0 || containers_.size() == 0;
	}
}
