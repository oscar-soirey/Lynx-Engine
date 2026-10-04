/**
 *
 */

#ifndef OBJECT_H
#define OBJECT_H

#include <variant>
#include <unordered_map>
#include <string>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include "../core/Common.h"
#include "../core/data/Observable.h"
#include "../core/data/OrderedMap.h"

namespace lynx
{
	enum Access {
		Exposed,
		Editable
	};

	using PropVariantType = std::variant<int, float, bool, std::string, vec2, vec3, vec4, transform>;
	using PropVariantTypePtr = std::variant<int*, float*, bool*, std::string*, vec2*, vec3*, vec4*, transform*>;

	struct LYNX_API property{
		PropVariantTypePtr property_member{};
		Access access{Exposed};
		IObservable* observable;

		/**
		 * @return The index of the type inside the variant list : 0:int, 1:float, 2:bool, etc...
		 */
		size_t GetType() const
			{ return property_member.index(); }

	};

	/**
	 * Returns the variant from a given string value : for example, input : "13.4", return 13.4 (<float>)
	 * Detect automatically the type
	 */
	LYNX_API PropVariantType GetValueByString(const std::string& name);

	LYNX_API std::string GetStringByValue(const PropVariantType& value);
	LYNX_API std::string PropertyToString(const property& prop);


	// ========================================================================
	// Fonctions reflechies (HFUNCTION) : appelables par leur nom (JavaScript)
	// ========================================================================

	/**
	 * Fonction membre enregistree avec HFUNCTION. Arguments et retour : les
	 * types des proprietes (int, float, bool, std::string, vec2, vec3, vec4,
	 * transform) ; les autres nombres sont convertis (double, uint32_t...).
	 */
	struct LYNX_API reflected_function
	{
		int arity = 0;
		std::function<std::optional<PropVariantType>(const std::vector<PropVariantType>&)> call;
	};

	namespace detail
	{
		template <class T>
		T FromVariant(const PropVariantType& v)
		{
			using D = std::decay_t<T>;

			if (const D* exact = std::get_if<D>(&v))
				return *exact;

			if constexpr (std::is_arithmetic_v<D>)
			{
				if (const int* i = std::get_if<int>(&v)) return static_cast<D>(*i);
				if (const float* f = std::get_if<float>(&v)) return static_cast<D>(*f);
				if (const bool* b = std::get_if<bool>(&v)) return static_cast<D>(*b);
				return D{};
			}
			else if constexpr (std::is_same_v<D, std::string>)
			{
				return GetStringByValue(v);
			}
			else
			{
				return D{};
			}
		}

		template <class R>
		std::optional<PropVariantType> ToVariant(R&& value)
		{
			using D = std::decay_t<R>;

			if constexpr (std::is_same_v<D, bool> || std::is_same_v<D, int> || std::is_same_v<D, float> ||
			              std::is_same_v<D, std::string> || std::is_same_v<D, vec2> || std::is_same_v<D, vec3> ||
			              std::is_same_v<D, vec4> || std::is_same_v<D, transform>)
				return PropVariantType(std::forward<R>(value));
			else if constexpr (std::is_integral_v<D>)
				return PropVariantType(static_cast<int>(value));
			else if constexpr (std::is_floating_point_v<D>)
				return PropVariantType(static_cast<float>(value));
			else if constexpr (std::is_convertible_v<D, std::string>)
				return PropVariantType(std::string(value));
			else
			{
				static_assert(sizeof(D) == 0, "HFUNCTION : unsupported return type");
				return std::nullopt;
			}
		}

		template <class R, class... A, class F, std::size_t... I>
		std::optional<PropVariantType> InvokeReflected(
			F&& f, const std::vector<PropVariantType>& args, std::index_sequence<I...>)
		{
			auto arg = [&](std::size_t i) -> const PropVariantType&
			{
				static const PropVariantType none = 0;
				return i < args.size() ? args[i] : none;
			};

			if constexpr (std::is_void_v<R>)
			{
				f(FromVariant<A>(arg(I))...);
				return std::nullopt;
			}
			else
			{
				return ToVariant(f(FromVariant<A>(arg(I))...));
			}
		}
	}

	/**
	 * The base object of HGE engine, contains the basics control functions like init, beginplay, ...
	 * And contains the reflection system (access a member by its name)
	 */
	class LYNX_API Object {
	public:
		std::string object_id_;

		Object();
		virtual ~Object();

		virtual void Init();

		//Gameplay logic functions
		virtual void StartGame();
		virtual void EndGame();

