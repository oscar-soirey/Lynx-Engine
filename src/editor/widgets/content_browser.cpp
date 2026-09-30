#include "content_browser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QTreeView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QToolButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QPixmap>
#include <QIcon>
#include <QStyle>
#include <QLocale>
#include <QDateTime>
#include <QHeaderView>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDrag>

#include "../common.h"
#include "../tabs/text_editor/code_editor.h"
#include "../tabs/text_editor/text_editor_tab.h"
#include "../window/main_window.h"

namespace
{
  // Qt6 deprecates QMouseEvent/QDropEvent::pos() in favor of position();
  // these small helpers keep call sites clean on both Qt5 and Qt6.
  QPoint EventPos(const QMouseEvent* event)
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
  }

  QPoint EventPos(const QDropEvent* event)
  {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
  }
}

namespace hn::editor
{
  const QString ContentBrowser::kPlaceholderIconPath = QStringLiteral("Editor/logo.png");
  const QString ContentBrowser::kInternalDragMimeType = QStringLiteral("application/x-hn-contentbrowser-internal");
  const int ContentBrowser::kThumbnailSize = 96;

  void ContentBrowser::OpenAsset(const QString &path)
  {
    fs::path p = path.toStdString();
    QString extension = QString::fromStdString(p.extension().string().substr(1));
    if (m_ExtensionClickedActions.contains(extension))
    {
      m_ExtensionClickedActions[extension](path);
    }
  }

  ContentBrowser::ContentBrowser(QWidget* parent)
    : QDockWidget(parent)
  {
    setObjectName("ContentBrowser");
    setWindowTitle(tr("Content Browser"));

    SetupUi();

    // Installed on the application so mouse side buttons, keyboard
    // shortcuts, and drag-and-drop are caught no matter which child widget
    // (tree, grid, viewport...) is the actual event target.
    qApp->installEventFilter(this);
  }

  ContentBrowser::~ContentBrowser()
  {
    qApp->removeEventFilter(this);
  }

  bool ContentBrowser::SelectExistingTab(const QString& file_path)
  {
    QTabWidget* tabs = GetMainWindow()->GetTabWidget();
    for (int i = 0; i < tabs->count(); ++i)
    {
      if (auto* editor_tab = qobject_cast<TextEditorTab*>(tabs->widget(i)))
      {
        if (editor_tab->GetFilePath() == file_path)
        {
          tabs->setCurrentIndex(i);
          return true;
        }
      }
    }
    return false;
  }

  void ContentBrowser::SetupUi()
  {
    // --- Folder tree (left pane) ---
    m_FolderModel = new QFileSystemModel(this);
    m_FolderModel->setRootPath(QDir::rootPath());
    m_FolderModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);

    m_FolderTree = new QTreeView();
    m_FolderTree->setModel(m_FolderModel);
    m_FolderTree->setHeaderHidden(true);
    m_FolderTree->setMinimumWidth(180);
    m_FolderTree->setMaximumWidth(320);
    // Only the "Name" column is relevant for a folder-only navigation tree.
    for (int column = 1; column < m_FolderModel->columnCount(); ++column)
      m_FolderTree->hideColumn(column);

    // Accepts items dragged from the asset grid, to move them into a folder.
    m_FolderTree->setAcceptDrops(true);
    m_FolderTree->viewport()->setAcceptDrops(true);

    connect(m_FolderTree, &QTreeView::clicked, this, &ContentBrowser::OnFolderTreeClicked);

    // --- Asset grid (right pane) ---
    m_AssetGrid = new QListWidget();
    m_AssetGrid->setViewMode(QListView::IconMode);
    m_AssetGrid->setIconSize(QSize(kThumbnailSize, kThumbnailSize));
    m_AssetGrid->setResizeMode(QListView::Adjust);
    m_AssetGrid->setMovement(QListView::Static);
    m_AssetGrid->setSpacing(12);
    m_AssetGrid->setWordWrap(true);
    m_AssetGrid->setUniformItemSizes(true);
    m_AssetGrid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_AssetGrid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_AssetGrid->setMouseTracking(true); // required for hover tooltips to feel responsive
    m_AssetGrid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_AssetGrid->setFocusPolicy(Qt::StrongFocus); // so it reliably receives key events

