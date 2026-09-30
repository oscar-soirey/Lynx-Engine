#pragma once

#include <QDockWidget>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QIcon>
#include <QPoint>
#include <functional>

class QFileSystemModel;
class QTreeView;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QToolButton;
class QModelIndex;
class QFileInfo;
class QEvent;
class QUrl;
class QMimeData;
class QWidget;

namespace hn::editor
{
  // Content Browser dock widget, similar in spirit to Unreal Engine 4's
  // Content Browser: a folder tree on the left, and a grid of thumbnails
  // (folders + assets) on the right, with back/forward/up navigation
  // (including mouse side buttons) and a path bar. Hovering an item shows
  // extra info via its tooltip. Right-clicking opens a context menu to
  // create/rename/delete/cut/copy/paste/duplicate entries, and the same
  // operations are available via keyboard shortcuts (Delete, Ctrl+C,
  // Ctrl+X, Ctrl+V, Ctrl+D, F2). Files can be dragged from the OS onto the
  // browser (imported, or dispatched to a registered extension action),
  // and items can be dragged within the browser onto a folder in the tree
  // or onto a folder tile in the grid to move them there.
  class ContentBrowser : public QDockWidget
  {
    Q_OBJECT

  public:
    explicit ContentBrowser(QWidget* parent = nullptr);
    ~ContentBrowser() override;

    // Loads the given directory as the browser root/current folder.
    // Resets navigation history.
    void LoadDirectory(const char* path);

    // Returns the directory currently displayed in the asset grid.
    QString CurrentPath() const { return m_CurrentPath; }

    // Registers (or overrides) the icon used for files with the given
    // extension (case-insensitive, without the leading dot, e.g. "png").
    // Extensions with no registered icon keep showing the placeholder
    // ("Editor/logo.png"). Not populated by default.
    void SetExtensionIcon(const QString& extension, const QIcon& icon);

    void SetExtensionClickedAction(const QString& extension, std::function<void(const QString&)> action);

    bool SelectExistingTab(const QString& name);

    // Registers (or overrides) an action to run when a file with the given
    // extension (case-insensitive, without the leading dot) is dropped onto
    // the browser (from the OS). The action receives the absolute path of
    // the dropped file (which stays at its original location; it is NOT
    // copied/imported into the current folder). Extensions with no
    // registered action fall back to a plain copy into the target folder.
    // Not populated by default.
    void SetExtensionAction(const QString& extension, std::function<void(const QString&)> action);

    void OpenAsset(const QString& path);

  signals:
    // Emitted when the user double-clicks a non-folder asset item.
    // path is the absolute path of the file.
    void AssetActivated(const QString& path);

    // Emitted whenever the displayed directory changes.
    void DirectoryChanged(const QString& path);

  protected:
    // Catches: the mouse "Back"/"Forward" side buttons; keyboard shortcuts
    // while the asset grid has focus; the start of an internal item drag;
    // and drag-and-drop (external files, or internal items) over the asset
    // grid or the folder tree.
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:
    void OnFolderTreeClicked(const QModelIndex& index);
    void OnAssetDoubleClicked(QListWidgetItem* item);
    void OnNavigateBack();
    void OnNavigateForward();
    void OnNavigateUp();
    void OnPathEditReturnPressed();
    void OnAssetGridContextMenuRequested(const QPoint& pos);

  private:
    void SetupUi();
    void RefreshAssetGrid();
    void NavigateTo(const QString& path, bool addToHistory = true);
    void UpdateNavigationButtons();

    QListWidgetItem* CreateFolderItem(const QFileInfo& info) const;
    QListWidgetItem* CreateAssetItem(const QFileInfo& info) const;
    QIcon GetIconForFile(const QFileInfo& info) const;

    // Context menu / keyboard-shortcut actions.
    void CreateNewFolder();
    void CreateNewFile();
    void RenameItem(QListWidgetItem* item);
    void DeleteItems(const QList<QListWidgetItem*>& items);
    void CopySelection();
    void CutSelection();
    void PasteClipboard();
    void DuplicateItems(const QList<QListWidgetItem*>& items);

    QString GenerateUniqueEntryName(const QString& baseName) const;
    QString GenerateUniqueFileName(const QString& folder, const QString& fileName) const;
    bool CopyEntryRecursively(const QString& sourcePath, const QString& destPath) const;

    // Drag-and-drop.
    void StartInternalDrag();
    void HandleDrop(const QMimeData* mimeData, const QString& targetFolder, bool isInternalDrag);
    QString FolderForDropPosition(QWidget* watched, const QPoint& pos) const;

  private:
    // Left pane: folder tree.
    QTreeView* m_FolderTree = nullptr;
    QFileSystemModel* m_FolderModel = nullptr;

    // Right pane: asset grid.
    QListWidget* m_AssetGrid = nullptr;

    // Toolbar.
    QLineEdit* m_PathEdit = nullptr;
    QToolButton* m_BackButton = nullptr;
    QToolButton* m_ForwardButton = nullptr;
    QToolButton* m_UpButton = nullptr;

    QString m_CurrentPath;
    QStringList m_History;
    int m_HistoryIndex = -1;

    // extension (lowercase, no dot) -> icon. Empty by default; entries not
    // present here fall back to the placeholder icon.
    QHash<QString, QIcon> m_ExtensionIcons;

    QHash<QString, std::function<void(const QString&)>> m_ExtensionClickedActions;

    // extension (lowercase, no dot) -> handler run on drop instead of
    // copying the file in. Empty by default.
    QHash<QString, std::function<void(const QString&)>> m_ExtensionActions;

    // Internal clipboard (paths) for Ctrl+C / Ctrl+X / Ctrl+V.
    QStringList m_ClipboardPaths;
    bool m_ClipboardCut = false;

    // Internal item-drag detection state.
    QPoint m_DragStartPos;
    bool m_DragCandidate = false;

    static const QString kPlaceholderIconPath;
    static const QString kInternalDragMimeType;
    static const int kThumbnailSize;
  };
}