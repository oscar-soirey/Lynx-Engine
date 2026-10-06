#pragma once

/**
 * Interfaces (comme les interfaces d'Unreal) : le moyen de faire parler les
 * acteurs entre eux SANS connaitre leur classe.
 *
 * Une interface est un nom et une liste de fonctions. Un acteur l'implemente
 * (C++, classe JS ou script attache) ; les autres lui envoient un message
 * par l'interface. Si l'acteur ne l'implemente pas, il ne se passe rien :
 * aucun cast, aucune dependance entre les classes.
 *
 *   La piece ne connait pas Player :
 *       other.send(Collectible, "OnCollected", this, 1)   // JS
 *   Player n'a pas besoin de connaitre Coin :
 *       class Player extends Humanoid {
 *           static interfaces = [Collectible];
 *           OnCollected(item, amount) { this.coins += amount; }
 *       }
 *
 * ---------------------------------------------------------------------------
 * Declarer une interface
 *   C++ :  lynx::DefineInterface("Collectible", { "OnCollected" });
 *          (une interface peut heriter d'autres : 3e argument)
 *   JS  :  un fichier .js (n'importe ou dans assets/) :
 *              class Collectible extends Interface {
 *                  OnCollected(item, amount) {}   // corps = implementation par defaut
 *              }
 *          (class Pickup extends Collectible : herite des fonctions)
 *
 * Implementer
 *   C++ :  ImplementInterface("Collectible");          // constructeur
 *          BindInterfaceFunction("Collectible", "OnCollected",
 *              [this](const lynx::InterfaceArgs& args) -> lynx::InterfaceArg { ...; return {}; });
 *          ou une fonction HFUNCTION du meme nom (arguments simples).
 *   JS  :  classe : static interfaces = [Collectible, "Damageable"];
 *          script attache : const interfaces = ["Collectible"]; + function OnCollected(...)
 *
 * Appeler
 *   C++ :  lynx::interfaces::Call(actor, "Collectible", "OnCollected", { this, 1 });
 *          actor->Implements("Collectible");
 *          lynx::interfaces::Broadcast("Damageable", "TakeDamage", { 10.f, nullptr });
 *   JS  :  actor.implements(Collectible) ; actor.send(Collectible, "OnCollected", ...args)
 *          Collectible.call(actor, "OnCollected", ...) ; Collectible.broadcast("OnCollected", ...)
 *          Collectible.find() / Level.findImplementing("Collectible")
 *
 * Ordre d'appel d'un message : handler C++ (BindInterfaceFunction) ou
 * HFUNCTION, puis methode de la classe JS, puis fonctions des scripts
 * attaches. Personne ne la definit : implementation par defaut de l'interface
 * JS (corps de la methode), sinon rien. La valeur retournee est la derniere.
 *
 * Interfaces du moteur (toujours definies) :
 *   Damageable    TakeDamage(amount, instigator)
 *   Interactable  Interact(instigator), CanInteract(instigator)
 * (lynx::ApplyDamage / lynx::Interact ci-dessous).
 *
 * Le C++ pur peut aussi utiliser l'heritage multiple et InterfaceCast<T>.
 */

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "../core/Common.h"

namespace lynx
{
	class Actor;

	/** Argument / retour d'un message d'interface (monostate = rien / undefined). */
	using InterfaceArg = std::variant<std::monostate, bool, int, float, std::string, vec2, vec3, vec4, Actor*>;
	using InterfaceArgs = std::vector<InterfaceArg>;
	using InterfaceHandler = std::function<InterfaceArg(const InterfaceArgs&)>;

	struct InterfaceInfo
	{
		std::string name;
		/** Fonctions declarees par cette interface (sans celles des parents). */
		std::vector<std::string> functions;
		/** Interfaces heritees. */
		std::vector<std::string> parents;
		/** Declaree par un fichier .js (oubliee au rechargement des scripts). */
		bool from_script = false;
	};

	/**
	 * Declare (ou redeclare) une interface. Les noms sont globaux : C++ et JS
	 * partagent les memes interfaces.
	 */
	LYNX_API void DefineInterface(const std::string& name, const std::vector<std::string>& functions,
	                              const std::vector<std::string>& parents = {}, bool from_script = false);

	/** nullptr si l'interface n'est pas declaree. */
	LYNX_API const InterfaceInfo* FindInterface(const std::string& name);

	LYNX_API std::vector<std::string> GetInterfaceNames();

	/** `name` est `base` ou en herite (directement ou non). */
	LYNX_API bool InterfaceIsA(const std::string& name, const std::string& base);

	/** Fonctions de l'interface, parents compris. */
	LYNX_API std::vector<std::string> GetInterfaceFunctions(const std::string& name);

	/**
	 * `function` fait partie de l'interface (ou de ses parents). Une interface
	 * non declaree accepte tout (true).
	 */
	LYNX_API bool InterfaceHasFunction(const std::string& name, const std::string& function);

	/** Retire les interfaces declarees en JS (rechargement des scripts). */
	LYNX_API void ForgetScriptInterfaces();

	namespace interfaces
	{
		/** L'acteur implemente `name` (C++, classe JS ou script, heritage compris). */
		LYNX_API bool Implements(const Actor* actor, const std::string& name);

		/**
		 * Envoie le message `function` de l'interface `name` a `actor`.
		 * Ne fait rien si l'acteur ne l'implemente pas.
		 * @return true si une implementation (ou l'implementation par defaut) a ete appelee.
		 */
		LYNX_API bool Call(Actor* actor, const std::string& name, const std::string& function,
		                   const InterfaceArgs& args = {}, InterfaceArg* result = nullptr);

		/** Acteurs du niveau courant qui implementent `name`. */
		LYNX_API std::vector<Actor*> FindImplementing(const std::string& name);

		/** Call sur tous les acteurs qui implementent `name`. @return nombre d'acteurs touches. */
		LYNX_API int Broadcast(const std::string& name, const std::string& function, const InterfaceArgs& args = {});
	}

	// ------------------------------------------------------------------------
	// Interfaces du moteur
	// ------------------------------------------------------------------------

	/** Damageable.TakeDamage(amount, instigator). @return true si la cible l'implemente. */
	LYNX_API bool ApplyDamage(Actor* target, float amount, Actor* instigator = nullptr);

	/**
	 * Interactable : CanInteract(instigator) (absente = oui) puis Interact(instigator).
	 * @return true si l'interaction a eu lieu.
	 */
	LYNX_API bool Interact(Actor* target, Actor* instigator = nullptr);

	// ------------------------------------------------------------------------
	// Lecture des arguments
	// ------------------------------------------------------------------------

	/** Nombre (int, float, bool) ou `fallback`. */
	LYNX_API float InterfaceArgFloat(const InterfaceArgs& args, size_t index, float fallback = 0.f);
	LYNX_API int InterfaceArgInt(const InterfaceArgs& args, size_t index, int fallback = 0);
	LYNX_API bool InterfaceArgBool(const InterfaceArgs& args, size_t index, bool fallback = false);
	LYNX_API std::string InterfaceArgString(const InterfaceArgs& args, size_t index, const std::string& fallback = {});
	/** nullptr si l'argument n'est pas un acteur. */
	LYNX_API Actor* InterfaceArgActor(const InterfaceArgs& args, size_t index);

	/** C++ pur (heritage multiple d'une classe interface) : dynamic_cast. */
	template<typename T, typename A>
	T* InterfaceCast(A* actor)
	{
		return dynamic_cast<T*>(actor);
	}
}