    // Accepts files dragged in from the OS, and items dragged from itself
    // (to reparent into a subfolder tile). Handled in eventFilter() so it
    // works regardless of the item view's own internal drag/drop handling.
    m_AssetGrid->setAcceptDrops(true);
    m_AssetGrid->viewport()->setAcceptDrops(true);

    connect(m_AssetGrid, &QListWidget::itemDoubleClicked, this, &ContentBrowser::OnAssetDoubleClicked);
    connect(m_AssetGrid, &QListWidget::customContextMenuRequested,
      this, &ContentBrowser::OnAssetGridContextMenuRequested);

    // --- Toolbar (back / forward / up / path bar) ---
    m_BackButton = new QToolButton();
    m_BackButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    m_BackButton->setToolTip(tr("Back"));
    m_BackButton->setEnabled(false);
    connect(m_BackButton, &QToolButton::clicked, this, &ContentBrowser::OnNavigateBack);

    m_ForwardButton = new QToolButton();
    m_ForwardButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_ForwardButton->setToolTip(tr("Forward"));
    m_ForwardButton->setEnabled(false);
    connect(m_ForwardButton, &QToolButton::clicked, this, &ContentBrowser::OnNavigateForward);

    m_UpButton = new QToolButton();
    m_UpButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_UpButton->setToolTip(tr("Up one level"));
    m_UpButton->setEnabled(false);
    connect(m_UpButton, &QToolButton::clicked, this, &ContentBrowser::OnNavigateUp);

    m_PathEdit = new QLineEdit();
    m_PathEdit->setPlaceholderText(tr("Path..."));
    connect(m_PathEdit, &QLineEdit::returnPressed, this, &ContentBrowser::OnPathEditReturnPressed);

