#pragma once

#include "RTTI.h"

struct RTTIContainerData {
    const char* TypeName;
    uint16_t Size;
    uint8_t Alignment;

    void* Constructor;
    void* Destructor;
    void* Unknown0[3];
    void* GetItemCount;
    void* Unknown1[7];
};
ASSERT_SIZEOF(RTTIContainerData, 0x78);
ASSERT_OFFSET(RTTIContainerData, Constructor, 0x10);

#ifdef __cplusplus
class RTTIContainer : RTTI {
public:
#else
struct RTTIContainer {
    RTTI base;
#endif
    RTTI* ItemType;
    RTTIContainerData* Data;
    const char* Name;

    RTTI_TYPEID(RTTIContainer);
};

