#include "pch.h"
#include "research.h"


namespace research {

std::span<RTTI* const> g_all_types;

RTTI* find_rtti(RTTIKind type, const char* name);
void init_craftable_item_component_resource();
}

void research::init(std::span<RTTI* const> all_types) {
    g_all_types = all_types;
    init_craftable_item_component_resource();
}


RTTI* research::find_rtti(RTTIKind type, const char* name) {
    for (const auto rtti : g_all_types) {
        if (rtti->Kind == type && strcmp(rtti->name(), name) == 0) {
            return rtti;
        }
    }

    return nullptr;
}

void research::init_craftable_item_component_resource() {
    const auto rtti = find_rtti(RTTIKind::Class, "CraftableItemComponentResource");
    if (!rtti) {
        return;
    }

    static void(*original)(void* cicr) = nullptr;
    static auto init_hook = [](void* cicr) {
        original(cicr);
        // Do stuff
    };

    hook_message(rtti->as_class(), "MsgInit", (decltype(original))init_hook, &original);
}
