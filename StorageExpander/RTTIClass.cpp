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
