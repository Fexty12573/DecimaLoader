#include "pch.h"
#include "RTTIClass.h"

bool RTTIClass::RTTIClass::instanceof(const RTTI* type) const {
    if (this == type) {
        return true;
    }

    for (uint8_t i = 0; i < BaseClassCount; ++i) {
        if (BaseClasses[i].Type->Kind == RTTIKind::Class && BaseClasses[i].Type->as_class()->instanceof(type)) {
            return true;
        }
    }

    return false;
}

bool RTTIClass::RTTIClass::instanceof(const char* type) const {
    if (strcmp(Name, type) == 0) {
        return true;
    }

    for (uint8_t i = 0; i < BaseClassCount; ++i) {
        if (BaseClasses[i].Type->Kind == RTTIKind::Class && BaseClasses[i].Type->as_class()->instanceof(type)) {
            return true;
        }
    }

    return false;
}

RTTIClassField* RTTIClass::RTTIClass::find_field(const char* name) const {
    if (!Fields) {
        return nullptr;
    }

    for (uint8_t i = 0; i < MemberCount; ++i) {
        if (Fields[i].Name && strcmp(Fields[i].Name, name) == 0) {
            return &Fields[i];
        }
    }

    return nullptr;
}

RTTIOrderedClassField* RTTIClass::RTTIClass::find_ordered_field(const char* name) const {
    if (!OrderedFields) {
        return nullptr;
    }

    for (uint32_t i = 0; i < OrderedFieldCount; ++i) {
        if (OrderedFields[i].Name && strcmp(OrderedFields[i].Name, name) == 0) {
            return &OrderedFields[i];
        }
    }

    return nullptr;
}
