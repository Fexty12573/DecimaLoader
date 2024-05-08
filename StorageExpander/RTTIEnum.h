#pragma once
#include "RTTI.h"

struct RTTIEnumValue {
    uint64_t Value;
    const char* Name;
    const char* Aliases[4];
};

#ifdef __cplusplus
class RTTIEnum : RTTI {
public:
#else
struct RTTIEnum {
    RTTI base;
#endif
    uint16_t ValueCount;
    const char* Name;
    RTTIEnumValue* Values;
    RTTI* RepresentationType;

    RTTI_TYPEID(RTTIEnum);
};