		/**
		 * Gameplay only
		 * @param _dt delta time
		 */
		virtual void Tick(double _dt);
		/**
		 * Called every frame (including when game doesn't simulate)
		 * @param _dt delta time
		 */
		virtual void Update(double _dt);

		template<typename T>
		T GetPropertyValue(const char* _name)
		{
			if (auto p = std::get_if<T*>(&properties_[_name].property_member))
			{
				return **p;
			}
			//LOG_ERROR("GetPropertyValue, Error with property : " + std::string(_name));
			return T{};
		}

		template<typename T>
		void SetPropertyValue(const char* _name, T _val)
		{
			if (auto p = std::get_if<T*>(&properties_[_name].property_member))
			{
				**p = _val;
			}
			else
			{
				//LOG_ERROR("SetPropertyValue, Error with property : " + std::string(_name));
			}
		}

		void SetPropertyValueStr(const char* _name, const char* _val);

		bool PropertyExists(const char* _name);
		Access GetPropertyAccess(const char* _name);

		const ordered_map<std::string, property>& GetProperties() const;

		/**
		 * Propriete creee a l'execution (classes JavaScript : static properties).
		 * Meme comportement qu'un HPROPERTY : panneau Details, sauvegarde du
		 * niveau, getProperty / setProperty. Sans effet si le nom existe deja.
		 */
		void AddDynamicProperty(const std::string& name, const PropVariantType& value, Access access = Exposed);

		/** Fonctions enregistrees avec HFUNCTION (appelables depuis JS). */
		const ordered_map<std::string, reflected_function>& GetFunctions() const { return functions_; }

		/**
		 * Appelle une fonction HFUNCTION par son nom.
		 * @return false si elle n'existe pas. `result` recoit la valeur de retour.
		 */
		bool CallFunctionByName(const std::string& name, const std::vector<PropVariantType>& args,
		                        std::optional<PropVariantType>* result = nullptr);

		/**
		 * Nom de la classe : celui de la classe JavaScript pour un acteur cree
		 * depuis une classe de script, sinon le nom C++ (ETypeName). C'est le nom
		 * ecrit dans les fichiers de niveau.
		 */
		std::string GetTypeName() const;

		/** Classe JavaScript de l'objet ("" = classe C++). Rempli par le moteur. */
		std::string script_class_;

	protected:
		ordered_map<std::string, property> properties_;
		ordered_map<std::string, reflected_function> functions_;

		/** Enregistre une fonction membre (publique ou protegee) : voir HFUNCTION. */
		template <class Obj, class R, class... A>
		void RegisterFunction(const char* name, R (Obj::*fn)(A...))
		{
			Obj* self = static_cast<Obj*>(this);
			AddReflectedFunction<R, A...>(name, [self, fn](A... a) -> R { return (self->*fn)(a...); });
		}

		template <class Obj, class R, class... A>
		void RegisterFunction(const char* name, R (Obj::*fn)(A...) const)
		{
			const Obj* self = static_cast<const Obj*>(this);
			AddReflectedFunction<R, A...>(name, [self, fn](A... a) -> R { return (self->*fn)(a...); });
		}

		template <class R, class... A, class F>
		void AddReflectedFunction(const char* name, F f)
		{
			reflected_function rf;
			rf.arity = static_cast<int>(sizeof...(A));
			rf.call = [f](const std::vector<PropVariantType>& args)
			{
				return detail::InvokeReflected<R, std::decay_t<A>...>(f, args, std::index_sequence_for<A...>{});
			};
			functions_[name] = std::move(rf);
		}

	private:
		// Valeurs des proprietes dynamiques (adresses stables).
		std::vector<std::unique_ptr<PropVariantType>> dynamic_values_;
	};
}

/**
 * Rend une fonction membre (publique ou protegee) appelable par son nom,
 * notamment depuis JavaScript (this.Jump(), this.Move(1)...). A mettre dans le
 * constructeur :
 *     HFUNCTION(Jump);
 * Fonction surchargee : RegisterFunction("Move", static_cast<void (Pawn::*)(float)>(&Pawn::Move));
 */
#define HFUNCTION(fn) \
	RegisterFunction(#fn, &std::remove_cv_t<std::remove_reference_t<decltype(*this)>>::fn)

#define HPROPERTY(var, access, ...) \
	properties_[#var] = lynx::property { \
		&var, \
		access, \
		new Observable<decltype(var)>(var, [this](){ __VA_ARGS__; })}


#endif //OBJECT_H
