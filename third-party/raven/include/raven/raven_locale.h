/**
 * Raven
 * raven_locale.h : translations and the conventions of a language.
 *
 * Translations live in CSV tables: one row per text, one column per language.
 * The first row names the columns: "key", then a language code per column
 * (the codes are free: "en", "fr", "pt-BR", "ar"...).
 *
 *     key,en,fr,ar
 *     menu.file,File,Fichier,ملف
 *     greeting,"Hello, {0}!","Bonjour, {0} !",مرحبا {0}
 *     files.one,{n} file,{n} fichier,ملف واحد
 *     files.other,{n} files,{n} fichiers,{n} ملفات
 *
 * The usual CSV rules apply: fields with commas, quotes or line breaks are
 * quoted, "" inside quotes is one quote. Lines starting with # are comments.
 * A UTF-8 BOM is accepted. Several tables can be loaded: they add up, and a
 * key loaded again replaces the old text. A table loaded from a file is read
 * again when the file is saved, while live reload is on (Rxml_set_live_reload).
 *
 * In C:
 *     Rlocale_load_csv("strings.csv");
 *     Rlocale_set_language(Rlocale_get_system_language());
 *     Rlabel_set_text(label, Rtr("menu.file"));
 *
 * In XML, an attribute that starts with @ is a key: the widget shows its
 * translation and follows the language when it changes ("@@" for a text that
 * really starts with @):
 *     <Button Text="@menu.file" Tooltip="@menu.file.tip"/>
 * and a document can load its tables itself:
 *     <Strings Source="strings.csv"/>
 *
 * A key with no translation in the language shows the text of the fallback
 * language (the first column, or Rlocale_set_fallback_language), then the key.
 *
 * The functions are for the UI thread. The strings they return stay valid
 * until the tables or the language change (Rtr), or for the next 16 calls
 * (Rtr_format, Rtr_plural, Rlocale_format_number).
 */

#ifndef RAVEN_LOCALE_H
#define RAVEN_LOCALE_H

#include "raven_core.h"

#ifdef __cplusplus
extern "C" {
#endif

	/* ---- tables ---- */

	/** @brief Loads a CSV table of translations from a file. Returns the number of keys read, -1 on an error. */
	RAVEN_API int Rlocale_load_csv(const char* path);
	/** @brief Same, from text in memory (UTF-8). */
	RAVEN_API int Rlocale_load_csv_from_memory(const char* csv, size_t size);
	/** @brief Forgets every table. */
	RAVEN_API void Rlocale_clear(void);
	/** @brief Adds or replaces one text of one language. */
	RAVEN_API void Rlocale_set_text(const char* language, const char* key, const char* text);

	/* ---- the language ---- */

	/** @brief Number of languages in the tables, and their codes, in the order of the columns. */
	RAVEN_API int Rlocale_get_language_count(void);
	RAVEN_API const char* Rlocale_get_language_at(int index);
	/**
	 * @brief Chooses the language: an exact code ("pt-BR"), or its first part
	 * ("pt") when the tables have no such column. Every widget that shows a key
	 * is updated, and the layout of the windows follows its direction (an
	 * Arabic, Hebrew, Persian or Urdu language is laid out from right to left,
	 * see Rset_layout_direction). Returns R_FALSE when no column matches (the
	 * language is set all the same: the texts fall back).
	 */
	RAVEN_API int Rlocale_set_language(const char* code);
	RAVEN_API const char* Rlocale_get_language(void);
	/** @brief The language of the user, from the system ("fr-FR", "en-US"...). */
	RAVEN_API const char* Rlocale_get_system_language(void);
	/** @brief The language whose texts are shown when the current one has none (default: the first column). */
	RAVEN_API void Rlocale_set_fallback_language(const char* code);
	/** @brief R_TRUE when the language is written from right to left. */
	RAVEN_API int Rlocale_is_rtl(void);

	typedef void (*RLocaleChangedCallback)(const char* language, void* user_data);
	/** @brief Called after the language or the tables changed (the widgets are already updated). */
	RAVEN_API void Rlocale_set_changed_callback(RLocaleChangedCallback callback, void* user_data);

	/* ---- texts ---- */

	/** @brief The text of a key in the current language (see the fallbacks above). Never NULL. */
	RAVEN_API const char* Rtr(const char* key);
	/** @brief R_TRUE when the key has a text in the current language or the fallback one. */
	RAVEN_API int Rtr_exists(const char* key);
	/**
	 * @brief The text of a key with its {0} {1} ... replaced by the arguments
	 * (translators may put them in any order). "{{" is a brace.
	 */
	RAVEN_API const char* Rtr_format(const char* key, const char* const* args, int arg_count);
	/**
	 * @brief The plural form of a key for a count: the key followed by the CLDR
	 * category of the count in the current language (".zero", ".one", ".two",
	 * ".few", ".many", ".other"), else ".other", else the key itself. {n} is
	 * replaced by the count, written in the conventions of the language.
	 */
	RAVEN_API const char* Rtr_plural(const char* key, int count);

	/**
	 * @brief Makes a widget show a key: an attribute ("Text", "Tooltip",
	 * "Title", "Placeholder"... any attribute that takes a text) is set to the
	 * translation now and every time the language changes. NULL or "" stops it.
	 * Same as Rwidget_set_attribute(widget, attribute, "@key").
	 */
	RAVEN_API void Rwidget_set_text_key(Rid widget, const char* attribute, const char* key);

	/* ---- numbers ---- */

	/** @brief A number written in the conventions of the language: decimal separator and digit grouping ("1 234,5" in French). */
	RAVEN_API const char* Rlocale_format_number(double value, int decimals);

#ifdef __cplusplus
}
#endif

#endif /* RAVEN_LOCALE_H */
