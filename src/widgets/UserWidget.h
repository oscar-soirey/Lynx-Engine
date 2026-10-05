#pragma once

// =============================================================================
// UserWidget (UMG-like) : a widget tree made in the Widget Editor (.widget)
// -----------------------------------------------------------------------------
//   auto* hud = lynx::CreateWidget(player, "ui/Hud.widget");   // assets/ui/Hud.widget
//   hud->GetWidget<lynx::ProgressBar>("Health")->percent = 0.75f;
//   hud->AddToViewport();          // viewport of `player` (split-screen aware)
//   ...
//   hud->RemoveFromParent();       // off screen, still usable
//   lynx::DestroyWidget(hud);      // deleted
//
// C++ subclass (like a Widget Blueprint with code) :
//
//   class MainMenu : public lynx::UserWidget {
//   protected:
//       void NativeConstruct() override {
//           GetWidget<lynx::Button>("Play")->OnClicked.Subscribe([] { ... });
//       }
//   };
//   LYNX_MODULE_REGISTER(MainMenu);          // in LYNX_LINK_MODULE
//   lynx::CreateWidget<MainMenu>(player, "ui/MainMenu.widget");
//
// or set "Class" to MainMenu in the Widget Editor : CreateWidget(player, path)
// then makes a MainMenu.
//
// Lifetime : the widgets made by CreateWidget belong to the engine. They are
// all deleted when the game stops (EndGame) and with the level, like the
// widgets of a Play In Editor session.
// =============================================================================

#include "Widget.h"

namespace lynx
{
	class PlayerController;

	class LYNX_API UserWidget : public Widget
	{
		friend struct WidgetSystem;
		friend class Widget;
	public:
		/**
		 * As a child of another tree (Widget Editor > Palette > User Widgets) :
		 * the .widget asset shown here (path in assets/).
		 */
		std::string asset;

		UserWidget();
		~UserWidget() override;


		// ---------------------------------------------------------------------
		// Content
		// ---------------------------------------------------------------------

		/** Loads assets/<path> (also from the packed archive of a shipped game). */
		bool LoadFromAsset(const std::string& asset_path);
		/** Loads a file anywhere (Widget Editor). */
		bool LoadFromFile(const std::string& file_path);
		bool LoadFromString(const std::string& xml, std::string* error = nullptr);

		std::string SaveToString() const;
		bool SaveToFile(const std::string& file_path) const;

		/** Path given to LoadFromAsset ("" otherwise). */
		const std::string& GetAssetPath() const { return asset_path_; }

		/** C++ class written in the file (Widget Editor > Class), "" = UserWidget. */
		const std::string& GetClassName() const { return class_name_; }
		void SetClassName(const std::string& name) { class_name_ = name; }

		/** Resolution of the Widget Editor (default 1920 x 1080). */
		vec2 GetDesignSize() const { return design_size_; }
		void SetDesignSize(vec2 size) { design_size_ = size; }

		Widget* GetRoot() const { return root_.get(); }
		void SetRoot(std::unique_ptr<Widget> root);
		std::unique_ptr<Widget> TakeRoot();

		/** First widget called `name` (depth-first, this tree only). */
		Widget* GetWidgetFromName(const std::string& name) const;

		template<typename T>
		T* GetWidget(const std::string& name) const { return dynamic_cast<T*>(GetWidgetFromName(name)); }

		void ForEachWidget(const std::function<void(Widget&)>& fn) override;


		// ---------------------------------------------------------------------
		// Screen
		// ---------------------------------------------------------------------

		/**
		 * Shows the widget on the viewport of its player (the default player
		 * when it has none). Higher z_order = in front.
		 */
		void AddToViewport(int z_order = 0);

		/** Off the viewport (still alive : AddToViewport again later). */
		void RemoveFromParent() override;

		bool IsInViewport() const { return in_viewport_; }
		int GetViewportZOrder() const { return z_order_; }

		PlayerController* GetOwningPlayer() const { return owner_; }
		/** A widget in a viewport moves to the viewport of `player`. */
		void SetOwningPlayer(PlayerController* player);


		// ---------------------------------------------------------------------
		// Widget Editor : layout without a viewport (DPI scale 1)
		// ---------------------------------------------------------------------

		void LayoutForDesign(float width, float height);

	protected:
		/** After the tree is loaded (the named widgets exist). */
		virtual void NativeConstruct() {}
		/** Before the widget is deleted. */
		virtual void NativeDestruct() {}
		/** Every frame while in a viewport. */
		virtual void NativeTick(float dt) { (void)dt; }

		vec2 ComputeDesiredSize() const override;
		void ArrangeChildren(const WidgetRect& rect) override;

	private:
		std::unique_ptr<Widget> root_;
		std::string asset_path_;
		std::string class_name_;
		vec2 design_size_{ 1920.f, 1080.f };

		PlayerController* owner_ = nullptr;
		bool in_viewport_ = false;
		int z_order_ = 0;
		bool constructed_ = false;

		// Layout / look to compute again (set by Widget::Invalidate).
		bool dirty_ = true;
		// Viewport size of the last layout (pixels).
		float last_viewport_w_ = 0.f;
		float last_viewport_h_ = 0.f;
	};


	/**
	 * New UserWidget from assets/<asset_path> for `owner` (nullptr = default
	 * player). Its class is the one written in the file (Widget Editor >
	 * Class, registered with LYNX_MODULE_REGISTER), else UserWidget.
	 * nullptr if the file can not be read.
	 */
	LYNX_API UserWidget* CreateWidget(PlayerController* owner, const std::string& asset_path);

	/** Deletes a widget made by CreateWidget (removed from the viewport first). */
	LYNX_API void DestroyWidget(UserWidget* widget);

	namespace ui
	{
		/** The engine owns `widget` from now on (CreateWidget<T>). */
		LYNX_API UserWidget* AdoptWidget(std::unique_ptr<UserWidget> widget, PlayerController* owner);

		/** Every widget made by CreateWidget is deleted. */
		LYNX_API void DestroyAllWidgets();

		/** Widgets made by CreateWidget, still alive. */
		LYNX_API std::vector<UserWidget*> GetAllWidgets();

		/** `widget` and its children as text (slot included) : copy / paste. */
		LYNX_API std::string CopyWidget(Widget& widget);

		/**
		 * Rebuilds a copied widget into `panel` at `index` (-1 = at the end).
		 * Its slot settings come back when the panel has the same kind of slot.
		 * nullptr if the text is not a copied widget or the panel is full.
		 */
		LYNX_API Widget* PasteWidget(PanelWidget& panel, const std::string& text, int index = -1);
	}

	/** New T (a UserWidget class) loaded from assets/<asset_path>. */
	template<typename T>
	T* CreateWidget(PlayerController* owner, const std::string& asset_path)
	{
		static_assert(std::is_base_of_v<UserWidget, T>, "T must inherit from lynx::UserWidget");
		auto widget = std::make_unique<T>();
		if (!asset_path.empty() && !widget->LoadFromAsset(asset_path))
			return nullptr;
		return static_cast<T*>(ui::AdoptWidget(std::move(widget), owner));
	}
}
