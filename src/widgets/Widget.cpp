#include "Widget.h"

#include "Panels.h"
#include "CommonWidgets.h"
#include "UserWidget.h"
#include "Private/WidgetSystem.h"

#include "../core/Engine.h"
#include "../core/Factory.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

namespace lynx
{
	// =========================================================================
	// PanelSlot
	// =========================================================================

	PanelSlot::PanelSlot() = default;
	PanelSlot::~PanelSlot() = default;

	void PanelSlot::Invalidate()
	{
		if (content_)
			content_->Invalidate();
		else if (parent_)
			parent_->Invalidate();
	}


	// =========================================================================
	// Widget
	// =========================================================================

	Widget::Widget()
	{
		HPROPERTY(visibility, Exposed, Invalidate());
		HPROPERTY(is_enabled, Exposed, Invalidate());
		HPROPERTY(render_opacity, Exposed, Invalidate());

		serial_ = WidgetSystem::Register(this);
	}

	Widget::~Widget()
	{
		WidgetSystem::DestroyNative(*this);
		WidgetSystem::Unregister(this);
	}

	bool Widget::IsVisible() const
	{
		for (const Widget* w = this; w; )
		{
			const EVisibility v = w->GetVisibility();
			if (v == EVisibility::Collapsed || v == EVisibility::Hidden)
				return false;

			if (w->slot_)
				w = w->slot_->GetParent();
			else
				w = w->root_of_;
		}
		return true;
	}

	UserWidget* Widget::GetUserWidget() const
	{
		const Widget* w = this;
		while (w)
		{
			if (w->root_of_)
				return w->root_of_;
			w = w->slot_ ? w->slot_->GetParent() : nullptr;
		}
		return nullptr;
	}

	void Widget::RemoveFromParent()
	{
		if (PanelWidget* parent = GetParent())
		{
			parent->RemoveChild(this);    // deletes this widget
			return;
		}

		if (root_of_)
			root_of_->TakeRoot();         // deletes this widget
	}

	vec2 Widget::GetDesiredSize() const
	{
		if (GetVisibility() == EVisibility::Collapsed)
			return { 0.f, 0.f };

		desired_ = ComputeDesiredSize();
		return desired_;
	}

	void Widget::Invalidate()
	{
		if (UserWidget* top = WidgetSystem::GetTop(this))
			top->dirty_ = true;
	}

	void Widget::ForEachWidget(const std::function<void(Widget&)>& fn)
	{
		fn(*this);
	}

	float Widget::GetDPIScale() const
	{
		return WidgetSystem::GetScale(WidgetSystem::GetTop(this));
	}

	void Widget::Arrange(const WidgetRect& rect)
	{
		geometry_ = rect;

		if (GetVisibility() == EVisibility::Collapsed)
		{
			// Children : nowhere (no stale rectangle in the Widget Editor).
			ArrangeChildren(WidgetRect{ rect.x, rect.y, 0.f, 0.f });
			return;
		}

		ArrangeChildren(rect);
	}


	// =========================================================================
	// PanelWidget
	// =========================================================================

	PanelWidget::PanelWidget() = default;

	PanelWidget::~PanelWidget()
	{
		ClearChildren();
	}

	std::unique_ptr<PanelSlot> PanelWidget::CreateSlot() const
	{
		return std::make_unique<PanelSlot>();
	}

	bool PanelWidget::CanAddChild() const
	{
		const int max = GetMaxChildren();
		return max < 0 || static_cast<int>(children_.size()) < max;
	}

	PanelSlot* PanelWidget::AddChild(std::unique_ptr<Widget> child)
	{
		return InsertChildAt(static_cast<int>(children_.size()), std::move(child));
	}

	PanelSlot* PanelWidget::InsertChildAt(int index, std::unique_ptr<Widget> child)
	{
		if (!child || !CanAddChild())
			return nullptr;

		Child entry;
		entry.slot = CreateSlot();
		entry.slot->content_ = child.get();
		entry.slot->parent_ = this;
		child->slot_ = entry.slot.get();
		child->root_of_ = nullptr;
		entry.widget = std::move(child);

		index = std::clamp(index, 0, static_cast<int>(children_.size()));
		PanelSlot* slot = entry.slot.get();
		children_.insert(children_.begin() + index, std::move(entry));

		// Natives : created by the next Refresh when the tree is on screen.
		Invalidate();
		return slot;
	}

	std::unique_ptr<Widget> PanelWidget::RemoveChild(Widget* child)
	{
		auto it = std::find_if(children_.begin(), children_.end(),
		                       [child](const Child& c) { return c.widget.get() == child; });
		if (it == children_.end())
			return nullptr;

		Invalidate();

		std::unique_ptr<Widget> owned = std::move(it->widget);
		WidgetSystem::DestroyNatives(*owned);
		owned->slot_ = nullptr;
		children_.erase(it);
		return owned;
	}

	void PanelWidget::ClearChildren()
	{
		// Widgets before their slots (a widget never outlives its slot pointer).
		while (!children_.empty())
		{
			Child child = std::move(children_.back());
			children_.pop_back();
			if (child.widget)
				child.widget->slot_ = nullptr;
			child.widget.reset();
		}
		Invalidate();
	}

