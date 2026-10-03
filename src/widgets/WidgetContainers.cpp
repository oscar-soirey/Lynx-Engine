#include "WidgetContainers.h"
#include <hrl/hrl.h>          // adapter le chemin si besoin
#include <algorithm>

namespace lynx
{
  static_assert(WidgetContainer::kInvalidWidget == HRL_INVALID_ID,
                "kInvalidWidget doit rester égal à HRL_INVALID_ID");

  // ---------------------------------------------------------------- WidgetContainer

  WidgetContainer::~WidgetContainer()
  {
    ClearChildren();
  }

  void WidgetContainer::AddWidget(uint32_t w, float width, float height, float fill)
  {
    if (w == kInvalidWidget)
      return;
    slots_.push_back({ w, std::max(0.f, width), std::max(0.f, height), std::max(0.f, fill) });
    UpdateLayout();
  }

  bool WidgetContainer::RemoveWidget(uint32_t w)
  {
    auto it = std::find_if(slots_.begin(), slots_.end(),
                           [w](const Slot& s) { return s.widget == w; });
    if (it == slots_.end())
      return false;
    if (ownsChildren_)
      HRL_DeleteWidget(it->widget);
    slots_.erase(it);
    UpdateLayout();
    return true;
  }

  void WidgetContainer::ClearChildren()
  {
    if (ownsChildren_) {
      for (const Slot& s : slots_)
        HRL_DeleteWidget(s.widget);
    }
    slots_.clear();
    UpdateLayout();
  }

  bool WidgetContainer::SetChildSize(uint32_t w, float width, float height)
  {
    for (Slot& s : slots_) {
      if (s.widget == w) {
        s.width  = std::max(0.f, width);
        s.height = std::max(0.f, height);
        UpdateLayout();
        return true;
      }
    }
    return false;
  }

  bool WidgetContainer::SetChildFill(uint32_t w, float fill)
  {
    for (Slot& s : slots_) {
      if (s.widget == w) {
        s.fill = std::max(0.f, fill);
        UpdateLayout();
        return true;
      }
    }
    return false;
  }

  uint32_t WidgetContainer::GetChildAt(int index) const
  {
    if (index < 0 || static_cast<size_t>(index) >= slots_.size())
      return kInvalidWidget;
    return slots_[static_cast<size_t>(index)].widget;
  }

  size_t WidgetContainer::GetChildCount() const { return slots_.size(); }

  void WidgetContainer::SetOwnsChildren(bool owns) { ownsChildren_ = owns; }
  bool WidgetContainer::OwnsChildren() const { return ownsChildren_; }

  void WidgetContainer::SetPosition(float x, float y)
  {
    x_ = x; y_ = y;
    UpdateLayout();
  }

  void WidgetContainer::SetSize(float width, float height)
  {
    width_  = std::max(0.f, width);
    height_ = std::max(0.f, height);
    UpdateLayout();
  }

  void WidgetContainer::SetRect(float x, float y, float width, float height)
  {
    x_ = x; y_ = y;
    width_  = std::max(0.f, width);
    height_ = std::max(0.f, height);
    UpdateLayout();
  }

  void WidgetContainer::SetAutoSize(bool autoSize)
  {
    autoSize_ = autoSize;
    UpdateLayout();
  }

  float WidgetContainer::GetX() const { return x_; }
  float WidgetContainer::GetY() const { return y_; }
  float WidgetContainer::GetWidth() const { return width_; }
  float WidgetContainer::GetHeight() const { return height_; }

  WidgetSize WidgetContainer::GetDesiredSize() const { return {}; }

  void WidgetContainer::UpdateLayout()
  {
    if (autoSize_) {
      const WidgetSize s = GetDesiredSize();
      width_  = s.width;
      height_ = s.height;
    }
    DoLayout();
  }

  void WidgetContainer::DoLayout() {}

  // ---------------------------------------------------------------- BoxContainer

  void BoxContainer::SetSpacing(float spacing)
  {
    spacing_ = std::max(0.f, spacing);
    UpdateLayout();
  }

  float BoxContainer::GetSpacing() const { return spacing_; }

  void BoxContainer::SetPadding(const WidgetPadding& padding)
  {
    padding_ = padding;
    UpdateLayout();
  }

  WidgetPadding BoxContainer::GetPadding() const { return padding_; }

  void BoxContainer::SetCrossAlignment(BoxAlignment alignment)
  {
    alignment_ = alignment;
    UpdateLayout();
  }

  BoxAlignment BoxContainer::GetCrossAlignment() const { return alignment_; }

  WidgetSize BoxContainer::GetDesiredSize() const
  {
    float mainTotal = 0.f;
    float crossMax  = 0.f;

    for (const Slot& s : slots_) {
      mainTotal += vertical_ ? s.height : s.width;
      crossMax   = std::max(crossMax, vertical_ ? s.width : s.height);
    }
    if (slots_.size() > 1)
      mainTotal += spacing_ * static_cast<float>(slots_.size() - 1);

    WidgetSize out;
    out.width  = (vertical_ ? crossMax : mainTotal) + padding_.left + padding_.right;
    out.height = (vertical_ ? mainTotal : crossMax) + padding_.top + padding_.bottom;
    return out;
  }

  void BoxContainer::DoLayout()
  {
    if (slots_.empty())
      return;

    const float innerX = GetX() + padding_.left;
    const float innerY = GetY() + padding_.top;
    const float innerW = std::max(0.f, GetWidth()  - padding_.left - padding_.right);
    const float innerH = std::max(0.f, GetHeight() - padding_.top  - padding_.bottom);

    const float mainAvail  = vertical_ ? innerH : innerW;
    const float crossAvail = vertical_ ? innerW : innerH;

    // Espace restant à répartir entre les enfants "fill".
    float fixedTotal = 0.f;
    float fillTotal  = 0.f;
    for (const Slot& s : slots_) {
      if (s.fill > 0.f) fillTotal += s.fill;
      else              fixedTotal += vertical_ ? s.height : s.width;
    }
    const float spacingTotal = spacing_ * static_cast<float>(slots_.size() - 1);
    const float remaining    = std::max(0.f, mainAvail - fixedTotal - spacingTotal);

    float cursor = vertical_ ? innerY : innerX;

    for (const Slot& s : slots_) {
      const float mainSize = (s.fill > 0.f)
        ? remaining * s.fill / fillTotal
        : (vertical_ ? s.height : s.width);

      float crossSize   = vertical_ ? s.width : s.height;
      float crossOffset = 0.f;

      switch (alignment_) {
        case BoxAlignment::Start:   break;
        case BoxAlignment::Center:  crossOffset = (crossAvail - crossSize) * 0.5f; break;
        case BoxAlignment::End:     crossOffset = crossAvail - crossSize; break;
        case BoxAlignment::Stretch: crossSize = crossAvail; break;
      }
      crossOffset = std::max(0.f, crossOffset);

      const float px = vertical_ ? innerX + crossOffset : cursor;
      const float py = vertical_ ? cursor : innerY + crossOffset;
      const float pw = vertical_ ? crossSize : mainSize;
      const float ph = vertical_ ? mainSize : crossSize;

      // Ancre en haut à gauche : la position HRL correspond au coin du widget.
      HRL_SetWidgetAnchor(s.widget, 0.f, 0.f);
      HRL_SetWidgetPosition(s.widget, px, py);
      HRL_SetWidgetSize(s.widget, pw, ph);

      cursor += mainSize + spacing_;
    }
  }
}
