#include "Private/WidgetSystem.h"
#include "Private/WidgetNative.h"

#include "Widget.h"
#include "Panels.h"
#include "UserWidget.h"

#include "../core/Engine.h"
#include "../core/RessourceManager.h"
#include "../core/Filesystem.h"
#include "../gameplay/PlayerController.h"
#include "../scripting/Private/ScriptSystem.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unordered_map>

namespace lynx
{
	namespace
	{
		struct QueuedEvent
		{
			uint64_t serial;
			int event;
			float value;
		};

		uint64_t g_next_serial = 1;
		std::unordered_map<uint64_t, Widget*> g_live;
		std::vector<QueuedEvent> g_events;

		int g_render_w = 0;
		int g_render_h = 0;

		HRL_id g_white_texture = HRL_INVALID_ID;
		HRL_id g_default_font = HRL_INVALID_ID;
		bool g_default_font_searched = false;

		HRL_id LoadFontFile(const std::filesystem::path& path)
		{
			std::error_code ec;
			if (!std::filesystem::is_regular_file(path, ec))
				return HRL_INVALID_ID;

			std::ifstream in(path, std::ios::binary);
			std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (data.empty())
				return HRL_INVALID_ID;
			return HRL_CreateFont(data.data(), data.size());
		}

		// Font of the texts without a font : the editor font (the Widget Editor
		// preview draws with it), so the game looks like the preview.
		// fonts/ next to the executable (editor, or shipped game : ShipGame
		// copies it there), then a Windows font.
		HRL_id DefaultFont()
		{
			if (g_default_font != HRL_INVALID_ID && HRL_IsValidFont(g_default_font))
				return g_default_font;
			if (g_default_font_searched && g_default_font == HRL_INVALID_ID)
				return HRL_INVALID_ID;

			g_default_font_searched = true;
			g_default_font = HRL_INVALID_ID;

			const std::filesystem::path exe(fs::GetExecutableFolder());
			std::vector<std::filesystem::path> candidates = {
				exe / "fonts" / "VCR-OSD-MONO.ttf",
				exe / "fonts" / "normal-font.ttf",
				std::filesystem::path("fonts") / "VCR-OSD-MONO.ttf",
			};
#ifdef _WIN32
			const std::filesystem::path system_fonts("C:/Windows/Fonts");
			candidates.push_back(system_fonts / "consola.ttf");
			candidates.push_back(system_fonts / "segoeui.ttf");
			candidates.push_back(system_fonts / "arial.ttf");
#endif
			for (const std::filesystem::path& path : candidates)
			{
				const HRL_id font = LoadFontFile(path);
				if (font != HRL_INVALID_ID)
					return g_default_font = font;
			}

			std::cerr << "[Widgets] no default font (fonts/VCR-OSD-MONO.ttf) : texts without a font are not drawn\n";
			return HRL_INVALID_ID;
		}

		// 1 x 1 white PNG : texture of the plain colors (Image / Border / Button).
		const unsigned char kWhitePng[] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
			0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
			0x89, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0x0f, 0x04, 0x00,
			0x09, 0xfb, 0x03, 0xfd, 0xfb, 0x5e, 0x6b, 0x2b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
			0xae, 0x42, 0x60, 0x82
		};

		std::vector<UserWidget*>& OnScreen()
		{
			static std::vector<UserWidget*> list;
			return list;
		}

		bool IsOnScreen(UserWidget* widget)
		{
			const auto& list = OnScreen();
			return std::find(list.begin(), list.end(), widget) != list.end();
		}

		void ForEachDirectChild(Widget& widget, const std::function<void(Widget&)>& fn)
		{
			if (auto* panel = dynamic_cast<PanelWidget*>(&widget))
			{
				for (int i = 0; i < panel->GetChildCount(); ++i)
					fn(*panel->GetChildAt(i));
			}
			else if (auto* user = dynamic_cast<UserWidget*>(&widget))
			{
				if (user->GetRoot())
					fn(*user->GetRoot());
			}
		}