	Widget* PanelWidget::GetChildAt(int index) const
	{
		if (index < 0 || index >= static_cast<int>(children_.size()))
			return nullptr;
		return children_[static_cast<size_t>(index)].widget.get();
	}

	int PanelWidget::GetChildIndex(const Widget* child) const
	{
		for (size_t i = 0; i < children_.size(); ++i)
		{
			if (children_[i].widget.get() == child)
				return static_cast<int>(i);
		}
		return -1;
	}

	void PanelWidget::ForEachWidget(const std::function<void(Widget&)>& fn)
	{
		fn(*this);

		// Copy : `fn` may not change the list, but a child may be a panel too.
		for (const Child& child : children_)
		{
			if (child.widget)
				child.widget->ForEachWidget(fn);
		}
	}


	// =========================================================================
	// Classes by name, enums, layout helpers
	// =========================================================================

	namespace ui
	{
		namespace
		{
			using Constructor = std::function<std::unique_ptr<Widget>()>;

			template<typename T>
			std::pair<std::string, Constructor> Entry(const char* name)
			{
				return { name, [] { return std::unique_ptr<Widget>(new T()); } };
			}

			const std::vector<std::pair<std::string, Constructor>>& Builtins()
			{
				static const std::vector<std::pair<std::string, Constructor>> builtins = {
					Entry<CanvasPanel>("CanvasPanel"),
					Entry<VerticalBox>("VerticalBox"),
					Entry<HorizontalBox>("HorizontalBox"),
					Entry<Overlay>("Overlay"),
					Entry<Border>("Border"),
					Entry<TextBlock>("TextBlock"),
					Entry<Image>("Image"),
					Entry<Button>("Button"),
					Entry<Slider>("Slider"),
					Entry<CheckBox>("CheckBox"),
					Entry<ProgressBar>("ProgressBar"),
					Entry<Spacer>("Spacer"),
					Entry<UserWidget>("UserWidget"),
				};
				return builtins;
			}

			float g_dpi_reference = 1080.f;
		}

		std::unique_ptr<Widget> NewWidget(const std::string& name)
		{
			for (const auto& [class_name, constructor] : Builtins())
			{
				if (class_name == name)
					return constructor();
			}

			// Game classes (LYNX_MODULE_REGISTER) deriving from lynx::Widget.
			if (Engine* engine = Engine::Get())
			{
				auto constructor = engine->GetFactory().GetObjectConstr(name.c_str());
				if (constructor.has_value() && constructor.value())
				{
					Object* object = constructor.value()();
					if (auto* widget = dynamic_cast<Widget*>(object))
						return std::unique_ptr<Widget>(widget);
					delete object;
				}
			}
			return nullptr;
		}

		const std::vector<std::string>& GetBuiltinWidgetClasses()
		{
			static const std::vector<std::string> names = [] {
				std::vector<std::string> out;
				for (const auto& entry : Builtins())
					out.push_back(entry.first);
				return out;
			}();
			return names;
		}

		const std::vector<std::string>& GetEnumNames(const std::string& property)
		{
			static const std::unordered_map<std::string, std::vector<std::string>> enums = {
				{ "visibility", { "Visible", "Collapsed", "Hidden", "Hit Test Invisible", "Self Hit Test Invisible" } },
				{ "h_align", { "Fill", "Left", "Center", "Right" } },
				{ "v_align", { "Fill", "Top", "Center", "Bottom" } },
				{ "size_rule", { "Auto", "Fill" } },
				{ "orientation", { "Horizontal", "Vertical" } },
			};
			static const std::vector<std::string> none;

			auto it = enums.find(property);
			return it == enums.end() ? none : it->second;
		}

		void SetDPIReference(float shortest_side)
		{
			if (shortest_side > 1.f)
				g_dpi_reference = shortest_side;

			for (UserWidget* widget : GetAllWidgets())
				widget->Invalidate();
		}

		float GetDPIReference()
		{
			return g_dpi_reference;
		}

		WidgetRect AlignInArea(const WidgetRect& area, vec2 desired, int h_align, int v_align)
		{
			WidgetRect r = area;

			switch (static_cast<EHAlign>(h_align))
			{
			case EHAlign::Left:   r.w = desired.x; break;
			case EHAlign::Center: r.w = desired.x; r.x = area.x + (area.w - desired.x) * 0.5f; break;
			case EHAlign::Right:  r.w = desired.x; r.x = area.x + area.w - desired.x; break;
			default: break;
			}

			switch (static_cast<EVAlign>(v_align))
			{
			case EVAlign::Top:    r.h = desired.y; break;
			case EVAlign::Center: r.h = desired.y; r.y = area.y + (area.h - desired.y) * 0.5f; break;
			case EVAlign::Bottom: r.h = desired.y; r.y = area.y + area.h - desired.y; break;
			default: break;
			}
			return r;
		}

		WidgetRect ApplyPadding(const WidgetRect& rect, const vec4& padding)
		{
			WidgetRect r;
			r.x = rect.x + padding.x;
			r.y = rect.y + padding.y;
			r.w = std::max(0.f, rect.w - padding.x - padding.z);
			r.h = std::max(0.f, rect.h - padding.y - padding.w);
			return r;
		}
	}
}
