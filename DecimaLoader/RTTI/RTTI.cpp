#include "pch.h"
#include "RTTI.h"

#include "RTTIPrimitive.h"
#include "RTTIReference.h"
#include "RTTIContainer.h"
#include "RTTIEnum.h"
#include "RTTIClass.h"

RTTIPrimitive* RTTI::as_primitive() {
    return reinterpret_cast<RTTIPrimitive*>(this);
}

RTTIReference* RTTI::as_reference() {
    return reinterpret_cast<RTTIReference*>(this);
}

RTTIContainer* RTTI::as_container() {
    return reinterpret_cast<RTTIContainer*>(this);
}

RTTIEnum* RTTI::as_enum() {
    return reinterpret_cast<RTTIEnum*>(this);
}

RTTIClass* RTTI::as_class() {
    return reinterpret_cast<RTTIClass*>(this);
}

const RTTIPrimitive* RTTI::as_primitive() const {
    return reinterpret_cast<const RTTIPrimitive*>(this);
}

const RTTIReference* RTTI::as_reference() const {
    return reinterpret_cast<const RTTIReference*>(this);
}

const RTTIContainer* RTTI::as_container() const {
    return reinterpret_cast<const RTTIContainer*>(this);
}

const RTTIEnum* RTTI::as_enum() const {
    return reinterpret_cast<const RTTIEnum*>(this);
}

const RTTIClass* RTTI::as_class() const {
    return reinterpret_cast<const RTTIClass*>(this);
}

const char* RTTI::name() const {
    switch (Kind) {
    case RTTIKind::Primitive: return as_primitive()->Name;
    case RTTIKind::Reference: return as_reference()->Name;
    case RTTIKind::Container: return as_container()->Name;
    case RTTIKind::EnumFlags:  [[fallthrough]];
    case RTTIKind::EnumBitSet: [[fallthrough]];
    case RTTIKind::Enum: return as_enum()->Name;
    case RTTIKind::Class: return as_class()->Name;
    case RTTIKind::Pod: break;
    }

    return "N/A";
}

void* RTTI::get_constructor_impl() const {
    switch (Kind) {
    case RTTIKind::Primitive: return as_primitive()->Constructor;
    case RTTIKind::Reference: return as_reference()->Data->Constructor;
    case RTTIKind::Container: return as_container()->Data->Constructor;
    case RTTIKind::Enum: [[fallthrough]];
    case RTTIKind::EnumFlags: [[fallthrough]];
    case RTTIKind::EnumBitSet: return RTTIEnum_ctor;
    case RTTIKind::Class: return as_class()->Constructor;
    case RTTIKind::Pod: return nullptr;
    }

    return nullptr;
}
