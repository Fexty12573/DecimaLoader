#pragma once

#include "RTTI.h"

struct RTTIContainerData {
    const char* TypeName;
    uint16_t Size;
    uint8_t Alignment;
    bool IsArray;

    void* Constructor;
    void* Destructor;
    void* Resize;
    void* Insert;
    void* Erase;
    void* GetSize;
    void* GetItem;
    void* Unknown0[7];
    void* TryPut;
    void* Unknown00;
    void* Clear;
    void* Unknown1[5];
};
ASSERT_SIZEOF(RTTIContainerData, 0xC0);
ASSERT_OFFSET(RTTIContainerData, Constructor, 0x10);
ASSERT_OFFSET(RTTIContainerData, TryPut, 0x80);

#ifdef __cplusplus
class RTTIContainer : public RTTI {
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