		// Viewport of the player of `top`, in pixels.
		bool ViewportPixels(const UserWidget& top, float& w, float& h)
		{
			const PlayerController* player = top.GetOwningPlayer();
			if (!player || g_render_w <= 0 || g_render_h <= 0)
				return false;

			const vec4 rect = player->GetViewportRect();
			w = rect.z * static_cast<float>(g_render_w);
			h = rect.w * static_cast<float>(g_render_h);
			return w >= 1.f && h >= 1.f;
		}

		// ---- Renderer callbacks : queued, dispatched by Tick ----------------

		void ButtonCallback(HRL_id, int clicked, int released, void* user_data)
		{
			const uint64_t serial = ui::native::FromUserData(user_data);
			if (clicked)
				ui::native::PushEvent(serial, ui::native::kPressed, 0.f);
			if (released)
				ui::native::PushEvent(serial, ui::native::kReleased, 0.f);
		}

		void SliderCallback(HRL_id, float value, void* user_data)
		{
			ui::native::PushEvent(ui::native::FromUserData(user_data), ui::native::kValueChanged, value);
		}

		void CheckboxCallback(HRL_id, int checked, void* user_data)
		{
			ui::native::PushEvent(ui::native::FromUserData(user_data), ui::native::kCheckChanged, checked ? 1.f : 0.f);
		}

