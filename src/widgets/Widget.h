#pragma once

// =============================================================================
// Widgets (UMG-like)
// -----------------------------------------------------------------------------
// A widget tree, like Unreal's UMG :
//
//   UserWidget            the asset (.widget file made in the Widget Editor)
//    └ CanvasPanel        panels place their children through a slot
//       ├ Button          (CanvasPanelSlot : anchors, offsets, alignment...)
//       └ VerticalBox
//          ├ TextBlock    (BoxSlot : padding, size rule, alignment)
//          └ ProgressBar
//
//   auto* menu = lynx::CreateWidget(player, "ui/MainMenu.widget");
//   menu->GetWidget<lynx::Button>("PlayButton")->OnClicked.Subscribe([] { ... });
//   menu->AddToViewport();
//
// Every widget and slot is a lynx::Object : its fields are reflected
// properties (Details panel of the Widget Editor, .widget files). Changing a
// field from code is enough : the change is seen at the next frame.
//
// Units : "slate units". A UserWidget fills the viewport of its player, whose
// size in slate units is its size in pixels / DPI scale (DPI scale = shortest
// side of the viewport / 1080, see ui::SetDPIReference). A layout made for
// 1920 x 1080 looks the same at any resolution.
//
// The renderer (HRL) is never visible here : the native widgets live in the
// .cpp files and are only created for a UserWidget in a viewport.
// =============================================================================

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/Common.h"
#include "../core/data/EventDispatcher.h"
#include "../gameplay/Object.h"

namespace lynx
{
	class Widget;
	class PanelWidget;
	class UserWidget;
	struct WidgetNative;    // renderer side, defined in the .cpp

	// Values of the int properties (int : reflected / saved as numbers).
	enum class EVisibility : int
	{
		Visible = 0,
		Collapsed = 1,              // hidden, takes no space
		Hidden = 2,                 // hidden, keeps its space
		HitTestInvisible = 3,       // visible, ignores the mouse (and its children)
		SelfHitTestInvisible = 4,   // visible, ignores the mouse (not its children)
	};

	enum class EHAlign : int { Fill = 0, Left = 1, Center = 2, Right = 3 };
	enum class EVAlign : int { Fill = 0, Top = 1, Center = 2, Bottom = 3 };
	enum class ESizeRule : int { Auto = 0, Fill = 1 };
	enum class EOrientation : int { Horizontal = 0, Vertical = 1 };

	// Rectangle in slate units, relative to the top-left corner of the
	// UserWidget at the top of the tree.
	struct WidgetRect
	{
		float x = 0.f;
		float y = 0.f;
		float w = 0.f;
		float h = 0.f;

		bool Contains(float px, float py) const
			{ return px >= x && py >= y && px < x + w && py < y + h; }
	};


	// =========================================================================
	// Slot : how a panel places one child (each panel has its slot class)
	// =========================================================================

	class LYNX_API PanelSlot : public Object
	{
		friend class PanelWidget;
	public:
		PanelSlot();
		~PanelSlot() override;

		Widget* GetContent() const { return content_; }
		PanelWidget* GetParent() const { return parent_; }

		/** The layout of the panel is computed again (called by the properties). */
		void Invalidate();

	private:
		Widget* content_ = nullptr;
		PanelWidget* parent_ = nullptr;
	};


	// =========================================================================
	// Widget
	// =========================================================================

	class LYNX_API Widget : public Object
	{
		friend class PanelWidget;
		friend class UserWidget;
		friend struct WidgetSystem;
	public:
		/** EVisibility */
		int visibility = 0;
		bool is_enabled = true;
		float render_opacity = 1.f;

		Widget();
		~Widget() override;

		Widget(const Widget&) = delete;
		Widget& operator=(const Widget&) = delete;

		/** Id never reused while the program runs (scripting, events). */
		uint64_t GetUniqueId() const { return serial_; }

		/** Name in the tree (GetWidgetFromName). Same as object_id_. */
		const std::string& GetName() const { return object_id_; }
		void SetName(const std::string& name) { object_id_ = name; }

		EVisibility GetVisibility() const { return static_cast<EVisibility>(visibility); }
		void SetVisibility(EVisibility v) { visibility = static_cast<int>(v); }
		/** Visible / HitTestInvisible / SelfHitTestInvisible, and its parents too. */
		bool IsVisible() const;

		PanelWidget* GetParent() const { return slot_ ? slot_->GetParent() : nullptr; }
		PanelSlot* GetSlot() const { return slot_; }

		template<typename T>
		T* GetSlotAs() const { return dynamic_cast<T*>(slot_); }

		/** UserWidget at the top of the tree (nullptr when not in one). */
		UserWidget* GetUserWidget() const;

		/**
		 * Removes and DELETES this widget (and its children). For a UserWidget
		 * at the top : removes it from the viewport (it stays usable).
		 */
		virtual void RemoveFromParent();

