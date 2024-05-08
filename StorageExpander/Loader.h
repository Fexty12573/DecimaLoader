#pragma once

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#define PLUGIN_API extern "C" __declspec(dllexport)

#ifdef __cplusplus

#ifndef LOADER_NO_DEFINE_RTTI
enum class RTTIKind : uint8_t {
    Primitive,
    Reference,
    Container,
    Enum,
    Class,
    EnumFlags,
    Pod,
    EnumBitSet
};

struct RTTI {
    int32_t TypeId;
    RTTIKind Kind;
    uint8_t FactoryFlags;
    union {
        uint16_t PrimitiveSize;
        struct {
            uint8_t BaseClassCount;
            uint8_t MemberCount;
        };
        struct {
            uint8_t EnumSize;
            uint8_t EnumAlignment;
        };
    };
};
#endif

using OnWinMain_t = void(*)();
using OnPreRegisterType_t = void(*)(RTTI* type);
using OnPostRegisterType_t = void(*)(RTTI* type);
using OnPreRegisterAllTypes_t = void(*)();
using OnPostRegisterAllTypes_t = void(*)();

struct PluginInitializeOptions {
    OnWinMain_t OnWinMain;
    OnPreRegisterType_t OnPreRegisterType;
    OnPostRegisterType_t OnPostRegisterType;
    OnPreRegisterAllTypes_t OnPreRegisterAllTypes;
    OnPostRegisterAllTypes_t OnPostRegisterAllTypes;
};

#else

#ifndef LOADER_NO_DEFINE_RTTI
typedef enum RTTIKind {
    RTTIKind_Primitive,
    RTTIKind_Reference,
    RTTIKind_Container,
    RTTIKind_Enum,
    RTTIKind_Class,
    RTTIKind_EnumFlags,
    RTTIKind_Pod,
    RTTIKind_EnumBitSet
} RTTIKind;

typedef struct RTTI {
    int32_t TypeId;
    uint8_t Kind; // RTTIKind
    uint8_t FactoryFlags;
    union {
        uint16_t PrimitiveSize;
        struct {
            uint8_t BaseClassCount;
            uint8_t MemberCount;
        };
        struct {
            uint8_t EnumSize;
            uint8_t EnumAlignment;
        };
    };
} RTTI;
#endif

typedef void(*OnWinMain_t)();
typedef void(*OnPreRegisterType_t)(RTTI* type);
typedef void(*OnPostRegisterType_t)(RTTI* type);
typedef void(*OnPreRegisterAllTypes_t)();
typedef void(*OnPostRegisterAllTypes_t)();

typedef struct PluginInitializeOptions {
    OnWinMain_t OnWinMain;
    OnPreRegisterType_t OnPreRegisterType;
    OnPostRegisterType_t OnPostRegisterType;
    OnPreRegisterAllTypes_t OnPreRegisterAllTypes;
    OnPostRegisterAllTypes_t OnPostRegisterAllTypes;
} PluginInitializeOptions;

#endif
