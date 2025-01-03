#pragma once

#include "RTTI.h"

#ifdef __cplusplus

class RTTIObject {
public:
    virtual RTTI* get_rtti() const = 0;
    virtual ~RTTIObject() = 0;
};

class RTTIRefObject : public RTTIObject {
public:
    GUID ObjectUUID;
    uint32_t RefCount;
};

#else

typedef void*(RTTIObject_GetRTTI)(void* this);
typedef void(RTTIObject_Destructor)(void* this);

struct RTTIObject_vtable {
    RTTIObject_GetRTTI* get_rtti;
    RTTIObject_Destructor* destructor;
};

struct RTTIObject {
    RTTIObject_vtable* vft;
};

struct RTTIRefObject {
    RTTIObject base;
    GUID ObjectUUID;
};

#endif
