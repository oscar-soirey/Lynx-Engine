#include "CommonWidgets.h"
#include "Panels.h"

#include "Private/WidgetNative.h"

#include <algorithm>

namespace lynx
{
	namespace
	{
		// The renderer can not measure a text : estimate from the size (the
		// editor font is monospaced). Several lines : '\n'.
		vec2 EstimateTextSize(const std::string& text, float size)
		{
			int lines = 1;
			int longest = 0;
			int current = 0;
			for (unsigned char c : text)
			{
				if (c == '\n')
				{
					++lines;
					longest = std::max(longest, current);
					current = 0;
				}
				else if ((c & 0xC0) != 0x80)    // one per UTF-8 character
				{
					++current;
				}
			}
			longest = std::max(longest, current);
			return { static_cast<float>(longest) * size * 0.6f, static_cast<float>(lines) * size * 1.25f };
		}

		void SetColor(void (*fn)(HRL_id, float, float, float, float), HRL_id id, const vec4& c, float alpha)
		{
			fn(id, c.x, c.y, c.z, c.w * alpha);
		}
	}


	// =========================================================================
	// TextBlock
	// =========================================================================

	TextBlock::TextBlock()
	{
		HPROPERTY(text, Exposed, Invalidate());
		HPROPERTY(font_size, Exposed, Invalidate());
		HPROPERTY(font, Exposed, Invalidate());
		HPROPERTY(color, Exposed, Invalidate());
	}

	vec2 TextBlock::ComputeDesiredSize() const
	{
		return EstimateTextSize(text, font_size);
	}

	int TextBlock::GetNativeType() const
	{
		return HRL_WIDGET_LABEL;
	}

	void TextBlock::SyncNativeStyle()
	{
		const HRL_id id = GetNative()->id;
		// Like the Widget Editor : real size, from the top-left corner, clipped.
		HRL_SetLabelTextLayout(id, HRL_TEXT_LAYOUT_TOP_LEFT);
		HRL_SetLabelText(id, text.c_str());
		HRL_SetLabelTextSize(id, font_size * GetDPIScale());
		HRL_SetLabelTintColor(id, color.x, color.y, color.z, color.w);
		const HRL_id f = ui::native::Font(font);
		if (f != HRL_INVALID_ID)
			HRL_SetLabelFont(id, f);
	}


	// =========================================================================
	// Image
	// =========================================================================

	Image::Image()
	{
		HPROPERTY(texture, Exposed, Invalidate());
		HPROPERTY(tint, Exposed, Invalidate());
		HPROPERTY(image_size, Exposed, Invalidate());
	}

	vec2 Image::ComputeDesiredSize() const
	{
		return image_size;
	}

	int Image::GetNativeType() const
	{
		return HRL_WIDGET_IMAGE;
	}

	void Image::SyncNativeStyle()
	{
		const HRL_id id = GetNative()->id;
		HRL_SetImageTexture(id, ui::native::Texture(texture));
		HRL_SetImageTintColor(id, tint.x, tint.y, tint.z, tint.w);
	}


	// =========================================================================
	// Border (layout : Panels.cpp)
	// =========================================================================

	int Border::GetNativeType() const
	{
		return HRL_WIDGET_IMAGE;
	}

	void Border::SyncNativeStyle()
	{
		const HRL_id id = GetNative()->id;
		HRL_SetImageTexture(id, ui::native::Texture(brush_texture));
		HRL_SetImageTintColor(id, brush_color.x, brush_color.y, brush_color.z, brush_color.w);
	}


	// =========================================================================
	// Button
	// =========================================================================

	Button::Button()
	{
		HPROPERTY(text, Exposed, Invalidate());
		HPROPERTY(font_size, Exposed, Invalidate());
		HPROPERTY(font, Exposed, Invalidate());
		HPROPERTY(text_color, Exposed, Invalidate());
		HPROPERTY(normal_color, Exposed, Invalidate());
		HPROPERTY(hovered_color, Exposed, Invalidate());
		HPROPERTY(pressed_color, Exposed, Invalidate());
		HPROPERTY(normal_texture, Exposed, Invalidate());
		HPROPERTY(hovered_texture, Exposed, Invalidate());
		HPROPERTY(pressed_texture, Exposed, Invalidate());
		HPROPERTY(padding, Exposed, Invalidate());
	}

	vec2 Button::ComputeDesiredSize() const
	{
		const vec2 text_size = EstimateTextSize(text, font_size);
		return { text_size.x + padding.x + padding.z, text_size.y + padding.y + padding.w };
	}

	int Button::GetNativeType() const
	{
		return HRL_WIDGET_BUTTON;
	}

	void Button::SyncNativeStyle()
	{
		const WidgetNative* native = GetNative();
		const HRL_id id = native->id;

		// Like the Widget Editor : real size, centered, clipped.
		HRL_SetButtonTextLayout(id, HRL_TEXT_LAYOUT_CENTER);
		HRL_SetButtonText(id, text.c_str());
		HRL_SetButtonTextSize(id, font_size * GetDPIScale());
		const HRL_id f = ui::native::Font(font);
		if (f != HRL_INVALID_ID)
			HRL_SetButtonTextFont(id, f);

		struct State { HRL_EWidgetState state; const vec4* color; const std::string* texture; };
		const State states[] = {
			{ HRL_WIDGET_STATE_IDLE, &normal_color, &normal_texture },
			{ HRL_WIDGET_STATE_HOVERED, &hovered_color, &hovered_texture },
			{ HRL_WIDGET_STATE_PRESSED, &pressed_color, &pressed_texture },
		};

		for (const State& s : states)
		{
			HRL_SetButtonTextTintColor(id, s.state, text_color.x, text_color.y, text_color.z, text_color.w);
			HRL_SetButtonBackgroundTexture(id, s.state, ui::native::Texture(*s.texture));
			HRL_SetButtonBackgroundTintColor(id, s.state, s.color->x, s.color->y, s.color->z, s.color->w);
		}

		HRL_SetButtonClickable(id, native->interactive ? 1 : 0);
	}

