#pragma once

// =============================================================================
// Common widgets (UMG-like) : TextBlock, Image, Button, Slider, CheckBox,
// ProgressBar, Spacer
// -----------------------------------------------------------------------------
// Colors are RGBA vec4 (0..1). Textures and fonts are paths in assets/
// ("" = default). Events are dispatched at the beginning of the next frame
// (never inside the renderer) : a callback may destroy any widget.
// =============================================================================

#include "Widget.h"

namespace lynx
{
	class LYNX_API TextBlock : public Widget
	{
	public:
		std::string text = "Text Block";
		/** Slate units (scaled with the DPI). */
		float font_size = 24.f;
		/** .ttf in assets/ ("" = default font of the renderer). */
		std::string font;
		vec4 color{ 1.f, 1.f, 1.f, 1.f };

		TextBlock();

		void SetText(const std::string& t) { text = t; }
		const std::string& GetText() const { return text; }

	protected:
		vec2 ComputeDesiredSize() const override;
		int GetNativeType() const override;
		void SyncNativeStyle() override;
	};


	class LYNX_API Image : public Widget
	{
	public:
		/** Texture in assets/ ("" = plain color). */
		std::string texture;
		vec4 tint{ 1.f, 1.f, 1.f, 1.f };
		/** Desired size (slate units). */
		vec2 image_size{ 64.f, 64.f };

		Image();

	protected:
		vec2 ComputeDesiredSize() const override;
		int GetNativeType() const override;
		void SyncNativeStyle() override;
	};


	class LYNX_API Button : public Widget
	{
	public:
		std::string text = "Button";
		float font_size = 24.f;
		std::string font;
		vec4 text_color{ 1.f, 1.f, 1.f, 1.f };

		vec4 normal_color{ 0.22f, 0.22f, 0.24f, 1.f };
		vec4 hovered_color{ 0.32f, 0.32f, 0.35f, 1.f };
		vec4 pressed_color{ 0.14f, 0.14f, 0.15f, 1.f };
		/** Background textures (assets/, "" = plain color). */
		std::string normal_texture;
		std::string hovered_texture;
		std::string pressed_texture;
		/** Space around the text (desired size) : left, top, right, bottom. */
		vec4 padding{ 16.f, 8.f, 16.f, 8.f };

		HEventDispatcher<> OnClicked;
		HEventDispatcher<> OnPressed;
		HEventDispatcher<> OnReleased;
		HEventDispatcher<> OnHovered;
		HEventDispatcher<> OnUnhovered;

		Button();

	protected:
		vec2 ComputeDesiredSize() const override;
		int GetNativeType() const override;
		void SyncNativeStyle() override;
		void OnNativeEvent(int event, float value) override;
	};


	class LYNX_API Slider : public Widget
	{
	public:
		float value = 0.5f;
		float min_value = 0.f;
		float max_value = 1.f;
		/** EOrientation */
		int orientation = 0;
		vec4 bar_color{ 0.15f, 0.15f, 0.16f, 1.f };
		vec4 fill_color{ 0.85f, 0.6f, 0.17f, 1.f };
		vec4 handle_color{ 0.9f, 0.9f, 0.9f, 1.f };
		/** false : display only. */
		bool interactive = true;

		/** New value (the user moved the handle). */
		HEventDispatcher<float> OnValueChanged;

		Slider();

	protected:
		vec2 ComputeDesiredSize() const override;
		int GetNativeType() const override;
		void SyncNativeStyle() override;
		void OnNativeEvent(int event, float value) override;
	};


	class LYNX_API CheckBox : public Widget
	{
	public:
		bool checked = false;
		vec4 background_color{ 0.15f, 0.15f, 0.16f, 1.f };
		vec4 checked_color{ 0.85f, 0.6f, 0.17f, 1.f };
		float box_size = 24.f;
		bool interactive = true;

		/** New state (clicked by the user). */
		HEventDispatcher<bool> OnCheckStateChanged;

		CheckBox();

		bool IsChecked() const { return checked; }
		void SetChecked(bool c) { checked = c; }

	protected:
		vec2 ComputeDesiredSize() const override;
		int GetNativeType() const override;
		void SyncNativeStyle() override;
		void OnNativeEvent(int event, float value) override;
	};


	class LYNX_API ProgressBar : public Widget
	{
	public:
		/** 0..1 */
		float percent = 0.5f;
		vec4 background_color{ 0.15f, 0.15f, 0.16f, 1.f };
		vec4 fill_color{ 0.85f, 0.6f, 0.17f, 1.f };

		ProgressBar();

		void SetPercent(float p) { percent = p; }

	protected:
		vec2 ComputeDesiredSize() const override;
		int GetNativeType() const override;
		void SyncNativeStyle() override;
	};


	/** Empty space (in a box). */
	class LYNX_API Spacer : public Widget
	{
	public:
		vec2 size{ 16.f, 16.f };

		Spacer();

	protected:
		vec2 ComputeDesiredSize() const override { return size; }
	};
}