		// State given by the parents.
		struct Inherited
		{
			bool visible = true;
			bool enabled = true;
			bool hit_test = true;
			float opacity = 1.f;
		};
	}


	// =========================================================================
	// Natives
	// =========================================================================

	namespace ui::native
	{
		HRL_id Texture(const std::string& path)
		{
			if (!path.empty())
			{
				const uint32_t texture = RessourceTex(path.c_str());
				if (texture != HRL_INVALID_ID)
					return texture;
			}

			if (g_white_texture == HRL_INVALID_ID)
				g_white_texture = HRL_CreateTexture(reinterpret_cast<const char*>(kWhitePng), sizeof(kWhitePng));
			return g_white_texture;
		}

		HRL_id Font(const std::string& path)
		{
			if (!path.empty())
			{
				const uint32_t font = RessourceFont(path.c_str());
				if (font != HRL_INVALID_ID)
					return font;
			}
			// No font (or not found) : the default one, never "no text at all".
			return DefaultFont();
		}

		void PushEvent(uint64_t serial, int event, float value)
		{
			g_events.push_back({ serial, event, value });
		}
	}

	uint64_t WidgetSystem::Register(Widget* widget)
	{
		const uint64_t serial = g_next_serial++;
		g_live[serial] = widget;
		return serial;
	}

	void WidgetSystem::Unregister(Widget* widget)
	{
		g_live.erase(widget->serial_);
		scripting::OnWidgetDestroyed(widget->serial_);
	}

	Widget* WidgetSystem::Find(uint64_t id)
	{
		auto it = g_live.find(id);
		return it == g_live.end() ? nullptr : it->second;
	}

	UserWidget* WidgetSystem::GetTop(const Widget* widget)
	{
		if (!widget)
			return nullptr;

		UserWidget* top = dynamic_cast<UserWidget*>(const_cast<Widget*>(widget));
		const Widget* w = widget;
		while (UserWidget* outer = w->GetUserWidget())
		{
			top = outer;
			w = outer;
		}
		return top;
	}

	void WidgetSystem::CreateNatives(Widget& root, UserWidget& top)
	{
		if (!top.in_viewport_ || !top.owner_)
			return;

		const uint32_t viewport = top.owner_->GetViewportBackend();
		if (viewport == HRL_INVALID_ID)
			return;

		if (!IsOnScreen(&top))
			OnScreen().push_back(&top);

		root.ForEachWidget([&](Widget& w)
		{
			const int type = w.GetNativeType();
			if (type < 0 || w.native_)
				return;

			const HRL_id id = HRL_CreateWidget(viewport, static_cast<HRL_EWidgetType>(type));
			if (id == HRL_INVALID_ID)
			{
				std::cerr << "[WIDGET] The renderer could not create a widget for " << w.GetTypeName() << "\n";
				return;
			}

			auto* native = new WidgetNative();
			native->id = id;
			native->type = type;
			w.native_ = native;

			// Positions are the top-left corner of the widget.
			HRL_SetWidgetAnchor(id, 0.f, 0.f);

			void* user_data = ui::native::ToUserData(w.serial_);
			switch (type)
			{
			case HRL_WIDGET_BUTTON:   HRL_SetButtonPressedCallback(id, ButtonCallback, user_data); break;
			case HRL_WIDGET_SLIDER:   HRL_SetSliderChangedCallback(id, SliderCallback, user_data); break;
			case HRL_WIDGET_CHECKBOX: HRL_SetCheckboxChangedCallback(id, CheckboxCallback, user_data); break;
			default: break;
			}
		});

		top.dirty_ = true;
	}

	void WidgetSystem::DestroyNative(Widget& widget)
	{
		if (!widget.native_)
			return;

		if (HRL_IsValidWidget(widget.native_->id))
			HRL_DeleteWidget(widget.native_->id);

		delete widget.native_;
		widget.native_ = nullptr;
	}

	void WidgetSystem::DestroyNatives(Widget& root)
	{
		root.ForEachWidget([](Widget& w) { DestroyNative(w); });

		// A UserWidget leaving the screen.
		if (auto* user = dynamic_cast<UserWidget*>(&root))
		{
			auto& list = OnScreen();
			list.erase(std::remove(list.begin(), list.end(), user), list.end());
		}
	}

	float WidgetSystem::GetScale(const UserWidget* top)
	{
		float w = 0.f, h = 0.f;
		if (!top || !top->in_viewport_ || !ViewportPixels(*top, w, h))
			return 1.f;
		return std::max(0.01f, std::min(w, h) / ui::GetDPIReference());
	}

	void WidgetSystem::Refresh(UserWidget& top)
	{
		float vp_w = 0.f, vp_h = 0.f;
		if (!ViewportPixels(top, vp_w, vp_h))
			return;

		const float scale = GetScale(&top);

		// Layout in slate units : the widget fills the viewport.
		top.Arrange(WidgetRect{ 0.f, 0.f, vp_w / scale, vp_h / scale });
		top.last_viewport_w_ = vp_w;
		top.last_viewport_h_ = vp_h;

		// Widgets added since the last refresh.
		CreateNatives(top, top);

		const int base_z = top.z_order_ * 100000;
		int order = 0;

		std::function<void(Widget&, Inherited, int)> apply = [&](Widget& w, Inherited parent, int canvas_z)
		{
			const EVisibility visibility = w.GetVisibility();

			Inherited self = parent;
			self.visible = parent.visible && visibility != EVisibility::Collapsed && visibility != EVisibility::Hidden;
			self.enabled = parent.enabled && w.is_enabled;
			self.opacity = parent.opacity * std::clamp(w.render_opacity, 0.f, 1.f);
			const bool self_hit = parent.hit_test &&
			                      visibility != EVisibility::HitTestInvisible &&
			                      visibility != EVisibility::SelfHitTestInvisible;
			self.hit_test = parent.hit_test && visibility != EVisibility::HitTestInvisible;

			// Canvas children : their z_order first.
			if (auto* slot = w.GetSlotAs<CanvasPanelSlot>())
				canvas_z = slot->z_order;

			if (WidgetNative* native = w.native_)
			{
				const HRL_id id = native->id;
				HRL_SetWidgetVisible(id, self.visible ? 1 : 0);

				if (self.visible)
				{
					const WidgetRect& g = w.GetGeometry();
					HRL_SetWidgetPosition(id, g.x * scale / vp_w, g.y * scale / vp_h);
					HRL_SetWidgetSize(id, g.w * scale / vp_w, g.h * scale / vp_h);
					HRL_SetWidgetAlpha(id, self.opacity);
					HRL_SetWidgetEnabled(id, self.enabled ? 1 : 0);
					HRL_SetWidgetZIndex(id, base_z + canvas_z * 1000 + order);

					native->interactive = self.enabled && self_hit;
					w.SyncNativeStyle();
				}
				++order;
			}

			ForEachDirectChild(w, [&](Widget& child) { apply(child, self, canvas_z); });
		};

		apply(top, Inherited{}, 0);
	}

	void WidgetSystem::Tick(float dt)
	{
		// 1. Events of the last frame (clicks...) : any widget may be destroyed.
		std::vector<QueuedEvent> events;
		events.swap(g_events);
		for (const QueuedEvent& e : events)
		{
			auto it = g_live.find(e.serial);
			if (it != g_live.end())
				it->second->OnNativeEvent(e.event, e.value);
		}

		// 2. Widgets on screen : hover, property changes, layout, NativeTick.
		const std::vector<UserWidget*> screen = OnScreen();
		for (UserWidget* top : screen)
		{
			if (!IsOnScreen(top))
				continue;

			top->ForEachWidget([](Widget& w)
			{
				// Reflected fields changed from code -> Invalidate.
				w.Update(0.0);
				if (PanelSlot* slot = w.GetSlot())
					slot->Update(0.0);

				// Hover : Button events.
				if (w.native_ && w.native_->type == HRL_WIDGET_BUTTON)
				{
					const bool hovered = HRL_IsWidgetHovered(w.native_->id) != 0;
					if (hovered != w.native_->hovered)
					{
						w.native_->hovered = hovered;
						ui::native::PushEvent(w.serial_, hovered ? ui::native::kHovered : ui::native::kUnhovered, 0.f);
					}
				}
			});

			float vp_w = 0.f, vp_h = 0.f;
			const bool has_viewport = ViewportPixels(*top, vp_w, vp_h);
			const bool resized = has_viewport && (vp_w != top->last_viewport_w_ || vp_h != top->last_viewport_h_);

			if (has_viewport && (top->dirty_ || resized))
			{
				top->dirty_ = false;
				Refresh(*top);
			}
		}

		for (UserWidget* top : screen)
		{
			if (!IsOnScreen(top))
				continue;

			std::vector<UserWidget*> users;
			top->ForEachWidget([&](Widget& w)
			{
				if (auto* u = dynamic_cast<UserWidget*>(&w))
					users.push_back(u);
			});

			for (UserWidget* u : users)
			{
				if (u->constructed_)
				{
					u->NativeTick(dt);
					if (IsOnScreen(top))
						scripting::OnUserWidgetEvent(*u, scripting::WidgetEvent::Tick, dt);
				}
				if (!IsOnScreen(top))
					break;      // removed by its own NativeTick
			}
		}
	}

	void WidgetSystem::SetRenderSize(int width, int height)
	{
		g_render_w = width;
		g_render_h = height;
	}

	void WidgetSystem::GetRenderSize(int& width, int& height)
	{
		width = g_render_w;
		height = g_render_h;
	}

	void WidgetSystem::OnPlayerDestroyed(PlayerController* player)
	{
		const std::vector<UserWidget*> screen = OnScreen();
		for (UserWidget* top : screen)
		{
			if (top->owner_ == player)
				top->RemoveFromParent();
		}

		for (const auto& owned : Owned())
		{
			if (owned->owner_ == player)
				owned->owner_ = nullptr;
		}
	}

	void WidgetSystem::Shutdown()
	{
		ui::DestroyAllWidgets();

		// Widgets made with new by the game, still on screen : no native left.
		const std::vector<UserWidget*> screen = OnScreen();
		for (UserWidget* top : screen)
			top->RemoveFromParent();
		OnScreen().clear();

		g_events.clear();
		g_white_texture = HRL_INVALID_ID;     // freed by HRL_Shutdown
		g_default_font = HRL_INVALID_ID;      // idem
		g_default_font_searched = false;
	}

	std::vector<std::unique_ptr<UserWidget>>& WidgetSystem::Owned()
	{
		static std::vector<std::unique_ptr<UserWidget>> owned;
		return owned;
	}


	// =========================================================================
	// Widget members that need the natives
	// =========================================================================

	bool Widget::IsHovered() const
	{
		return native_ && native_->hovered;
	}
}
