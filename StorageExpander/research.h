#pragma once

#include "Types.h"
#include "RTTI.h"
#include "RTTIClass.h"

#include <MinHook.h>
#include <span>

namespace research {

void init(std::span<RTTI* const> all_types);

template<typename TRet, typename ...TArgs>
bool hook_message(RTTIClass* cls, const char* message, TRet(*hook)(TArgs...), TRet(**original)(TArgs...)) {
    *original = nullptr;

    if (cls == nullptr || message == nullptr || hook == nullptr || original == nullptr) {
        return false;
    }

    if (cls->MessageHandlers == nullptr || cls->MessageHandlerCount == 0) {
        return false;
    }

    for (uint8_t i = 0; i < cls->MessageHandlerCount; i++) {
        if (cls->MessageHandlers[i].MessageType == nullptr || 
            cls->MessageHandlers[i].Handler == nullptr) {
            continue;
        }

        if (strcmp(cls->MessageHandlers[i].MessageType->as_class()->Name, message) == 0) {
            const auto handler = cls->MessageHandlers[i].Handler;
            MH_CreateHook(handler, (void*)hook, (void**)original);
            MH_EnableHook(handler);
            return true;
        }
    }

    return false;
}

}
