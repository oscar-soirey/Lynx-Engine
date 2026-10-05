#pragma once

// =============================================================================
// Widget system (engine side, not exported)
// -----------------------------------------------------------------------------
// Native widgets (renderer), events, viewports of the UserWidgets. Called by
// the Engine and the widget .cpp files only.
// =============================================================================

#include <cstdint>
#include <memory>
#include <vector>

namespace lynx
{
	class Widget;
	class UserWidget;
	class PlayerController;

	struct WidgetSystem
	{
		// --- Engine -----------------------------------------------------------
		/** Events, property changes, layout, natives, NativeTick. Before the render. */
		static void Tick(float dt);
		/** Size of the render target (pixels) : the viewports of the players. */
		static void SetRenderSize(int width, int height);
		static void GetRenderSize(int& width, int& height);
		/** Its widgets leave the screen (its viewport is going away). */
		static void OnPlayerDestroyed(PlayerController* player);
		/** Every widget deleted, before the renderer shuts down. */
		static void Shutdown();

		// --- Widgets ----------------------------------------------------------
		static uint64_t Register(Widget* widget);
		static void Unregister(Widget* widget);
		/** Live widget with this id (Widget::GetUniqueId), nullptr otherwise. */
		static Widget* Find(uint64_t id);

		/** Native widgets of `root` and its children (in a viewport). */
		static void CreateNatives(Widget& root, UserWidget& top);
		static void DestroyNatives(Widget& root);
		static void DestroyNative(Widget& widget);

		/** UserWidget at the very top (`widget` itself if it is one at the top). */
		static UserWidget* GetTop(const Widget* widget);

		/** Layout + natives of a UserWidget in a viewport. */
		static void Refresh(UserWidget& top);

		/** DPI scale of the viewport of `top` (1 outside a viewport). */
		static float GetScale(const UserWidget* top);

		/** UserWidgets owned by the engine (CreateWidget). */
		static std::vector<std::unique_ptr<UserWidget>>& Owned();

		/** CreateWidget / DestroyWidget (NativeConstruct / NativeDestruct). */
		static UserWidget* Adopt(std::unique_ptr<UserWidget> widget, PlayerController* owner);
		static void Destroy(UserWidget* widget);
	};
}
