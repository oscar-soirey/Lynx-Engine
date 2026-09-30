#include "text_editor_tab.h"
#include "code_editor.h"
#include "lua_syntax_highlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QShortcut>
#include <QKeySequence>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSyntaxHighlighter>
#include <utility>

namespace hn::editor
{
  QMap<QString, TextEditorTab::HighlighterFactory>& TextEditorTab::HighlighterRegistry()
  {
   static QMap<QString, HighlighterFactory> registry;
   static bool initialized = false;

   if (!initialized)
   {
    initialized = true;
    // Lua enregistré par défaut ; les autres langages s'ajoutent depuis
    // l'extérieur via RegisterHighlighter, sans modifier ce fichier.
    registry.insert(QStringLiteral("lua"), [](QTextDocument* doc) -> QSyntaxHighlighter*
    {
     return new LuaSyntaxHighlighter(doc);
    });
   }

   return registry;
  }

  void TextEditorTab::RegisterHighlighter(const QString& extension, HighlighterFactory factory)
  {
   HighlighterRegistry().insert(extension.toLower(), std::move(factory));
  }

  TextEditorTab::TextEditorTab(QWidget* parent)
   : WindowTab(parent)
  {
   SetupUi();
   SetupShortcuts();
   UpdateWindowTitle();
  }

  TextEditorTab::TextEditorTab(QWidget* parent, const QString& file_path)
   : TextEditorTab(parent)
  {
   OpenFile(file_path);
  }

  void TextEditorTab::SetupUi()
  {
   code_editor_ = new CodeEditor(this);
   setCentralWidget(code_editor_);

   connect(code_editor_, &CodeEditor::textChanged, this, &TextEditorTab::OnTextChanged);
  }

  void TextEditorTab::SetupShortcuts()
  {
   auto* save_shortcut = new QShortcut(QKeySequence::Save, this); // Ctrl+S
   connect(save_shortcut, &QShortcut::activated, this, &TextEditorTab::SaveFile);
  }

  void TextEditorTab::OnTextChanged()
  {
   if (!has_unsaved_changes_)
   {
    has_unsaved_changes_ = true;
    UpdateWindowTitle();
   }
  }

  void TextEditorTab::UpdateWindowTitle()
  {
   QString title = file_path_.isEmpty()
    ? QStringLiteral("Sans titre")
    : QFileInfo(file_path_).fileName();

   if (has_unsaved_changes_)
    title += QStringLiteral(" *");

   setWindowTitle(title);
  }

  void TextEditorTab::ApplyHighlighterForPath(const QString& file_path)
  {
   delete highlighter_;
   highlighter_ = nullptr;

   const QString extension = QFileInfo(file_path).suffix().toLower();
   const auto& registry = HighlighterRegistry();

   const auto it = registry.find(extension);
   if (it != registry.end())
    highlighter_ = it.value()(code_editor_->document());
  }

  bool TextEditorTab::OpenFile(const QString& file_path)
  {
   QFile file(file_path);
   if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
   {
    QMessageBox::warning(this, QStringLiteral("Erreur"),
     QStringLiteral("Impossible d'ouvrir le fichier :\n%1").arg(file_path));
    return false;
   }

   QTextStream stream(&file);
   stream.setEncoding(QStringConverter::Utf8);
   const QString content = stream.readAll();
   file.close();

   // On bloque les signaux le temps de charger le contenu pour ne pas
   // déclencher OnTextChanged (le fichier vient d'être ouvert, il n'est
   // pas "modifié").
   code_editor_->blockSignals(true);
   code_editor_->setPlainText(content);
   code_editor_->blockSignals(false);

   file_path_ = file_path;
   has_unsaved_changes_ = false;

   ApplyHighlighterForPath(file_path_);
   UpdateWindowTitle();

   emit FileOpened(file_path_);
   return true;
  }

  bool TextEditorTab::SaveFile()
  {
   if (file_path_.isEmpty())
    return false; // Pas de chemin connu : à remplacer par une boîte "Enregistrer sous" côté appelant si besoin.

   return SaveFileAs(file_path_);
  }

  bool TextEditorTab::SaveFileAs(const QString& file_path)
  {
   QFile file(file_path);
   if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
   {
    QMessageBox::warning(this, QStringLiteral("Erreur"),
     QStringLiteral("Impossible d'enregistrer le fichier :\n%1").arg(file_path));
    return false;
   }

   QTextStream stream(&file);
   stream.setEncoding(QStringConverter::Utf8);
   stream << code_editor_->toPlainText();
   file.close();

   const bool extension_changed = QFileInfo(file_path).suffix() != QFileInfo(file_path_).suffix();

   file_path_ = file_path;
   has_unsaved_changes_ = false;

   if (extension_changed)
    ApplyHighlighterForPath(file_path_);

   UpdateWindowTitle();

   emit FileSaved(file_path_);
   return true;
  }

  bool TextEditorTab::ConfirmDiscardChanges()
  {
   if (!has_unsaved_changes_)
    return true;

   const auto answer = QMessageBox::question(this, QStringLiteral("Modifications non enregistrées"),
    QStringLiteral("Voulez-vous enregistrer les modifications avant de fermer ?"),
    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

   if (answer == QMessageBox::Save)
    return SaveFile();

   return answer == QMessageBox::Discard;
  }

  void TextEditorTab::closeEvent(QCloseEvent* event)
  {
   if (ConfirmDiscardChanges())
    event->accept();
   else
    event->ignore();
  }
}