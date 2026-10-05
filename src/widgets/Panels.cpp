#include "Panels.h"

#include <algorithm>
#include <numeric>

namespace lynx
{
	// =========================================================================
	// CanvasPanel
	// =========================================================================

	CanvasPanelSlot::CanvasPanelSlot()
	{
		HPROPERTY(anchor_min, Exposed, Invalidate());
		HPROPERTY(anchor_max, Exposed, Invalidate());
		HPROPERTY(offsets, Exposed, Invalidate());
		HPROPERTY(alignment, Exposed, Invalidate());
		HPROPERTY(auto_size, Exposed, Invalidate());
		HPROPERTY(z_order, Exposed, Invalidate());
	}

	void CanvasPanelSlot::SetAnchors(vec2 min, vec2 max)
	{
		anchor_min = min;
		anchor_max = max;
		Invalidate();
	}

	void CanvasPanelSlot::SetPosition(vec2 position)
	{
		offsets.x = position.x;
		offsets.y = position.y;
		Invalidate();
	}

	void CanvasPanelSlot::SetSize(vec2 size)
	{
		offsets.z = size.x;
		offsets.w = size.y;
		Invalidate();
	}

	CanvasPanel::CanvasPanel() = default;

	CanvasPanelSlot* CanvasPanel::AddChildToCanvas(std::unique_ptr<Widget> child)
	{
		return static_cast<CanvasPanelSlot*>(AddChild(std::move(child)));
	}

	std::unique_ptr<PanelSlot> CanvasPanel::CreateSlot() const
	{
		return std::make_unique<CanvasPanelSlot>();
	}

	namespace
	{
		// One axis of a canvas slot : start and length in the panel.
		void CanvasAxis(float panel_start, float panel_size,
		                float anchor_min, float anchor_max,
		                float offset_start, float offset_end,
		                float alignment, bool auto_size, float desired,
		                float& out_start, float& out_size)
		{
			if (anchor_min == anchor_max)
			{
				// Point anchor : offset_start = position, offset_end = size.
				out_size = auto_size ? desired : offset_end;
				out_start = panel_start + anchor_min * panel_size + offset_start - alignment * out_size;
			}
			else
			{
				// Stretch : offsets are margins from the anchors.
				out_start = panel_start + anchor_min * panel_size + offset_start;
				out_size = std::max(0.f, (anchor_max - anchor_min) * panel_size - offset_start - offset_end);
			}
		}
	}

	vec2 CanvasPanel::ComputeDesiredSize() const
	{
		// Furthest point of the children placed from the top-left corner.
		vec2 size{ 0.f, 0.f };
		for (const Child& child : Children())
		{
			const auto* slot = static_cast<const CanvasPanelSlot*>(child.slot.get());
			const vec2 desired = child.widget->GetDesiredSize();

			const float w = slot->auto_size ? desired.x : slot->offsets.z;
			const float h = slot->auto_size ? desired.y : slot->offsets.w;

			if (slot->anchor_min.x == slot->anchor_max.x)
				size.x = std::max(size.x, slot->offsets.x - slot->alignment.x * w + w);
			if (slot->anchor_min.y == slot->anchor_max.y)
				size.y = std::max(size.y, slot->offsets.y - slot->alignment.y * h + h);
		}
		return size;
	}

	void CanvasPanel::ArrangeChildren(const WidgetRect& rect)
	{
		for (const Child& child : Children())
		{
			const auto* slot = static_cast<const CanvasPanelSlot*>(child.slot.get());
			const vec2 desired = slot->auto_size ? child.widget->GetDesiredSize() : vec2{ 0.f, 0.f };

			WidgetRect r;
			CanvasAxis(rect.x, rect.w, slot->anchor_min.x, slot->anchor_max.x,
			           slot->offsets.x, slot->offsets.z, slot->alignment.x, slot->auto_size, desired.x,
			           r.x, r.w);
			CanvasAxis(rect.y, rect.h, slot->anchor_min.y, slot->anchor_max.y,
			           slot->offsets.y, slot->offsets.w, slot->alignment.y, slot->auto_size, desired.y,
			           r.y, r.h);

			ArrangeChild(*child.widget, r);
		}
	}


	// =========================================================================
	// VerticalBox / HorizontalBox
	// =========================================================================

	BoxSlot::BoxSlot()
	{
		HPROPERTY(padding, Exposed, Invalidate());
		HPROPERTY(size_rule, Exposed, Invalidate());
		HPROPERTY(fill, Exposed, Invalidate());
		HPROPERTY(h_align, Exposed, Invalidate());
		HPROPERTY(v_align, Exposed, Invalidate());
	}

	BoxPanel::BoxPanel(bool vertical)
		: vertical_(vertical)
	{
	}

	std::unique_ptr<PanelSlot> BoxPanel::CreateSlot() const
	{
		return std::make_unique<BoxSlot>();
	}

	vec2 BoxPanel::ComputeDesiredSize() const
	{
		vec2 size{ 0.f, 0.f };
		for (const Child& child : Children())
		{
			if (child.widget->GetVisibility() == EVisibility::Collapsed)
				continue;

			const auto* slot = static_cast<const BoxSlot*>(child.slot.get());
			const vec2 desired = child.widget->GetDesiredSize();
			const float w = desired.x + slot->padding.x + slot->padding.z;
			const float h = desired.y + slot->padding.y + slot->padding.w;

			if (vertical_)
			{
				size.x = std::max(size.x, w);
				size.y += h;
			}
			else
			{
				size.x += w;
				size.y = std::max(size.y, h);
			}
		}
		return size;
	}

