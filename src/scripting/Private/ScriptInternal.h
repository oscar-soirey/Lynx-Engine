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
}
