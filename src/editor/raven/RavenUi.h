#pragma once
// Lynx Editor (Raven) : a thin C++ layer over the C API of Raven.
//
//   - the files of the interface (XML, themes, icons, fonts) are embedded in
//     the executable (cmake/LynxEmbedFiles.cmake) and registered as memory
//     files : Rwidget_create_from_xml_file("ui/outliner.xml") reads them.
//     LYNX_UI_DIR=<folder of src/editor/ui> reads them from disk instead,
//     with live reload while the editor runs (to work on the interface) ;
//   - callbacks are std::function, owned by the widget they are bound to
//     (freed when it is destroyed) ;
//   - <Icon Glyph="Play"/> and <FileIcon Kind="Folder"/> in the XML : the
//     pixel-art atlases of the editor ;
//   - Menu : context menus built on demand ;
//   - input : the state of the keys and buttons, as the editor polls them
//     (the codes are the GLFW ones the editor and the engine use).

#include <Raven.h>

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "../EditorIcons.h"
#include "../FileIcons.h"

namespace lynx::editor::ui
{
    // Types of the drags inside the editor (Rdrag_begin) :
    //   lynx.asset.path   files of the Content Browser, paths separated by '\n'
    //   lynx.actor.class  a class of Place Actors (its name)
    //   lynx.outliner     actors / folders of the Outliner (tree items)
    constexpr const char* kDragAsset = "lynx.asset.path";
    constexpr const char* kDragActorClass = "lynx.actor.class";
    constexpr const char* kDragOutliner = "lynx.outliner";

    // ---- start / stop -------------------------------------------------------

    // After Rinit : embedded files, <Icon>, <FileIcon>, themes, fonts.
    void Init();
    void Shutdown();

    // Folder the files come from when LYNX_UI_DIR is set ("" : embedded).
    const std::string& DiskFolder();

    // "outliner.xml" -> the path to give to Raven ("ui/outliner.xml", or the
    // file on disk with LYNX_UI_DIR).
    std::string Path(const char* file);

    // A widget (or a window) built from an interface file of the editor.
    Rid Load(const char* file);
    Rid LoadWindow(const char* file);

    // Rwidget_find, with an error in the console when the name is missing
    // (a mistake in an XML file must not go unnoticed).
    Rid Find(Rid scope, const char* name);

    // ---- themes ---------------------------------------------------------------

    // `file` : the theme file of the user ("" for the themes of the editor).
    struct ThemeInfo { std::string name; std::string label; std::filesystem::path file; };
    // Themes of the editor : Pixel (Aseprite, the default), Pixel Dark, Lynx (warm dark), Dark, Light,
    // then the user's own themes (every .xml of UserThemesFolder()).
    const std::vector<ThemeInfo>& Themes();
    void UseTheme(const std::string& name);

    // Themes written by the user : %APPDATA%/Lynx/themes (~/.config/lynx/themes).
    std::filesystem::path UserThemesFolder();
    // Loads the user themes again (new, removed or edited files). true when
    // something changed (the list or a file).
    bool ReloadUserThemes();
    // A new theme file in UserThemesFolder(), from themes/template.xml, with a
    // free name ("My Theme", "My Theme 2"...). Its path, empty on error.
    std::filesystem::path CreateUserTheme();

    // ---- callbacks owned by the widgets --------------------------------------

    void OnClick(Rid button, std::function<void()> fn);
    void OnToggle(Rid checkbox, std::function<void(bool)> fn);
    void OnValue(Rid spin_box_or_slider, std::function<void(double)> fn);
    void OnSelect(Rid combo_box, std::function<void(int)> fn);
    void OnText(Rid text_input, std::function<void(const std::string&)> fn);       // each keystroke
    void OnCommit(Rid text_input, std::function<void(const std::string&)> fn);     // Enter / focus lost
    void OnMenuItem(Rid item, std::function<void()> fn);
    void OnColor(Rid color_picker, std::function<void(float, float, float, float)> fn);
    void OnExpand(Rid expander, std::function<void(bool)> fn);
    void OnTab(Rid tab_view, std::function<void(int)> fn);
    // true : the event is used (not passed to the widget and its parents).
    void OnEvent(Rid widget, std::function<bool(const REvent&)> fn);
    // true : the drop is taken here.
    void OnDrop(Rid widget, std::function<bool(const RDropEvent&)> fn);
    void OnTreeSelect(Rid tree, std::function<void(Rid item)> fn);
    void OnTreeActivate(Rid tree, std::function<void(Rid item)> fn);
    void OnTreeContext(Rid tree, std::function<void(Rid item, float x, float y)> fn);
    void OnTileItem(Rid tiles, int count, std::function<void(int index, RTileItem& item)> fn);
    void OnTileSelect(Rid tiles, std::function<void(int index)> fn);
    void OnTileActivate(Rid tiles, std::function<void(int index)> fn);
    void OnTileContext(Rid tiles, std::function<void(int index, float x, float y)> fn);
    // The data of a drag of tiles (the selected ones) : paths...
    void OnTileDragData(Rid tiles, std::function<std::string()> fn);
    void OnListActivate(Rid list, std::function<void(int index)> fn);
    void OnListSelect(Rid list, std::function<void(int index)> fn);
    void OnPaint(Rid custom, std::function<void(RPainter*, float w, float h)> fn);
    // Raven actions (OnClicked="editor.save" in the XML).
    void Action(const char* name, std::function<void()> fn);

