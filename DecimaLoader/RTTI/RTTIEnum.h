#pragma once
#include <string_view>

#include "RTTI.h"

struct RTTIEnumValue {
    uint64_t Value;
    const char* Name;
    const char* Aliases[4];
};

#ifdef __cplusplus
class RTTIEnum : public RTTI {
public:
#else
struct RTTIEnum {
    RTTI base;
#endif
    uint16_t ValueCount;
    const char* Name;
    RTTIEnumValue* Values;
    RTTI* RepresentationType;

    RTTIEnumValue* find_value(std::string_view name) const {
        for (uint16_t i = 0; i < ValueCount; ++i) {
            if (name == Values[i].Name) {
                return &Values[i];
            }
        }
        return nullptr;
    }

    RTTIEnumValue* find_value(uint64_t value) const {
        for (uint16_t i = 0; i < ValueCount; ++i) {
            if (value == Values[i].Value) {
                return &Values[i];
            }
        }
        return nullptr;
    }

    std::string_view to_string(uint64_t value) const {
        const auto enum_value = find_value(value);
        if (enum_value) {
            return enum_value->Name;
        }
        return "N/A";
    }

    template<typename T> requires std::integral<T> || std::is_enum_v<T>
    static void ctor(T& obj) {
        new (&obj) T();
    }

    static void ctor(const RTTI* type, void* obj) {
        std::memset(obj, 0, type->EnumSize);
    }

    RTTI_TYPEID(RTTIEnum);
};

inline void RTTIEnum_ctor(const RTTI* type, void* obj) {
    std::memset(obj, 0, type->EnumSize);
}