    QWidget* toolbar = new QWidget();
    QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 4, 4, 4);
    toolbarLayout->addWidget(m_BackButton);
    toolbarLayout->addWidget(m_ForwardButton);
    toolbarLayout->addWidget(m_UpButton);
    toolbarLayout->addWidget(m_PathEdit, 1);

    // --- Right pane container (toolbar + grid) ---
    QWidget* rightPane = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(toolbar);
    rightLayout->addWidget(m_AssetGrid, 1);

    // --- Splitter combining both panes ---
    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_FolderTree);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    setWidget(splitter);

    SetExtensionIcon("cpp", QIcon("Editor/Icons/cpp.png"));
    SetExtensionIcon("c", QIcon("Editor/Icons/c.png"));
    SetExtensionIcon("h", QIcon("Editor/Icons/c.png"));
    SetExtensionIcon("lua", QIcon("Editor/Icons/lua.png"));

    connect(
      this,
      &ContentBrowser::AssetActivated,
      this,
      [this](const QString& path)
      {
          OpenAsset(path);
      }
    );

    SetExtensionClickedAction("lua", [this](const QString& path)
    {
      if (SelectExistingTab(path))
        return;

      auto* code_editor = new TextEditorTab(GetMainWindow());
      code_editor->OpenFile(path);

      fs::path p = path.toStdString();
      int index = GetMainWindow()->GetTabWidget()->addTab(
          code_editor,
          QString::fromStdString(p.filename().string())
      );
      GetMainWindow()->GetTabWidget()->setCurrentIndex(index);
    });

    LoadDirectory(GetEditorRessources()->current_project->ressources_path.c_str());
  }

  void ContentBrowser::SetExtensionClickedAction(const QString &extension, std::function<void(const QString &)> action)
  {
    m_ExtensionClickedActions.emplace(extension, action);
  }

  void ContentBrowser::LoadDirectory(const char* path)
  {
    if (path == nullptr)
      return;

    m_History.clear();
    m_HistoryIndex = -1;

    NavigateTo(QString::fromUtf8(path));
  }

  void ContentBrowser::SetExtensionIcon(const QString& extension, const QIcon& icon)
  {
    m_ExtensionIcons.insert(extension.toLower(), icon);
  }

  void ContentBrowser::SetExtensionAction(const QString& extension, std::function<void(const QString&)> action)
  {
    m_ExtensionActions.insert(extension.toLower(), std::move(action));
  }

  bool ContentBrowser::eventFilter(QObject* watched, QEvent* event)
  {
    QWidget* widget = qobject_cast<QWidget*>(watched);
    const bool isOurs = widget != nullptr && (widget == this || this->isAncestorOf(widget));

    if (isOurs)
    {
      const bool overGrid = (widget == m_AssetGrid || m_AssetGrid->isAncestorOf(widget));
      const bool overTree = (widget == m_FolderTree || m_FolderTree->isAncestorOf(widget));

      switch (event->type())
      {
        case QEvent::MouseButtonPress:
        {
          QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

          if (mouseEvent->button() == Qt::BackButton)
          {
            OnNavigateBack();
            return true;
          }
          if (mouseEvent->button() == Qt::ForwardButton)
          {
            OnNavigateForward();
            return true;
          }

          // Remember where a potential item-drag would start; the actual
          // native click/selection handling is left untouched (we don't
          // consume this event).
          if (mouseEvent->button() == Qt::LeftButton && overGrid)
          {
            m_DragStartPos = EventPos(mouseEvent);
            m_DragCandidate = m_AssetGrid->itemAt(m_DragStartPos) != nullptr;
          }
          break;
        }

        case QEvent::MouseMove:
        {
          if (m_DragCandidate && overGrid)
          {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if ((mouseEvent->buttons() & Qt::LeftButton) &&
                (EventPos(mouseEvent) - m_DragStartPos).manhattanLength() >= QApplication::startDragDistance())
            {
              m_DragCandidate = false;
              StartInternalDrag();
              return true;
            }
          }
          break;
        }

        case QEvent::MouseButtonRelease:
        {
          m_DragCandidate = false;
          break;
        }

        case QEvent::KeyPress:
        {
          if (widget == m_AssetGrid)
          {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->key() == Qt::Key_Delete || keyEvent->matches(QKeySequence::Delete))
            {
              DeleteItems(m_AssetGrid->selectedItems());
              return true;
            }
            if (keyEvent->matches(QKeySequence::Copy))
            {
              CopySelection();
              return true;
            }
            if (keyEvent->matches(QKeySequence::Cut))
            {
              CutSelection();
              return true;
            }
            if (keyEvent->matches(QKeySequence::Paste))
            {
              PasteClipboard();
              return true;
            }
            if (keyEvent->key() == Qt::Key_D && keyEvent->modifiers() == Qt::ControlModifier)
            {
              DuplicateItems(m_AssetGrid->selectedItems());
              return true;
            }
            if (keyEvent->key() == Qt::Key_F2)
            {
              const QList<QListWidgetItem*> selected = m_AssetGrid->selectedItems();
              if (!selected.isEmpty())
                RenameItem(selected.first());
              return true;
            }
          }
          break;
        }

        case QEvent::DragEnter:
        case QEvent::DragMove:
        {
          if (overGrid || overTree)
          {
            QDropEvent* dragEvent = static_cast<QDropEvent*>(event);
            if (dragEvent->mimeData()->hasUrls())
            {
              dragEvent->acceptProposedAction();
              return true;
            }
          }
          break;
        }

        case QEvent::Drop:
        {
          if (overGrid || overTree)
          {
            QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
            if (dropEvent->mimeData()->hasUrls())
            {
              const QString targetFolder = FolderForDropPosition(widget, EventPos(dropEvent));
              const bool isInternal = dropEvent->mimeData()->hasFormat(kInternalDragMimeType);

              HandleDrop(dropEvent->mimeData(), targetFolder, isInternal);
              dropEvent->acceptProposedAction();
              return true;
            }
          }
          break;
        }

        default:
          break;
      }
    }

    return QDockWidget::eventFilter(watched, event);
  }

  void ContentBrowser::NavigateTo(const QString& path, bool addToHistory)
  {
    QDir dir(path);
    if (!dir.exists())
      return;

    m_CurrentPath = QDir::cleanPath(dir.absolutePath());

    if (addToHistory)
    {
      // Drop any "forward" history past the current point, then append.
      while (m_History.size() > m_HistoryIndex + 1)
        m_History.removeLast();

      m_History.append(m_CurrentPath);
      m_HistoryIndex = m_History.size() - 1;
    }

    m_PathEdit->setText(m_CurrentPath);

    // Keep the folder tree selection in sync.
    const QModelIndex index = m_FolderModel->index(m_CurrentPath);
    if (index.isValid())
    {
      m_FolderTree->setCurrentIndex(index);
      m_FolderTree->scrollTo(index);
    }

    RefreshAssetGrid();
    UpdateNavigationButtons();

    emit DirectoryChanged(m_CurrentPath);
  }

  void ContentBrowser::RefreshAssetGrid()
  {
    m_AssetGrid->clear();

    QDir dir(m_CurrentPath);
    if (!dir.exists())
      return;

    const QFileInfoList entries = dir.entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot,
      QDir::DirsFirst | QDir::Name);

    for (const QFileInfo& info : entries)
    {
      QListWidgetItem* item = info.isDir() ? CreateFolderItem(info) : CreateAssetItem(info);
      m_AssetGrid->addItem(item);
    }
  }

  QListWidgetItem* ContentBrowser::CreateFolderItem(const QFileInfo& info) const
  {
    QListWidgetItem* item = new QListWidgetItem();
    item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    item->setText(info.fileName());
    item->setData(Qt::UserRole, info.absoluteFilePath());
    item->setData(Qt::UserRole + 1, true); // is directory

    const QDir subDir(info.absoluteFilePath());
    const int entryCount = subDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();

    const QString tooltip = QStringLiteral(
      "<b>%1</b><br>%2<br>%3 item(s)<br>Modified: %4")
      .arg(info.fileName())
      .arg(tr("Folder"))
      .arg(entryCount)
      .arg(info.lastModified().toString("yyyy-MM-dd HH:mm"));
    item->setToolTip(tooltip);

    return item;
  }

  QIcon ContentBrowser::GetIconForFile(const QFileInfo& info) const
  {
    const QString suffix = info.suffix().toLower();

    const auto it = m_ExtensionIcons.constFind(suffix);
    if (it != m_ExtensionIcons.constEnd())
      return it.value();

    // No icon registered for this extension: fall back to the placeholder.
    const QPixmap thumbnail(kPlaceholderIconPath);
    if (thumbnail.isNull())
      return style()->standardIcon(QStyle::SP_FileIcon);

    return QIcon(thumbnail.scaled(kThumbnailSize, kThumbnailSize,
      Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }

  QListWidgetItem* ContentBrowser::CreateAssetItem(const QFileInfo& info) const
  {
    QListWidgetItem* item = new QListWidgetItem();
    item->setIcon(GetIconForFile(info));

    item->setText(info.fileName());
    item->setData(Qt::UserRole, info.absoluteFilePath());
    item->setData(Qt::UserRole + 1, false); // is directory

    const QString suffix = info.suffix().isEmpty() ? tr("File") : info.suffix().toUpper();
    const QString sizeStr = QLocale().formattedDataSize(info.size());

    const QString tooltip = QStringLiteral(
      "<b>%1</b><br>Type: %2<br>Size: %3<br>Modified: %4")
      .arg(info.fileName())
      .arg(suffix)
      .arg(sizeStr)
      .arg(info.lastModified().toString("yyyy-MM-dd HH:mm"));
    item->setToolTip(tooltip);

    return item;
  }

  void ContentBrowser::UpdateNavigationButtons()
  {
    m_BackButton->setEnabled(m_HistoryIndex > 0);
    m_ForwardButton->setEnabled(m_HistoryIndex < m_History.size() - 1);

    const QDir dir(m_CurrentPath);
    QDir parent = dir;
    m_UpButton->setEnabled(parent.cdUp());
  }

  void ContentBrowser::OnFolderTreeClicked(const QModelIndex& index)
  {
    const QString path = m_FolderModel->filePath(index);
    NavigateTo(path);
  }

  void ContentBrowser::OnAssetDoubleClicked(QListWidgetItem* item)
  {
    if (item == nullptr)
      return;

    const bool isDir = item->data(Qt::UserRole + 1).toBool();
    const QString path = item->data(Qt::UserRole).toString(); // absolute path

    if (isDir)
      NavigateTo(path);
    else
      emit AssetActivated(path);
  }

  void ContentBrowser::OnNavigateBack()
  {
    if (m_HistoryIndex > 0)
    {
      --m_HistoryIndex;
      NavigateTo(m_History[m_HistoryIndex], false);
    }
  }

  void ContentBrowser::OnNavigateForward()
  {
    if (m_HistoryIndex < m_History.size() - 1)
    {
      ++m_HistoryIndex;
      NavigateTo(m_History[m_HistoryIndex], false);
    }
  }

  void ContentBrowser::OnNavigateUp()
  {
    QDir dir(m_CurrentPath);
    if (dir.cdUp())
      NavigateTo(dir.absolutePath());
  }

  void ContentBrowser::OnPathEditReturnPressed()
  {
    NavigateTo(m_PathEdit->text());
  }

  void ContentBrowser::OnAssetGridContextMenuRequested(const QPoint& pos)
  {
    QListWidgetItem* clickedItem = m_AssetGrid->itemAt(pos);

    QMenu menu(this);
    QAction* newFolderAction = menu.addAction(tr("New Folder"));
    QAction* newFileAction = menu.addAction(tr("New File"));

    menu.addSeparator();
    QAction* pasteAction = menu.addAction(tr("Paste"));
    pasteAction->setEnabled(!m_ClipboardPaths.isEmpty());

    QAction* cutAction = nullptr;
    QAction* copyAction = nullptr;
    QAction* duplicateAction = nullptr;
    QAction* renameAction = nullptr;
    QAction* deleteAction = nullptr;
    if (clickedItem != nullptr)
    {
      menu.addSeparator();
      cutAction = menu.addAction(tr("Cut"));
      copyAction = menu.addAction(tr("Copy"));
      duplicateAction = menu.addAction(tr("Duplicate"));
      menu.addSeparator();
      renameAction = menu.addAction(tr("Rename"));
      deleteAction = menu.addAction(tr("Delete"));
    }

    QAction* chosen = menu.exec(m_AssetGrid->mapToGlobal(pos));
    if (chosen == nullptr)
      return;

    if (chosen == newFolderAction)
      CreateNewFolder();
    else if (chosen == newFileAction)
      CreateNewFile();
    else if (chosen == pasteAction)
      PasteClipboard();
    else if (chosen == cutAction)
      CutSelection();
    else if (chosen == copyAction)
      CopySelection();
    else if (chosen == duplicateAction)
      DuplicateItems(m_AssetGrid->selectedItems());
    else if (chosen == renameAction)
      RenameItem(clickedItem);
    else if (chosen == deleteAction)
      DeleteItems(m_AssetGrid->selectedItems());
  }

  QString ContentBrowser::GenerateUniqueEntryName(const QString& baseName) const
  {
    const QDir dir(m_CurrentPath);
    if (!dir.exists(baseName))
      return baseName;

    for (int suffix = 1; ; ++suffix)
    {
      const QString candidate = QStringLiteral("%1 (%2)").arg(baseName).arg(suffix);
      if (!dir.exists(candidate))
        return candidate;
    }
  }

  QString ContentBrowser::GenerateUniqueFileName(const QString& folder, const QString& fileName) const
  {
    const QDir dir(folder);
    if (!dir.exists(fileName))
      return fileName;

    const QFileInfo info(fileName);
    const QString baseName = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int i = 1; ; ++i)
    {
      const QString candidate = suffix.isEmpty()
        ? QStringLiteral("%1 (%2)").arg(baseName).arg(i)
        : QStringLiteral("%1 (%2).%3").arg(baseName).arg(i).arg(suffix);
      if (!dir.exists(candidate))
        return candidate;
    }
  }

  bool ContentBrowser::CopyEntryRecursively(const QString& sourcePath, const QString& destPath) const
  {
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists())
      return false;

    if (!sourceInfo.isDir())
      return QFile::copy(sourcePath, destPath);

    const QFileInfo destInfo(destPath);
    QDir destParentDir(destInfo.absolutePath());
    if (!destParentDir.exists())
      return false;
    if (!destParentDir.mkdir(destInfo.fileName()))
      return false;

    const QDir sourceDir(sourcePath);
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries)
    {
      const QString childDest = QDir(destPath).filePath(entry.fileName());
      if (!CopyEntryRecursively(entry.absoluteFilePath(), childDest))
        return false;
    }
    return true;
  }

  void ContentBrowser::CreateNewFolder()
  {
    QDir dir(m_CurrentPath);
    const QString name = GenerateUniqueEntryName(tr("New Folder"));

    if (!dir.mkdir(name))
    {
      QMessageBox::warning(this, tr("New Folder"), tr("Could not create the folder."));
      return;
    }

    RefreshAssetGrid();
  }

  void ContentBrowser::CreateNewFile()
  {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New File"), tr("File name:"),
      QLineEdit::Normal, GenerateUniqueEntryName(tr("New File.txt")), &ok);

    if (!ok || name.trimmed().isEmpty())
      return;

    QFile file(QDir(m_CurrentPath).filePath(name));
    if (!file.open(QIODevice::WriteOnly))
    {
      QMessageBox::warning(this, tr("New File"), tr("Could not create the file."));
      return;
    }
    file.close();

    RefreshAssetGrid();
  }

  void ContentBrowser::RenameItem(QListWidgetItem* item)
  {
    if (item == nullptr)
      return;

    const QString oldPath = item->data(Qt::UserRole).toString();
    const QFileInfo oldInfo(oldPath);

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
      QLineEdit::Normal, oldInfo.fileName(), &ok);

    if (!ok || newName.trimmed().isEmpty() || newName == oldInfo.fileName())
      return;

    const QString newPath = oldInfo.absoluteDir().filePath(newName);

    if (!QFile::rename(oldPath, newPath))
    {
      QMessageBox::warning(this, tr("Rename"), tr("Could not rename \"%1\".").arg(oldInfo.fileName()));
      return;
    }

    RefreshAssetGrid();
  }

  void ContentBrowser::DeleteItems(const QList<QListWidgetItem*>& items)
  {
    if (items.isEmpty())
      return;

    const QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Delete"),
      items.size() == 1
        ? tr("Are you sure you want to delete \"%1\"?").arg(items.first()->text())
        : tr("Are you sure you want to delete %1 items?").arg(items.size()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes)
      return;

    for (QListWidgetItem* item : items)
    {
      const QString path = item->data(Qt::UserRole).toString();
      const bool isDir = item->data(Qt::UserRole + 1).toBool();

      if (isDir)
        QDir(path).removeRecursively();
      else
        QFile::remove(path);
    }

    RefreshAssetGrid();
  }

  void ContentBrowser::CopySelection()
  {
    m_ClipboardPaths.clear();
    for (QListWidgetItem* item : m_AssetGrid->selectedItems())
      m_ClipboardPaths.append(item->data(Qt::UserRole).toString());
    m_ClipboardCut = false;
  }

  void ContentBrowser::CutSelection()
  {
    m_ClipboardPaths.clear();
    for (QListWidgetItem* item : m_AssetGrid->selectedItems())
      m_ClipboardPaths.append(item->data(Qt::UserRole).toString());
    m_ClipboardCut = true;
  }

  void ContentBrowser::PasteClipboard()
  {
    if (m_ClipboardPaths.isEmpty())
      return;

    bool anyChange = false;

    for (const QString& sourcePath : m_ClipboardPaths)
    {
      const QFileInfo sourceInfo(sourcePath);
      if (!sourceInfo.exists())
        continue;

      const bool sameFolder = QDir::cleanPath(sourceInfo.absolutePath()) == QDir::cleanPath(m_CurrentPath);

      if (m_ClipboardCut && sameFolder)
        continue; // cutting into its own folder is a no-op

      const QString destPath = QDir(m_CurrentPath).filePath(
        GenerateUniqueFileName(m_CurrentPath, sourceInfo.fileName()));

      if (m_ClipboardCut)
      {
        if (QFile::rename(sourceInfo.absoluteFilePath(), destPath))
          anyChange = true;
      }
      else
      {
        // Copy-pasting, including into the same folder it came from
        // (produces a duplicate), matches common file manager behavior.
        if (CopyEntryRecursively(sourceInfo.absoluteFilePath(), destPath))
          anyChange = true;
      }
    }

    if (m_ClipboardCut)
    {
      m_ClipboardPaths.clear();
      m_ClipboardCut = false;
    }

    if (anyChange)
      RefreshAssetGrid();
  }

  void ContentBrowser::DuplicateItems(const QList<QListWidgetItem*>& items)
  {
    bool anyChange = false;

    for (QListWidgetItem* item : items)
    {
      const QString sourcePath = item->data(Qt::UserRole).toString();
      const QFileInfo sourceInfo(sourcePath);
      if (!sourceInfo.exists())
        continue;

      const QString destPath = QDir(m_CurrentPath).filePath(
        GenerateUniqueFileName(m_CurrentPath, sourceInfo.fileName()));

      if (CopyEntryRecursively(sourcePath, destPath))
        anyChange = true;
    }

    if (anyChange)
      RefreshAssetGrid();
  }

  void ContentBrowser::StartInternalDrag()
  {
    const QList<QListWidgetItem*> selected = m_AssetGrid->selectedItems();
    if (selected.isEmpty())
      return;

    QList<QUrl> urls;
    for (QListWidgetItem* item : selected)
      urls.append(QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));

    QMimeData* mimeData = new QMimeData();
    mimeData->setUrls(urls);
    mimeData->setData(kInternalDragMimeType, QByteArray("1"));

    QDrag* drag = new QDrag(m_AssetGrid);
    drag->setMimeData(mimeData);
    if (!selected.first()->icon().isNull())
      drag->setPixmap(selected.first()->icon().pixmap(48, 48));

    // Blocks until the drop (or cancel) completes.
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    drag->deleteLater();
  }

  QString ContentBrowser::FolderForDropPosition(QWidget* watched, const QPoint& pos) const
  {
    if (watched == m_FolderTree || m_FolderTree->isAncestorOf(watched))
    {
      const QModelIndex index = m_FolderTree->indexAt(pos);
      if (index.isValid())
        return m_FolderModel->filePath(index);
      return m_CurrentPath;
    }

    QListWidgetItem* item = m_AssetGrid->itemAt(pos);
    if (item != nullptr && item->data(Qt::UserRole + 1).toBool())
      return item->data(Qt::UserRole).toString();

    return m_CurrentPath;
  }

  void ContentBrowser::HandleDrop(const QMimeData* mimeData, const QString& targetFolder, bool isInternalDrag)
  {
    if (mimeData == nullptr || !mimeData->hasUrls())
      return;

    const QDir targetDir(targetFolder);
    if (!targetDir.exists())
      return;

    bool anyChange = false;

    for (const QUrl& url : mimeData->urls())
    {
      if (!url.isLocalFile())
        continue;

      const QFileInfo sourceInfo(url.toLocalFile());
      if (!sourceInfo.exists())
        continue;

      const bool sameFolder = QDir::cleanPath(sourceInfo.absolutePath()) == QDir::cleanPath(targetFolder);

      if (isInternalDrag)
      {
        if (sameFolder)
          continue; // dropped back where it already was: no-op

        QString destPath = targetDir.filePath(sourceInfo.fileName());
        if (QFileInfo::exists(destPath))
          destPath = targetDir.filePath(GenerateUniqueFileName(targetFolder, sourceInfo.fileName()));

        if (QFile::rename(sourceInfo.absoluteFilePath(), destPath))
          anyChange = true;
      }
      else
      {
        const QString extension = sourceInfo.suffix().toLower();
        const auto it = m_ExtensionActions.constFind(extension);

        if (!sourceInfo.isDir() && it != m_ExtensionActions.constEnd())
        {
          // Recognized extension: run the handler, do NOT import the file.
          it.value()(sourceInfo.absoluteFilePath());
        }
        else
        {
          const QString destPath = targetDir.filePath(
            GenerateUniqueFileName(targetFolder, sourceInfo.fileName()));
          if (CopyEntryRecursively(sourceInfo.absoluteFilePath(), destPath))
            anyChange = true;
        }
      }
    }

    if (anyChange)
      RefreshAssetGrid();
  }
}