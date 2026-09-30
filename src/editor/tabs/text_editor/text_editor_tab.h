#pragma once

#include "../window_tab.h"
#include "../../common.h"

#include <QString>
#include <QMap>
#include <functional>

QT_BEGIN_NAMESPACE
class QCloseEvent;
class QSyntaxHighlighter;
class QTextDocument;
QT_END_NAMESPACE

namespace hn::editor
{
  class CodeEditor;

  // Onglet d'édition de texte type IDE : coloration syntaxique, ouverture
  // et sauvegarde d'un fichier à un chemin donné, Ctrl+S.
  //
  // Le choix du langage se fait par extension de fichier via un système
  // de factories enregistrables (RegisterHighlighter), pour rester
  // indépendant du Lua : ajouter un langage ne touche pas cette classe.
  class EDITOR_API TextEditorTab : public WindowTab
  {
   Q_OBJECT

  public:
   using HighlighterFactory = std::function<QSyntaxHighlighter*(QTextDocument*)>;

   explicit TextEditorTab(QWidget* parent);
   TextEditorTab(QWidget* parent, const QString& file_path);

   bool OpenFile(const QString& file_path);
   bool SaveFile();
   bool SaveFileAs(const QString& file_path);

   const QString& GetFilePath() const { return file_path_; }
   bool HasUnsavedChanges() const { return has_unsaved_changes_; }
   CodeEditor* GetCodeEditor() const { return code_editor_; }

   // Enregistre un highlighter pour une extension donnée (sans le point,
   // ex: "lua", "cpp", "py"). Appel typique au démarrage de l'application :
   //   TextEditorTab::RegisterHighlighter("cpp", [](QTextDocument* doc) {
   //       return new CppSyntaxHighlighter(doc);
   //   });
   static void RegisterHighlighter(const QString& extension, HighlighterFactory factory);

  signals:
   void FileSaved(const QString& file_path);
   void FileOpened(const QString& file_path);

  protected:
   void closeEvent(QCloseEvent* event) override;

  private slots:
   void OnTextChanged();

  private:
   void SetupUi();
   void SetupShortcuts();
   void ApplyHighlighterForPath(const QString& file_path);
   void UpdateWindowTitle();
   bool ConfirmDiscardChanges();

   static QMap<QString, HighlighterFactory>& HighlighterRegistry();

   CodeEditor* code_editor_ = nullptr;
   QSyntaxHighlighter* highlighter_ = nullptr;
   QString file_path_;
   bool has_unsaved_changes_ = false;
  };
}