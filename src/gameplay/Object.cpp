/**
 *
 */

#include "Object.h"
#include "../core/data/Typename.h"

#include <sstream>
#include <charconv>

//#include "log/log.h"


namespace lynx
{
	PropVariantType GetValueByString(const std::string& value)
	{
		//bool
		if (value == "true") return true;
		if (value == "false") return false;

		//int
		int intVal;
		if (auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), intVal);
			ec == std::errc() && p == value.data() + value.size())
			return intVal;

		//float
		try
		{
			size_t idx;
			float f = std::stof(value, &idx);
			if (idx == value.size()) return f;
		}
		catch (...) {}

		//vec2 "vec2:x,y"
		if (value.rfind("vec2:", 0) == 0)
		{
			std::string data = value.substr(5);
			size_t comma = data.find(',');
			if (comma != std::string::npos)
				return vec2(std::stof(data.substr(0, comma)),
								 std::stof(data.substr(comma + 1)));
		}

		//vec3 "vec3:x,y,z"
		if (value.rfind("vec3:", 0) == 0)
		{
			std::string data = value.substr(5);
			size_t c1 = data.find(',');
			size_t c2 = data.rfind(',');
			if (c1 != std::string::npos && c2 != std::string::npos && c1 != c2)
				return vec3(std::stof(data.substr(0, c1)),
								 std::stof(data.substr(c1 + 1, c2 - c1 - 1)),
								 std::stof(data.substr(c2 + 1)));
		}

		//vec4 "vec4:x,y,z,w"
		if (value.rfind("vec4:", 0) == 0)
		{
			std::string data = value.substr(5);
			size_t c1 = data.find(',');
			size_t c2 = data.find(',', c1 + 1);
			size_t c3 = data.rfind(',');
			if (c1 != std::string::npos && c2 != std::string::npos && c3 != std::string::npos)
				return vec4(std::stof(data.substr(0, c1)),
								 std::stof(data.substr(c1 + 1, c2 - c1 - 1)),
								 std::stof(data.substr(c2 + 1, c3 - c2 - 1)),
								 std::stof(data.substr(c3 + 1)));
		}

		//transform
		if (value.rfind("transform:", 0) == 0)
		{
			std::string data = value.substr(10); //remove "transform:"

			//splitted by ';'
			size_t s1 = data.find(';');
			size_t s2 = data.find(';', s1 + 1);

			if (s1 != std::string::npos && s2 != std::string::npos)
			{
				auto parseVec3 = [](const std::string& str) -> vec3
				{
					size_t c1 = str.find(',');
					size_t c2 = str.rfind(',');
					if (c1 != std::string::npos && c2 != std::string::npos)
					{
						return vec3(
								std::stof(str.substr(0, c1)),
								std::stof(str.substr(c1 + 1, c2 - c1 - 1)),
								std::stof(str.substr(c2 + 1))
						);
					}
					//fallback if error
					return vec3{};
				};

				vec3 location = parseVec3(data.substr(0, s1));
				vec3 rotation = parseVec3(data.substr(s1 + 1, s2 - s1 - 1));
				vec3 scale    = parseVec3(data.substr(s2 + 1));

				return transform{location, rotation, scale};
			}
		}

		//string if none
		return value;
	}


	std::string GetStringByValue(const PropVariantType& value)
	{
		return std::visit([](const auto& v) -> std::string
		{
			using T = std::decay_t<decltype(v)>;

			if constexpr (std::is_same_v<T, bool>)
				return v ? "true" : "false";

			else if constexpr (std::is_same_v<T, int>)
				return std::to_string(v);

			else if constexpr (std::is_same_v<T, float>)
			{
				//to_string donne trop de zéros, on trim
				std::string s = std::to_string(v);
				s.erase(s.find_last_not_of('0') + 1, std::string::npos);
				if (s.back() == '.') s += '0';  //garde au moins "1.0"
				return s;
			}

			else if constexpr (std::is_same_v<T, vec2>)
				return "vec2:" + std::to_string(v.x) + "," + std::to_string(v.y);

			else if constexpr (std::is_same_v<T, vec3>)
				return "vec3:" + std::to_string(v.x) + "," +
												 std::to_string(v.y) + "," +
												 std::to_string(v.z);

			else if constexpr (std::is_same_v<T, vec4>)
				return "vec4:" + std::to_string(v.x) + "," +
												 std::to_string(v.y) + "," +
												 std::to_string(v.z) + "," +
												 std::to_string(v.w);

			else if constexpr (std::is_same_v<T, transform>)
			{
				auto vec3str = [](const vec3& u) {
					return std::to_string(u.x) + "," +
								 std::to_string(u.y) + "," +
								 std::to_string(u.z);
				};
				return "transform:" + vec3str(v.location) + ";" +
															vec3str(v.rotation) + ";" +
															vec3str(v.scale);
			}

			else if constexpr (std::is_same_v<T, std::string>)
				return v;

			else
				return ""; //unknown type

		}, value);
	}


	std::string PropertyToString(const property& prop)
	{
		return std::visit([](const auto* ptr) -> std::string
		{
			if (!ptr) return "";
			return GetStringByValue(*ptr);  //déréférence le pointeur
		}, prop.property_member);
	}


	Object::Object()
	{
		HPROPERTY(object_id_, Exposed);
	}

	Object::~Object()
	{
		//Delete every observable pointer of properties
		for (const auto& p : properties_)
		{
			delete p.second.observable;
		}
	}

	void Object::Init()
	{
		//call tous les callback des properties
		for (const auto& p : properties_)
		{
			if (p.second.observable)
			{
				p.second.observable->Tick();
			}
		}
	}

	void Object::StartGame() {}
	void Object::EndGame() {}

	void Object::Tick(double _dt) {}
	void Object::Update(double _dt)
	{
		//call Tick method of Observable for each properties
		for (const auto& p : properties_)
		{
			if (p.second.observable)
			{
				p.second.observable->Tick();
			}
		}
	}


	bool Object::PropertyExists(const char* _name)
	{
		auto it = properties_.find(_name);
		return it != properties_.end();
	}

	void Object::SetPropertyValueStr(const char* _name, const char* _val)
	{
		auto it = properties_.find(_name);
		if (it == properties_.end()) return;

		PropVariantType variant_val = GetValueByString(_val);

		std::visit([&](auto* ptr) {
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr) return;

				if (auto* v = std::get_if<T>(&variant_val))
				{
						*ptr = *v; // type exact
				}
				// conversions numériques
				else if constexpr (std::is_same_v<T, float>)
				{
						if (auto* v = std::get_if<int>(&variant_val))
							*ptr = static_cast<float>(*v);
				}
				else if constexpr (std::is_same_v<T, int>)
				{
					if (auto* v = std::get_if<float>(&variant_val))
						*ptr = static_cast<int>(*v);
				}
}, it->second.property_member);
	}


	Access Object::GetPropertyAccess(const char* _name)
	{
		auto it = properties_.find(_name);
		if (it == properties_.end())
		{
			return {};
		}
		return it->second.access;
	}

	const ordered_map<std::string, property>& Object::GetProperties() const
	{
		return properties_;
	}

	void Object::AddDynamicProperty(const std::string& name, const PropVariantType& value, Access access)
	{
		if (properties_.contains(name))
			return;

		dynamic_values_.push_back(std::make_unique<PropVariantType>(value));
		PropVariantType& stored = *dynamic_values_.back();

		property p{};
		p.access = access;
		p.observable = nullptr;
		p.property_member = std::visit([](auto& v) -> PropVariantTypePtr { return &v; }, stored);

		properties_[name] = p;
	}

	bool Object::CallFunctionByName(const std::string& name, const std::vector<PropVariantType>& args,
	                                std::optional<PropVariantType>* result)
	{
		if (!functions_.contains(name))
			return false;

		auto r = functions_[name].call(args);

		if (result)
			*result = std::move(r);

		return true;
	}

	std::string Object::GetTypeName() const
	{
		if (!script_class_.empty())
			return script_class_;

		// Engine classes (lynx::PointLightActor...) : registered and saved
		// without the namespace.
		std::string name = ETypeName(*this);
		if (name.rfind("lynx::", 0) == 0)
			name.erase(0, 6);
		return name;
	}
}
