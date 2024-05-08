#pragma once
#include "RTTI.h"

struct RTTIReferenceData {
    const char* TypeName;
    uint32_t Size;
    uint8_t Alignment;
    uint8_t Pad0[3];
    void* Constructor;
    void* Destructor;
    void* Get;
    void* Set;
    void* Copy;
};

#ifdef __cplusplus
class RTTIReference : RTTI {
public:
#else
struct RTTIReference {
    RTTI base;
#endif
    RTTI* ReferenceType;
    RTTIReferenceData* Data;
    const char* Name;

    RTTI_TYPEID(RTTIReference);
};