    // One function when the widget is destroyed (the panels forget it).
    void OnDestroy(Rid widget, std::function<void()> fn);

    // Calls `fn` every `seconds` (UI thread). The timer lives until Shutdown.
    void Every(double seconds, std::function<void()> fn);
    // Once, at the next frame (after the current event).
    void Later(std::function<void()> fn);

    // ---- small helpers ----------------------------------------------------------

    // Only when it changed (keeps Raven from laying out again every frame).
    void SetText(Rid label_or_button, const std::string& text);
    void SetVisible(Rid widget, bool visible);       // Collapsed when hidden
    void SetEnabled(Rid widget, bool enabled);
    void SetChecked(Rid checkbox, bool checked);
    void SetClass(Rid widget, const char* style_class, bool on);
    void SetTooltip(Rid widget, const std::string& text);
    void SetValue(Rid spin_box, double value);       // not while the user edits it

    // ---- icons -----------------------------------------------------------------

    // Texture of an atlas and the region of one icon (u0, v0, u1, v1).
    Rid IconTexture();
    void IconRegion(icons::Icon icon, float uv[4]);
    Rid FileIconTexture();
    void FileIconRegion(file_icons::Kind kind, float uv[4]);
    // An image of the editor (resources : "lynxie.png"...), or of the project.
    Rid Texture(const std::string& path);
    // A texture from a file of the project (thumbnails), loaded once ; R_INVALID_ID when it is not an image.
    Rid FileTexture(const std::filesystem::path& path);
    // An Image widget showing an editor icon.
    Rid MakeIcon(icons::Icon icon, float size);
    // Another icon of the atlas in an <Icon> / MakeIcon image (Play -> Stop...).
    void SetIcon(Rid image, icons::Icon icon);
    // Editor icons on the tab of a docked panel, and before the text of a menu item.
    void SetPanelIcon(Rid dock, Rid panel, icons::Icon icon);
    void SetMenuIcon(Rid item, icons::Icon icon);
    void SetMenuFileIcon(Rid item, file_icons::Kind kind);
    icons::Icon IconFromName(const char* name, bool* found = nullptr);

    // ---- context menus -----------------------------------------------------------

    class Menu
    {
    public:
        // A menu for this window ; Open shows it at the pointer (or at x, y).
        explicit Menu(Rid window);
        ~Menu();
        Menu(const Menu&) = delete;
        Menu& operator=(const Menu&) = delete;

        // Built again before each Open (the items are destroyed).
        void Clear();
        Rid Item(const std::string& text, std::function<void()> fn, bool enabled = true,
                 const char* shortcut = nullptr);
        Rid Check(const std::string& text, bool checked, std::function<void()> fn, bool enabled = true);
        Rid Title(const std::string& text);     // a disabled line
        void Separator();
        // A sub menu ; fill it with the returned Menu (owned by this one).
        Menu& Sub(const std::string& text);

        void Open();
        void Open(float x, float y);
        bool Empty() const { return items_ == 0; }
        Rid Handle() const { return menu_; }

    private:
        explicit Menu(Rid window, Rid menu);
        Rid window_ = R_INVALID_ID;
        Rid menu_ = R_INVALID_ID;
        int items_ = 0;
        std::vector<Menu*> subs_;
    };

    // ---- dialogs ---------------------------------------------------------------

    // A message with an OK button (not blocking).
    void Message(Rid window, const std::string& title, const std::string& text);

    // A dialog with buttons ; `done` gets the index of the button (-1 : closed with Escape).
    void Ask(Rid window, const std::string& title, const std::string& text,
             const std::vector<std::string>& buttons, std::function<void(int)> done);

    // A dialog asking for a name ; `done` gets it (not called on Cancel).
    // `check` (optional) : "" when the name is fine, else why it is not.
    void AskText(Rid window, const std::string& title, const std::string& label, const std::string& initial,
                 std::function<void(const std::string&)> done,
                 std::function<std::string(const std::string&)> check = {});
}


namespace lynx::editor::input
{
    // The window whose keys are read. Keeps the deltas of the mouse and the
    // presses of this frame (NewFrame clears them).
    void Attach(Rid window);
    Rid Window();
    // Every input event of the window (its event hook calls it), then NewFrame once per frame.
    void Feed(const REvent& event);
    void NewFrame();

    // GLFW key codes (GLFW_KEY_W...) ; the editor code was written with them.
    bool Key(int glfw_key);
    bool KeyPressed(int glfw_key);     // went down this frame
    // GLFW mouse buttons : 0 left, 1 right, 2 middle.
    bool Mouse(int button);
    bool MouseClicked(int button);     // went down this frame
    bool MouseReleased(int button);    // went up this frame
    // Distance the pointer travelled since the button went down (logical units).
    float MouseDragDistance(int button);
    // Pointer in the window, logical units (as the widgets are placed).
    void MousePosition(double& x, double& y);
    // Movement of the pointer this frame.
    void MouseDelta(float& dx, float& dy);
    float Wheel();                      // this frame

    bool Ctrl();
    bool Shift();
    bool Alt();

    // A text field / text editor has the keyboard.
    bool WantTextInput();

    // GLFW <-> Raven key codes.
    int ToGlfw(RKey key);
    RKey FromGlfw(int glfw_key);
}
