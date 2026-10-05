Certaines libraries third-party ont étés modifiées,

ImGui: le systeme de rendu a été modifié

ImNodes (imgui_node/) : style « pixel » assorti à ImGui (grille papier millimétré qui suit le
panoramique, nœuds = petites fenêtres avec contour 1px et barre de titre en relief, pins carrés /
ronds en pixel art, couleurs prises dans le thème ImGui : ImNodes::StyleColorsPixel / StylePixel).
Modifications marquées [PIXEL STYLE], désactivées avec IMGUI_DISABLE_PIXEL_STYLE ;
imgui_node/imnodes_pixel.patch contient le diff par rapport à imnodes d'origine.
