#pragma once
#include "RTTI.h"
#include "Types.h"

#ifdef __cplusplus
using DeserializeDelegate = bool(*)(const String& src, void* obj);
using SerializeDelegate = bool(*)(const void* obj, String& dst);
using AssignValueDelegate = void(*)(void* dst, const void* src);
using IsEqualDelegate = bool(*)(const void* lhs, const void* rhs);
using ConstructorDelegate = void(*)(RTTI* rtti, void* obj);
using DestructorDelegate = void(*)(RTTI* rtti, void* obj);
using SwapEndiannessDelegate = bool(*)(const void* src, void* dst, uint8_t type);
using TryAssignValueDelegate = bool(*)(void* dst, const void* src);
using GetSizeDelegate = size_t(*)(const void* obj);
using CompareByStringsDelegate = bool(*)(const void* obj, const char* lhs, const char* rhs);
#else
typedef bool(*DeserializeDelegate)(const String* src, void* obj);
typedef bool(*SerializeDelegate)(const void* obj, String* dst);
typedef void(*AssignValueDelegate)(void* dst, const void* src);
typedef bool(*IsEqualDelegate)(const void* lhs, const void* rhs);
typedef void(*ConstructorDelegate)(RTTI* rtti, void* obj);
typedef void(*DestructorDelegate)(RTTI* rtti, void* obj);
typedef bool(*SwapEndiannessDelegate)(const void* src, void* dst, uint8_t type);
typedef bool(*TryAssignValueDelegate)(void* dst, const void* src);
typedef size_t(*GetSizeDelegate)(const void* obj);
typedef bool(*CompareByStringsDelegate)(const void* obj, const char* lhs, const char* rhs);
#endif

#ifdef __cplusplus
class RTTIPrimitive : RTTI {
public:
#else
struct RTTIPrimitive {
    RTTI base;
#endif
    uint8_t Alignment;
    bool IsSimple;
    uint8_t Pad0[6];
    const char* Name;
    RTTI* BaseType;
    DeserializeDelegate Deserialize;
    SerializeDelegate Serialize;
    void* UnknownFunction;
    AssignValueDelegate Assign;
    IsEqualDelegate Equals;
    ConstructorDelegate Constructor;
    DestructorDelegate Destructor;
    SwapEndiannessDelegate SwapEndianness;
    TryAssignValueDelegate TryAssign;
    GetSizeDelegate GetSize;
    CompareByStringsDelegate CompareByStrings;
    RTTI* RepresentationType;

    RTTI_TYPEID(RTTIPrimitive);
};

