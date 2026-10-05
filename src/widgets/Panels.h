#pragma once

// =============================================================================
// Panels (UMG-like) : CanvasPanel, VerticalBox, HorizontalBox, Overlay, Border
// -----------------------------------------------------------------------------
// A panel places its children through their slot (GetSlotAs<CanvasPanelSlot>()
// ...). Slot fields are reflected properties, like the widget ones.
// =============================================================================

#include "Widget.h"

namespace lynx
{
	// =========================================================================
	// CanvasPanel : free placement with anchors
	// =========================================================================
	// Anchors are fractions of the panel (0,0 = top-left, 1,1 = bottom-right).
	// On an axis where anchor_min == anchor_max, offsets are position and size
	// (x = left, z = width ; y = top, w = height). Where they differ, the child
	// stretches between the anchors and offsets are margins (x = left,
	// z = right ; y = top, w = bottom), like Unreal.
	// alignment : pivot of the child (0.5, 0.5 = its center on the position).

	class LYNX_API CanvasPanelSlot : public PanelSlot
	{
	public:
		vec2 anchor_min{ 0.f, 0.f };
		vec2 anchor_max{ 0.f, 0.f };
		vec4 offsets{ 0.f, 0.f, 100.f, 30.f };
		vec2 alignment{ 0.f, 0.f };
		/** Size = desired size of the child (offsets z / w are ignored). */
		bool auto_size = false;
		/** Drawing order among the children of the panel (higher = in front). */
		int z_order = 0;

		CanvasPanelSlot();

		void SetAnchors(vec2 min, vec2 max);
		/** Position / size on the axes with a point anchor. */
		void SetPosition(vec2 position);
		void SetSize(vec2 size);
		vec2 GetPosition() const { return { offsets.x, offsets.y }; }
		vec2 GetSize() const { return { offsets.z, offsets.w }; }
	};

	class LYNX_API CanvasPanel : public PanelWidget
	{
	public:
		CanvasPanel();

		/** Adds `child` at `position` with `size` (point anchor top-left). */
		CanvasPanelSlot* AddChildToCanvas(std::unique_ptr<Widget> child);

	protected:
		std::unique_ptr<PanelSlot> CreateSlot() const override;
		vec2 ComputeDesiredSize() const override;
		void ArrangeChildren(const WidgetRect& rect) override;
	};


	// =========================================================================
	// VerticalBox / HorizontalBox : children one after the other
	// =========================================================================

	class LYNX_API BoxSlot : public PanelSlot
	{
	public:
		/** Left, top, right, bottom. */
		vec4 padding{ 0.f, 0.f, 0.f, 0.f };
		/** ESizeRule : Auto = desired size, Fill = share of the free space. */
		int size_rule = 0;
		/** Share of the free space (Fill). */
		float fill = 1.f;
		/** EHAlign / EVAlign inside the slot. */
		int h_align = 0;
		int v_align = 0;

		BoxSlot();
	};

	class LYNX_API BoxPanel : public PanelWidget
	{
	public:
		explicit BoxPanel(bool vertical);
		bool IsVertical() const { return vertical_; }

	protected:
		std::unique_ptr<PanelSlot> CreateSlot() const override;
		vec2 ComputeDesiredSize() const override;
		void ArrangeChildren(const WidgetRect& rect) override;

	private:
		bool vertical_;
	};

	class LYNX_API VerticalBox : public BoxPanel
	{
	public:
		VerticalBox() : BoxPanel(true) {}
	};

	class LYNX_API HorizontalBox : public BoxPanel
	{
	public:
		HorizontalBox() : BoxPanel(false) {}
	};


	// =========================================================================
	// Overlay : children on top of each other
	// =========================================================================

	class LYNX_API OverlaySlot : public PanelSlot
	{
	public:
		vec4 padding{ 0.f, 0.f, 0.f, 0.f };
		int h_align = 0;
		int v_align = 0;

		OverlaySlot();
	};

	class LYNX_API Overlay : public PanelWidget
	{
	public:
		Overlay();

	protected:
		std::unique_ptr<PanelSlot> CreateSlot() const override;
		vec2 ComputeDesiredSize() const override;
		void ArrangeChildren(const WidgetRect& rect) override;
	};


	// =========================================================================
	// Border : a background (color / texture) around ONE child
	// =========================================================================

	class LYNX_API Border : public PanelWidget
	{
	public:
		/** RGBA (0..1). */
		vec4 brush_color{ 0.1f, 0.1f, 0.1f, 0.8f };
		/** Texture (path in assets/, "" = plain color). */
		std::string brush_texture;
		/** Space around the child : left, top, right, bottom. */
		vec4 padding{ 4.f, 2.f, 4.f, 2.f };
		int h_align = 0;
		int v_align = 0;

		Border();

		int GetMaxChildren() const override { return 1; }

	protected:
		vec2 ComputeDesiredSize() const override;
		void ArrangeChildren(const WidgetRect& rect) override;
		int GetNativeType() const override;
		void SyncNativeStyle() override;
	};


	namespace ui
	{
		/**
		 * Places a child of `desired` size in `area` with the alignments
		 * (Fill = the whole area). Used by the panels.
		 */
		LYNX_API WidgetRect AlignInArea(const WidgetRect& area, vec2 desired, int h_align, int v_align);

		/** `rect` without the padding (left, top, right, bottom). */
		LYNX_API WidgetRect ApplyPadding(const WidgetRect& rect, const vec4& padding);
	}
}