		/** Last computed rectangle (slate units). */
		const WidgetRect& GetGeometry() const { return geometry_; }

		/** Size wanted by the widget (slate units), Collapsed = 0. */
		vec2 GetDesiredSize() const;

		/** The mouse is over the native widget (only in a viewport). */
		bool IsHovered() const;

		/** Layout + look updated at the next frame. */
		void Invalidate();

		/** Depth-first visit : this widget, then the widgets inside it. */
		virtual void ForEachWidget(const std::function<void(Widget&)>& fn);

	protected:
		// --- Layout (pure : no renderer, also used by the Widget Editor) -----
		virtual vec2 ComputeDesiredSize() const { return { 0.f, 0.f }; }
		/** Places the children inside `rect` (geometry_ is already set). */
		virtual void ArrangeChildren(const WidgetRect& rect) { (void)rect; }

		// --- Renderer (implemented in the .cpp files) ------------------------
		/** Renderer widget type (-1 : nothing to draw, ex : panels). */
		virtual int GetNativeType() const { return -1; }
		/** Pushes the style (text, colors, textures...) to the native widget. */
		virtual void SyncNativeStyle() {}
		/** A native event (click...) was received : dispatch it now. */
		virtual void OnNativeEvent(int event, float value) { (void)event; (void)value; }

		/** Native widget of this widget (nullptr outside a viewport). */
		WidgetNative* GetNative() const { return native_; }

		/** DPI scale of the viewport (1 in the Widget Editor). */
		float GetDPIScale() const;

	private:
		void Arrange(const WidgetRect& rect);

		PanelSlot* slot_ = nullptr;
		UserWidget* root_of_ = nullptr;     // set on the root of a UserWidget
		WidgetRect geometry_{};
		mutable vec2 desired_{ 0.f, 0.f };
		WidgetNative* native_ = nullptr;
		uint64_t serial_ = 0;     // live widget id (native events)
	};


	// =========================================================================
	// PanelWidget : owns its children
	// =========================================================================

	class LYNX_API PanelWidget : public Widget
	{
	public:
		PanelWidget();
		~PanelWidget() override;

		/**
		 * Adds `child` (the panel owns it) and returns its slot (nullptr when
		 * the panel is full, ex : Border has one child).
		 */
		PanelSlot* AddChild(std::unique_ptr<Widget> child);

		template<typename T, typename... Args>
		T* AddNew(Args&&... args)
		{
			auto child = std::make_unique<T>(std::forward<Args>(args)...);
			T* raw = child.get();
			return AddChild(std::move(child)) ? raw : nullptr;
		}

		/** Inserts at `index` (clamped). */
		PanelSlot* InsertChildAt(int index, std::unique_ptr<Widget> child);

		/** Takes `child` out of the panel (its slot is deleted). */
		std::unique_ptr<Widget> RemoveChild(Widget* child);
		void ClearChildren();

		int GetChildCount() const { return static_cast<int>(children_.size()); }
		Widget* GetChildAt(int index) const;
		int GetChildIndex(const Widget* child) const;

		/** Max children (-1 = any). */
		virtual int GetMaxChildren() const { return -1; }
		bool CanAddChild() const;

		void ForEachWidget(const std::function<void(Widget&)>& fn) override;

	protected:
		/** Slot class of this panel. */
		virtual std::unique_ptr<PanelSlot> CreateSlot() const;

		/** Gives `rect` to `child` (and its children). For ArrangeChildren. */
		static void ArrangeChild(Widget& child, const WidgetRect& rect) { child.Arrange(rect); }

		struct Child
		{
			std::unique_ptr<Widget> widget;
			std::unique_ptr<PanelSlot> slot;
		};

		const std::vector<Child>& Children() const { return children_; }

	private:
		std::vector<Child> children_;
	};


	// =========================================================================
	// Widget classes by name (.widget files, Palette of the Widget Editor)
	// =========================================================================

	namespace ui
	{
		/**
		 * New widget of class `name` : built-in classes ("Button"...), then the
		 * engine factory (game classes registered with LYNX_MODULE_REGISTER
		 * that derive from lynx::Widget). nullptr if unknown.
		 */
		LYNX_API std::unique_ptr<Widget> NewWidget(const std::string& name);

		/** Built-in classes, Palette order. */
		LYNX_API const std::vector<std::string>& GetBuiltinWidgetClasses();

		/**
		 * Names of an int property used as an enum ("visibility", "h_align"...).
		 * Empty if the property is a plain number.
		 */
		LYNX_API const std::vector<std::string>& GetEnumNames(const std::string& property);

		/** Reference height for the DPI scale (default 1080). */
		LYNX_API void SetDPIReference(float shortest_side);
		LYNX_API float GetDPIReference();
	}
}
