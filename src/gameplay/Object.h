/**
 *
 */

#ifndef OBJECT_H
#define OBJECT_H

#include <variant>
#include <unordered_map>
#include <string>
#include <functional>

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

	protected:
		ordered_map<std::string, property> properties_;
	};
}

#define HPROPERTY(var, access, ...) \
	properties_[#var] = lynx::property { \
		&var, \
		access, \
		new Observable<decltype(var)>(var, [this](){ __VA_ARGS__; })}


#endif //OBJECT_H
