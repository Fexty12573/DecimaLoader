#pragma once
#include <span>

#include "RTTI.h"
#include "RTTIClass.h"
#include "RTTIObject.h"
#include "RTTIPrimitive.h"

#ifdef __cplusplus
struct StringView;
class RTTIClass;
#else
struct RTTIClass;
#endif;
struct RTTIBaseClass;
struct RTTIClassField;
struct RTTIOrderedClassField;

struct RTTIMessageHandler {
    RTTI* MessageType;
    void* Handler;
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

#ifdef __cplusplus
struct RTTIOrderedClassField : RTTIClassField {
#else
struct RTTIOrderedClassField {
    RTTIClassField base;
#endif
    const RTTIClass* Parent;
    const char* Category;
};

#ifdef __cplusplus
using FromStringDelegate = bool(*)(RTTIObject* obj, const StringView& src);
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
class RTTIClass : public RTTI {
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
    RTTIOrderedClassField* OrderedFields;
    uint32_t OrderedFieldCount;
    RTTIMessageHandler MsgReadBinary;
    uint32_t VtableOffset;

    bool instanceof(const RTTI* type) const;
    bool instanceof(const char* type) const;

    RTTIClassField* find_field(const char* name) const;
    RTTIOrderedClassField* find_ordered_field(const char* name) const;
    std::span<const RTTIClassField> get_fields() const { return { Fields, MemberCount }; }
    std::span<const RTTIOrderedClassField> get_ordered_fields() const { return { OrderedFields, (size_t)OrderedFieldCount }; }

    RTTI_TYPEID(RTTIClass);
};
ASSERT_SIZEOF(RTTIClass, 0xB0);
ASSERT_OFFSET(RTTIClass, MessageHandlers, 0x68);

struct RTTIBaseClass {
    RTTI* Type;
    uint32_t Offset;
};