	void BoxPanel::ArrangeChildren(const WidgetRect& rect)
	{
		const float main_size = vertical_ ? rect.h : rect.w;

		// 1. Space taken by the Auto children, total weight of the Fill ones.
		float used = 0.f;
		float total_fill = 0.f;
		for (const Child& child : Children())
		{
			if (child.widget->GetVisibility() == EVisibility::Collapsed)
				continue;

			const auto* slot = static_cast<const BoxSlot*>(child.slot.get());
			const float padding = vertical_ ? slot->padding.y + slot->padding.w : slot->padding.x + slot->padding.z;
			used += padding;

			if (slot->size_rule == static_cast<int>(ESizeRule::Fill))
				total_fill += std::max(0.f, slot->fill);
			else
			{
				const vec2 desired = child.widget->GetDesiredSize();
				used += vertical_ ? desired.y : desired.x;
			}
		}

		const float free_space = std::max(0.f, main_size - used);

		// 2. Children one after the other.
		float cursor = vertical_ ? rect.y : rect.x;
		for (const Child& child : Children())
		{
			const auto* slot = static_cast<const BoxSlot*>(child.slot.get());

			if (child.widget->GetVisibility() == EVisibility::Collapsed)
			{
				ArrangeChild(*child.widget, WidgetRect{ rect.x, rect.y, 0.f, 0.f });
				continue;
			}

			const vec2 desired = child.widget->GetDesiredSize();

			float length;
			if (slot->size_rule == static_cast<int>(ESizeRule::Fill))
				length = total_fill > 0.f ? free_space * std::max(0.f, slot->fill) / total_fill : 0.f;
			else
				length = vertical_ ? desired.y : desired.x;

			// Area of the slot (padding included), then the child inside it.
			WidgetRect area;
			if (vertical_)
			{
				area = { rect.x, cursor, rect.w, length + slot->padding.y + slot->padding.w };
				cursor += area.h;
			}
			else
			{
				area = { cursor, rect.y, length + slot->padding.x + slot->padding.z, rect.h };
				cursor += area.w;
			}

			const WidgetRect inner = ui::ApplyPadding(area, slot->padding);

			// Main axis : the child gets its whole share ; cross axis : alignment.
			const int h_align = vertical_ ? slot->h_align : static_cast<int>(EHAlign::Fill);
			const int v_align = vertical_ ? static_cast<int>(EVAlign::Fill) : slot->v_align;
			ArrangeChild(*child.widget, ui::AlignInArea(inner, desired, h_align, v_align));
		}
	}


	// =========================================================================
	// Overlay
	// =========================================================================

	OverlaySlot::OverlaySlot()
	{
		HPROPERTY(padding, Exposed, Invalidate());
		HPROPERTY(h_align, Exposed, Invalidate());
		HPROPERTY(v_align, Exposed, Invalidate());
	}

	Overlay::Overlay() = default;

	std::unique_ptr<PanelSlot> Overlay::CreateSlot() const
	{
		return std::make_unique<OverlaySlot>();
	}

	vec2 Overlay::ComputeDesiredSize() const
	{
		vec2 size{ 0.f, 0.f };
		for (const Child& child : Children())
		{
			const auto* slot = static_cast<const OverlaySlot*>(child.slot.get());
			const vec2 desired = child.widget->GetDesiredSize();
			size.x = std::max(size.x, desired.x + slot->padding.x + slot->padding.z);
			size.y = std::max(size.y, desired.y + slot->padding.y + slot->padding.w);
		}
		return size;
	}

	void Overlay::ArrangeChildren(const WidgetRect& rect)
	{
		for (const Child& child : Children())
		{
			const auto* slot = static_cast<const OverlaySlot*>(child.slot.get());
			const WidgetRect inner = ui::ApplyPadding(rect, slot->padding);
			ArrangeChild(*child.widget,
			             ui::AlignInArea(inner, child.widget->GetDesiredSize(), slot->h_align, slot->v_align));
		}
	}


	// =========================================================================
	// Border (style : WidgetSystem.cpp)
	// =========================================================================

	Border::Border()
	{
		HPROPERTY(brush_color, Exposed, Invalidate());
		HPROPERTY(brush_texture, Exposed, Invalidate());
		HPROPERTY(padding, Exposed, Invalidate());
		HPROPERTY(h_align, Exposed, Invalidate());
		HPROPERTY(v_align, Exposed, Invalidate());
	}

	vec2 Border::ComputeDesiredSize() const
	{
		vec2 size{ padding.x + padding.z, padding.y + padding.w };
		if (!Children().empty())
		{
			const vec2 desired = Children().front().widget->GetDesiredSize();
			size.x += desired.x;
			size.y += desired.y;
		}
		return size;
	}

	void Border::ArrangeChildren(const WidgetRect& rect)
	{
		if (Children().empty())
			return;

		Widget& child = *Children().front().widget;
		const WidgetRect inner = ui::ApplyPadding(rect, padding);
		ArrangeChild(child, ui::AlignInArea(inner, child.GetDesiredSize(), h_align, v_align));
	}
}