	void Button::OnNativeEvent(int event, float)
	{
		switch (event)
		{
		case ui::native::kPressed:
			OnPressed.Call();
			break;
		case ui::native::kReleased:
			OnReleased.Call();
			OnClicked.Call();
			break;
		case ui::native::kHovered:
			OnHovered.Call();
			break;
		case ui::native::kUnhovered:
			OnUnhovered.Call();
			break;
		default:
			break;
		}
	}


	// =========================================================================
	// Slider
	// =========================================================================

	Slider::Slider()
	{
		HPROPERTY(value, Exposed, Invalidate());
		HPROPERTY(min_value, Exposed, Invalidate());
		HPROPERTY(max_value, Exposed, Invalidate());
		HPROPERTY(orientation, Exposed, Invalidate());
		HPROPERTY(bar_color, Exposed, Invalidate());
		HPROPERTY(fill_color, Exposed, Invalidate());
		HPROPERTY(handle_color, Exposed, Invalidate());
		HPROPERTY(interactive, Exposed, Invalidate());
	}

	vec2 Slider::ComputeDesiredSize() const
	{
		return orientation == static_cast<int>(EOrientation::Vertical) ? vec2{ 24.f, 200.f } : vec2{ 200.f, 24.f };
	}

	int Slider::GetNativeType() const
	{
		return HRL_WIDGET_SLIDER;
	}

	void Slider::SyncNativeStyle()
	{
		const WidgetNative* native = GetNative();
		const HRL_id id = native->id;

		HRL_SetSliderRange(id, min_value, max_value);
		HRL_SetSliderValue(id, value);
		HRL_SetSliderOrientation(id, orientation == static_cast<int>(EOrientation::Vertical)
		                                 ? HRL_SLIDER_VERTICAL : HRL_SLIDER_HORIZONTAL);
		HRL_SetSliderClickable(id, interactive && native->interactive ? 1 : 0);
		SetColor(HRL_SetSliderBackgroundColor, id, bar_color, 1.f);
		SetColor(HRL_SetSliderFillColor, id, fill_color, 1.f);
		SetColor(HRL_SetSliderHandleColor, id, handle_color, 1.f);
	}

	void Slider::OnNativeEvent(int event, float new_value)
	{
		if (event != ui::native::kValueChanged || new_value == value)
			return;

		value = new_value;
		OnValueChanged.Call(new_value);
	}


	// =========================================================================
	// CheckBox
	// =========================================================================

	CheckBox::CheckBox()
	{
		HPROPERTY(checked, Exposed, Invalidate());
		HPROPERTY(background_color, Exposed, Invalidate());
		HPROPERTY(checked_color, Exposed, Invalidate());
		HPROPERTY(box_size, Exposed, Invalidate());
		HPROPERTY(interactive, Exposed, Invalidate());
	}

	vec2 CheckBox::ComputeDesiredSize() const
	{
		return { box_size, box_size };
	}

	int CheckBox::GetNativeType() const
	{
		return HRL_WIDGET_CHECKBOX;
	}

	void CheckBox::SyncNativeStyle()
	{
		const WidgetNative* native = GetNative();
		const HRL_id id = native->id;

		HRL_SetCheckboxChecked(id, checked ? 1 : 0);
		HRL_SetCheckboxClickable(id, interactive && native->interactive ? 1 : 0);
		SetColor(HRL_SetCheckboxBackgroundColor, id, background_color, 1.f);
		SetColor(HRL_SetCheckboxCheckedColor, id, checked_color, 1.f);
	}

	void CheckBox::OnNativeEvent(int event, float new_value)
	{
		const bool new_checked = new_value > 0.5f;
		if (event != ui::native::kCheckChanged || new_checked == checked)
			return;

		checked = new_checked;
		OnCheckStateChanged.Call(new_checked);
	}


	// =========================================================================
	// ProgressBar
	// =========================================================================

	ProgressBar::ProgressBar()
	{
		HPROPERTY(percent, Exposed, Invalidate());
		HPROPERTY(background_color, Exposed, Invalidate());
		HPROPERTY(fill_color, Exposed, Invalidate());
	}

	vec2 ProgressBar::ComputeDesiredSize() const
	{
		return { 200.f, 20.f };
	}

	int ProgressBar::GetNativeType() const
	{
		return HRL_WIDGET_PROGRESSBAR;
	}

	void ProgressBar::SyncNativeStyle()
	{
		const HRL_id id = GetNative()->id;
		HRL_SetProgressBarValue(id, std::clamp(percent, 0.f, 1.f));
		SetColor(HRL_SetProgressBarBackgroundColor, id, background_color, 1.f);
		SetColor(HRL_SetProgressBarFillColor, id, fill_color, 1.f);
	}


	// =========================================================================
	// Spacer
	// =========================================================================

	Spacer::Spacer()
	{
		HPROPERTY(size, Exposed, Invalidate());
	}
}
