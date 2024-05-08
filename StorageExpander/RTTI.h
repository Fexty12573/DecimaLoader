#pragma once

#include <cstdint>
#include <cstddef>


#ifdef __cplusplus
#define ASSERT_SIZEOF(TYPE, SIZE) static_assert(sizeof(TYPE) == (SIZE), "sizeof(" #TYPE ") != " #SIZE)
#define ASSERT_OFFSET(TYPE, MEMBER, OFFSET) static_assert(offsetof(TYPE, MEMBER) == (OFFSET), "offsetof(" #TYPE ", " #MEMBER ") != " #OFFSET)

#define RTTI_TYPEID(TYPE) static constexpr const char* TypeName = #TYPE
#else
#define ASSERT_SIZEOF(TYPE, SIZE)
#define ASSERT_OFFSET(TYPE, MEMBER, OFFSET)
#define RTTI_TYPEID(TYPE)
#endif

#ifdef __cplusplus
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
#endif

class RTTIPrimitive;
class RTTIReference;
class RTTIContainer;
class RTTIEnum;
class RTTIClass;

struct RTTI {
    int32_t TypeId;
    RTTIKind Kind;
    uint8_t FactoryFlags;
    union {
        uint16_t AtomSize;
        struct {
            uint8_t BaseClassCount;
            uint8_t MemberCount;
        };
        struct {
            uint8_t EnumSize;
            uint8_t EnumAlignment;
        };
    };

#ifdef __cplusplus
    RTTIPrimitive* as_primitive();
    RTTIReference* as_reference();
    RTTIContainer* as_container();
    RTTIEnum* as_enum();
    RTTIClass* as_class();

    const RTTIPrimitive* as_primitive() const;
    const RTTIReference* as_reference() const;
    const RTTIContainer* as_container() const;
    const RTTIEnum* as_enum() const;
    const RTTIClass* as_class() const;

    const char* name() const;
#endif
};
ASSERT_SIZEOF(RTTI, 8);
