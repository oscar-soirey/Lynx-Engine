#pragma once

/**
 * Utilitaires internes du systeme de script, partages entre ScriptSystem.cpp
 * et ComponentBindings.cpp. Moteur uniquement (inclut QuickJS).
 */

#include <cstdint>
#include <string>

#include <quickjs-ng/quickjs.h>

namespace lynx
{
	class Actor;
	class PlayerController;
}

namespace lynx::script_detail
{
	/** Contexte JS (nullptr si le runtime n'existe pas / plus). */
	JSContext* Context();

	/** Objet JS unique de l'acteur (reference a liberer), JS_NULL si nullptr. */
	JSValue ActorObject(JSContext* ctx, Actor* actor);

	/** Acteur d'un objet JS Actor, nullptr sinon (ou detruit). */
	Actor* ActorFromJS(JSValueConst value);

	std::string ToStdString(JSContext* ctx, JSValueConst value);

	/** Lit {x,y,(z),(w)} ou [..] dans out[0..n-1]. */
	bool ReadFloats(JSContext* ctx, JSValueConst value, float* out, int n);

	/** Objet {x,y,(z),(w)}. */
	JSValue NewPlainVec(JSContext* ctx, const float* values, int n);

	/** Affiche (et vide) l'exception en cours. */
	void LogException(JSContext* ctx, const std::string& where);

	/** Execute les promesses en attente. */
	void RunPendingJobs();

	/** Autour d'un appel JS fait depuis le C++ (liberations differees). */
	void EnterCall();
	void LeaveCall();

	void DefGetSet(JSContext* ctx, JSValueConst obj, const char* name,
	               JSCFunctionMagic* getter, JSCFunctionMagic* setter, int magic);
	void DefFuncMagic(JSContext* ctx, JSValueConst obj, const char* name,
	                  JSCFunctionMagic* fn, int length, int magic);

	// ---- ComponentBindings.cpp -------------------------------------------

	/** Classes et prototypes des composants, methodes Actor.addComponent... */
	void RegisterComponentBindings(JSContext* ctx, JSValueConst actor_proto);

	/** Libere tout ce que les composants gardent du JS (avant la fin du runtime). */
	void ShutdownComponentBindings(JSContext* ctx);

	/**
	 * Appelle `name(...)` : methode de la classe JS de l'acteur et fonctions de
	 * ses scripts. Retourne la derniere valeur (a liberer).
	 */
	JSValue CallActorFunction(JSContext* ctx, Actor* actor, const char* name, int argc, JSValueConst* argv, bool* found);

	// ---- GameplayBindings.cpp (joueurs, widgets) --------------------------

	/** Actor.controller, Actor.autoPossessPlayer... */
	void RegisterGameplayBindings(JSContext* ctx, JSValueConst actor_proto);

	/** Engine.createPlayer..., UI, UserWidget. Apres la creation de Engine. */
	void RegisterGameplayGlobals(JSContext* ctx);

	/** Libere les objets JS des joueurs / widgets et leurs callbacks. */
	void ShutdownGameplayBindings(JSContext* ctx);

	/** Objet JS du joueur (reference a liberer), JS_NULL si nullptr. */
	JSValue PlayerObject(JSContext* ctx, PlayerController* player);

	/** Joueur d'un objet JS PlayerController (nullptr sinon / detruit). */
	PlayerController* PlayerFromJS(JSValueConst value);

	/** Classes de widgets JS (class X extends UserWidget) : chargement des fichiers. */
	void ForgetWidgetClasses(JSContext* ctx);
	bool IsWidgetClassConstructor(JSContext* ctx, JSValueConst value);
	void RegisterWidgetClass(JSContext* ctx, const std::string& name, JSValueConst ctor, const std::string& file);
}
