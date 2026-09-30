#ifndef HGE_DATA_ENUM_H
#define HGE_DATA_ENUM_H

#include <cstdint>

#include <span>
#include <string_view>


namespace lynx
{
	/**
	struct IDataEnum {
		virtual ~IDataEnum()=default;
	};

	template<typename E>
	struct DataEnum : public IDataEnum {
		DataEnum(int32_t val) : value(val) {}

		int32_t Val() const { return value; }

		bool operator==(int32_t val) const { return value == val; }
		bool operator!=(int32_t val) const { return value != val; }

	private:
		E enum_inst_;
		int32_t value;
	};
	*/

/**
	struct DataEnum {
		DataEnum(int32_t val, std::span<const std::string_view> names) {}

		bool operator==(const int32_t val) const { return value_ == val; }
		bool operator!=(const int32_t val) const { return value_ != val; }

		bool operator==(const char* val) const { return names[value_] == val; }
		bool operator==(const std::string& val) const { return names[value_] == val; }
		bool operator==(const std::string_view val) const { return names[value_] == val; }

		int32_t Val() const { return value_; }

	private:
		int32_t value_;
		std::span<const std::string_view> names;
	};

#define HENUM_REGISTER(EnumName, ...)                                        \
	inline constexpr std::string_view EnumName##_Names[] = { __VA_ARGS__ };   \
	inline hge::DataEnum Make##EnumName(int32_t val)                           \
	{                                                                           \
		return hge::DataEnum(val, EnumName##_Names);                             \
	}
*/
}

#endif