#pragma once

#include "../core/Common.h"
#include <vector>
#include <cstdint>

namespace lynx
{
  // Toutes les valeurs (positions, tailles, padding, spacing) sont normalisées
  // par rapport au viewport propriétaire, comme dans l'API UI de HRL.
  struct WidgetSize    { float width = 0.f, height = 0.f; };
  struct WidgetPadding { float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f; };

  // Alignement sur l'axe perpendiculaire à celui de la boîte
  // (horizontal pour une VerticalBox, vertical pour une HorizontalBox).
  enum class BoxAlignment { Start, Center, End, Stretch };

  // Possède ses widgets : c'est lui qui les positionne, les dimensionne et
  // (par défaut) les détruit. Les appels HRL se font uniquement dans le .cpp.
  struct LYNX_API WidgetContainer {
    // Équivaut à HRL_INVALID_ID (vérifié par un static_assert dans le .cpp).
    static constexpr uint32_t kInvalidWidget = 0xFFFFFFFFu;

    WidgetContainer() = default;
    virtual ~WidgetContainer();
    WidgetContainer(const WidgetContainer&) = delete;
    WidgetContainer& operator=(const WidgetContainer&) = delete;
    WidgetContainer(WidgetContainer&&) = delete;
    WidgetContainer& operator=(WidgetContainer&&) = delete;

    // --- Enfants --------------------------------------------------------
    // width/height : taille souhaitée du widget (HRL n'a pas de getter, donc
    // le container la mémorise). fill > 0 : l'enfant prend une part de
    // l'espace restant sur l'axe principal (proportionnelle à fill) et sa
    // taille souhaitée sur cet axe est ignorée.
    void     AddWidget(uint32_t w, float width, float height, float fill = 0.f);
    bool     RemoveWidget(uint32_t w);   // détruit le widget si OwnsChildren()
    void     ClearChildren();            // idem
    bool     SetChildSize(uint32_t w, float width, float height);
    bool     SetChildFill(uint32_t w, float fill);
    uint32_t GetChildAt(int index) const;
    size_t   GetChildCount() const;

    // Si true (défaut), les widgets enfants sont détruits avec HRL_DeleteWidget
    // lors du retrait, du ClearChildren() et de la destruction du container.
    void SetOwnsChildren(bool owns);
    bool OwnsChildren() const;

    // --- Rectangle du container ------------------------------------------
    void SetPosition(float x, float y);
    void SetSize(float width, float height);
    void SetRect(float x, float y, float width, float height);
    // Si activé, la taille du container suit celle de son contenu.
    void SetAutoSize(bool autoSize);
    float GetX() const;
    float GetY() const;
    float GetWidth() const;
    float GetHeight() const;

    // Taille nécessaire pour contenir tous les enfants.
    virtual WidgetSize GetDesiredSize() const;

    // Recalcule et applique position/taille de chaque enfant. Appelé
    // automatiquement après chaque modification ; à rappeler soi-même après
    // un redimensionnement de fenêtre (HRL garde la taille en pixels des
    // widgets, donc les valeurs normalisées doivent être recalculées).
    void UpdateLayout();

  protected:
    struct Slot {
      uint32_t widget;
      float    width;
      float    height;
      float    fill;
    };

    virtual void DoLayout();
    std::vector<Slot> slots_;

  private:
    float x_ = 0.f, y_ = 0.f, width_ = 0.f, height_ = 0.f;
    bool  autoSize_     = false;
    bool  ownsChildren_ = true;
  };

  // Base commune de VerticalBox / HorizontalBox.
  struct LYNX_API BoxContainer : WidgetContainer {
    void          SetSpacing(float spacing);
    float         GetSpacing() const;
    void          SetPadding(const WidgetPadding& padding);
    WidgetPadding GetPadding() const;
    void          SetCrossAlignment(BoxAlignment alignment);
    BoxAlignment  GetCrossAlignment() const;

    WidgetSize GetDesiredSize() const override;

  protected:
    explicit BoxContainer(bool vertical) : vertical_(vertical) {}
    void DoLayout() override;

  private:
    bool          vertical_;
    float         spacing_   = 0.f;
    WidgetPadding padding_;
    BoxAlignment  alignment_ = BoxAlignment::Start;
  };

  struct LYNX_API VerticalBox : BoxContainer {
    VerticalBox() : BoxContainer(true) {}
  };

  struct LYNX_API HorizontalBox : BoxContainer {
    HorizontalBox() : BoxContainer(false) {}
  };
}
