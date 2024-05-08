#pragma once
#include "RTTI.h"
#include "RTTIObject.h"
#include "RTTIPrimitive.h"

#ifdef __cplusplus
class RTTIClass;
#else
struct RTTIClass;
#endif;
struct RTTIBaseClass;
struct RTTIClassField;
struct RTTIMessageHandler;

#ifdef __cplusplus
using FromStringDelegate = bool(*)(RTTIObject* obj, const char* src);
using ToStringDelegate = bool(*)(const RTTIObject* obj, char* dst);
using FromStringViewDelegate = bool(*)(RTTIObject* obj, const StringView& src);
using GetExportedSymbolsDelegate = RTTIClass* (*)();
#else
typedef bool(*FromStringDelegate)(RTTIObject* obj, const char* src);
typedef bool(*ToStringDelegate)(const RTTIObject* obj, char* dst);
typedef bool(*FromStringViewDelegate)(RTTIObject* obj, const void* src);
typedef RTTIClass* (*GetExportedSymbolsDelegate)();
#endif


#ifdef __cplusplus
class RTTIClass : RTTI {
public:
#else
struct RTTIClass {
    RTTI base;
#endif
    uint8_t MessageHandlerCount;
    uint32_t Version;
    uint32_t Size;
    uint16_t Alignment;
    uint16_t Flags;

    ConstructorDelegate Constructor;
    DestructorDelegate Destructor;
    FromStringDelegate Deserialize;
    FromStringViewDelegate DeserializeView;
    ToStringDelegate Serialize;

    const char* Name;
    RTTI* Prev;
    RTTI* Next;
    RTTIBaseClass* BaseClasses;
    RTTIClassField* Fields;
    RTTIMessageHandler* MessageHandlers;
    void* Unknown0;
    GetExportedSymbolsDelegate GetExportedSymbols;
    RTTI* RepresentationType;
    void* Unknown1[3];
    void* OnReadMessageBinary;
    uint32_t VtableOffset;

    RTTI_TYPEID(RTTIClass);
};
ASSERT_OFFSET(RTTIClass, MessageHandlers, 0x68);

struct RTTIBaseClass {
    RTTI* Type;
    uint32_t Offset;
};

struct RTTIClassField {
    RTTI* Type;
    uint16_t Offset;
    uint16_t Flags;
    const char* Name;
    void* Get;
    void* Set;
    const char* MinValue;
    const char* MaxValue;
};

struct RTTIMessageHandler {
    RTTI* MessageType;
    void* Handler;
};
