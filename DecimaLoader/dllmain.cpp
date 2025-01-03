#include "pch.h"

#include <combaseapi.h>

#include "PatternScan.h"
#include "Loader.h"
#include "RTTI/RTTI.h"
#include "Decima/Types.h"
#include "ObjectDumper.h"

#include <MinHook.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <ranges>
#include <regex>
#include <shared_mutex>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "RTTI/RTTIClass.h"
#include "RTTI/RTTIEnum.h"
#include "RTTI/RTTIObject.h"
#include "RTTI/RTTIReference.h"


std::unordered_map<std::string, RTTI*> type_map;

namespace {

template<typename Ret, typename ...Args>
void create_hook(void* addr, Ret(*hook)(Args...), Ret(**orig)(Args...)) {
    MH_CreateHook(addr, (void*)hook, (void**)orig);
    MH_QueueEnableHook(addr);
}

#define HookLambda(TARGET, LAMBDA) \
    do { \
        static decltype(TARGET) TARGET##_original; \
        create_hook((void*)(TARGET), (decltype(TARGET))(LAMBDA), &TARGET##_original); \
    } while (0)

constexpr auto winmain_pattern = "48 83 EC 28 49 8B D0 E8 ? ? ? ? 84 C0 75 0A B8 01 00 00 00 48 83 C4 28 C3";
constexpr auto register_type_pattern = "40 55 53 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 0F B6 42 05 48 8B DA 48 8B";
constexpr auto register_all_types_pattern = "40 55 48 8B EC 48 83 EC 70 80 3D ? ? ? ? ? 0F 85 ? ? ? ? 48 89 9C 24";
constexpr auto read_object_pattern = "48 89 5C 24 08 57 48 83 EC 20 4C 8B 89 18 01 15 00 48 8B F9 4D 63 41 30";
constexpr auto alloc_mem_pattern = "48 8B 01 48 8B D7 48 8B 5C 24 30 48 83 C4 20 5F 48 FF A0 88 00 00 00";
constexpr auto win32_system_ctor_pattern = "48 89 05 ? ? ? ? 48 8D 05 ? ? ? ? 88 0D";
constexpr auto ref_object_release_pattern = "40 53 48 83 EC 20 48 8B D9 B8 FF FF FF FF F0 0F C1 41 18 25 FF FF 3F 00";

constexpr auto enum_value_from_string_pattern = "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56 48 8b 71 18";
constexpr auto float_to_half_pattern = "C4 C1 79 7E C1 41 8B D1 45 8B D9 81 E2 00 00 80 7F 41 C1 E9 10";

const auto indexer_regex = std::regex(R"((\w+)\[([^\]]+)\])");

void* win32_system_instance = nullptr;

std::vector<PluginInitializeOptions> plugin_options;
std::unordered_map<GUID, nlohmann::json> object_patches;
RTTI* rtti_ref_object_rtti = nullptr;

std::unordered_set<RTTI*> integral_types;
std::unordered_set<RTTI*> floating_point_types;
std::unordered_set<RTTI*> string_types;
std::unordered_set<RTTI*> bool_types;

RTTIEnumValue* (*enum_value_from_string)(RTTIEnum* rtti, StringView* value);
uint16_t(*float_to_half)(float value);
void* (*alloc_mem)(size_t size);

std::shared_mutex deserialized_objects_mutex{};
std::unordered_map<RTTIRefObject*, nlohmann::json> delayed_patches;
std::unordered_set<void*> deserialized_objects;
std::unordered_set<void*> collectable_robots;
std::unordered_set<void*> loot_component_resources;

uint32_t get_current_language() {
    if (!win32_system_instance) {
        return 0;
    }

    return *(uint32_t*)((uint8_t*)win32_system_instance + 0x78);
}

std::vector<int> parse_index_list(const std::string& list) {
    std::vector<int> indices;
    std::stringstream ss(list);

    for (int i; ss >> i;) {
        indices.push_back(i);
        while (ss.peek() == ',' || std::isspace(ss.peek())) {
            ss.ignore();
        }
    }

    return indices;
}

std::vector<std::string> parse_enum_values(const std::string& input, char delimiter) {
    std::vector<std::string> result;
    std::istringstream stream(input);
    std::string token;

    while (std::getline(stream, token, delimiter)) {
        // Trim leading whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        // Trim trailing whitespace
        token.erase(token.find_last_not_of(" \t") + 1);
        result.push_back(token);
    }

    return result;
}

void try_patch_object(RTTIRefObject* obj, const RTTIClass* type, const nlohmann::json& patch, bool is_real_ref_obj = true);

std::string_view convert_to_decima_guid(std::string_view guid) {
    if (guid.size() == 38) {
        return guid.substr(1, 36);
    }
    return guid;
}

void assign_enum_simple(const RTTIEnum* type, void* dst, const void* src) {
    std::memcpy(dst, src, type->EnumSize);
}

void assign_enum(const RTTIEnum* type, const RTTIClassField* field, void* obj, void* src) {
    const auto dst = (uint8_t*)obj + field->Offset;
    if (field->Set) {
        ((void(*)(void*, void*))field->Set)(dst, src);
    } else {
        assign_enum_simple(type, dst, src);
    }
}

void try_patch_enum(RTTIRefObject* obj, RTTIClassField* field, const nlohmann::json& value) {
    const auto type = field->Type->as_enum();
    if (!type) {
        spdlog::warn("Failed to find enum type for field: {}", field->Name);
        return;
    }

    if (value.is_string()) {
        uint64_t final_value = 0;

        if (type->Kind == RTTIKind::EnumFlags) {
            const auto values = parse_enum_values(value.get<std::string>(), '|');
            for (const auto& val : values) {
                const auto enum_value = type->find_value(val);
                if (!enum_value) {
                    spdlog::warn("Failed to find enum value for field {}: {}", field->Name, val);
                    continue;
                }

                final_value |= enum_value->Value;
            }
        } else {
            const auto enum_value = type->find_value(value.get<std::string>());
            if (!enum_value) {
                spdlog::warn("Invalid enum value for field {}: {}", field->Name, value.get<std::string>());
                return;
            }

            final_value = enum_value->Value;
        }

        assign_enum(type, field, obj, &final_value);
    } else if (value.is_number_integer()) {
        int64_t enum_value = value.get<int64_t>();
        assign_enum(type, field, obj, &enum_value);
    } else {
        spdlog::warn("Invalid enum value for field: {}", field->Name);
    }
}

void assign_primitive_simple(const RTTIPrimitive* type, void* dst, const void* src) {
    if (type->IsSimple) {
        std::memcpy(dst, src, type->AtomSize);
    } else {
        type->Assign(dst, src);
    }
}

void assign_primitive(const RTTIPrimitive* type, const RTTIClassField* field, void* obj, void* src) {
    const auto dst = (uint8_t*)obj + field->Offset;
    if (field->Set) {
        ((void(*)(void*, void*))field->Set)(dst, src);
    } else {
        assign_primitive_simple(type, dst, src);
    }
}

void try_patch_primitive(RTTIRefObject* obj, RTTIClassField* field, const nlohmann::json& value) {
    const RTTIPrimitive* type = field->Type->as_primitive();
    RTTI* base_type = type->BaseType; // Get the base type of the primitive to handle typedefs
    if (!base_type) {
        spdlog::warn("Failed to find base type for field: {}", field->Name);
        return;
    }

    if (integral_types.contains(base_type)) {
        if (value.is_number_integer()) {
            auto int_value = value.get<int64_t>();
            assign_primitive(type, field, obj, &int_value);
        } else if (value.is_string()) {
            const StringView view(value.get<std::string>());
            uint8_t buffer[16] = {}; // Max size of an integral type
            field->Type->as_primitive()->Deserialize(view, buffer);
            assign_primitive(type, field, obj, buffer);
        }
    } else if (floating_point_types.contains(base_type)) {
        if (value.is_number_float()) {
            auto double_value = value.get<double>();
            if (base_type == type_map["HalfFloat"]) {
                auto half_value = float_to_half((float)double_value);
                assign_primitive(type, field, obj, &half_value);
            } else if (base_type == type_map["float"]) {
                auto float_value = (float)double_value;
                assign_primitive(type, field, obj, &float_value);
            } else if (base_type == type_map["double"]) {
                assign_primitive(type, field, obj, &double_value);
            }
        } else if (value.is_string()) {
            const StringView view(value.get<std::string>());
            uint8_t buffer[16] = {}; // Max size of a floating point type
            field->Type->as_primitive()->Deserialize(view, buffer);
            assign_primitive(type, field, obj, buffer);
        }
    } else if (string_types.contains(base_type)) {
        if (value.is_string()) {
            StringView view(value.get<std::string>());
            assign_primitive(type, field, obj, &view);
        } else {
            spdlog::warn("Invalid string value for field: {}", field->Name);
        }
    } else if (bool_types.contains(base_type)) {
        if (value.is_boolean()) {
            auto bool_value = value.get<bool>();
            assign_primitive(type, field, obj, &bool_value);
        } else if (value.is_string()) {
            const StringView view(value.get<std::string>());
            bool bool_value = false;
            field->Type->as_primitive()->Deserialize(view, &bool_value);
            assign_primitive(type, field, obj, &bool_value);
        }
    } else {
        spdlog::warn("Unknown primitive type for field: {} ({})", field->Name, type->Name);
    }
}

void try_patch_reference(RTTIRefObject* obj, RTTIClassField* field, const nlohmann::json& value) {
    const auto reference = field->Type->as_reference();
    if (strcmp(reference->Data->TypeName, "UUIDRef") == 0) {
        if (value.is_string()) {
            const auto uuid = value.get<std::string>();
            const StringView normalized(convert_to_decima_guid(uuid));
            GUID guid;
            type_map["GGUUID"]->as_class()->Deserialize((RTTIObject*)&guid, normalized);

            if (field->Set) {
                ((void(*)(void*, void*))field->Set)(obj, &guid);
            } else {
                const auto data = (uint8_t*)obj + field->Offset;
                std::memcpy(data, &guid, sizeof(GUID));
            }
        }
    } else if (strcmp(reference->Data->TypeName, "Ref") == 0) {
        if (value.contains("_Target")) { // Replace entire reference target
            spdlog::warn("Replacing entire reference target not supported yet: {}", field->Name);
        } else { // Modify reference target
            std::shared_lock lock(deserialized_objects_mutex);

            // No Getters/Setters for Ref<T> from what I've seen
            const auto target = *(RTTIRefObject**)((uint8_t*)obj + field->Offset);
            if (!target) {
                spdlog::warn("Failed to find reference target for field: {}", field->Name);
                return;
            }

            if (deserialized_objects.contains(target)) {
                try_patch_object(target, reference->ReferenceType->as_class(), value);
            } else {
                // Can't patch the object if it hasn't been deserialized yet
                // Delay patching until the object is deserialized
                delayed_patches[target] = value;
                spdlog::debug("Reference target not deserialized yet, delaying patch: {}", field->Name);
            }
        }
    } else {
        spdlog::warn("Unsupported reference type: {}", reference->Data->TypeName);
    }
}

void try_patch_class(RTTIRefObject* obj, RTTIClassField* field, const nlohmann::json& value, bool is_real_ref_obj = true) {
    const auto cls = field->Type->as_class();
    if (!cls) {
        spdlog::warn("Failed to find class type for field: {}", field->Name);
        return;
    }

    if (value.is_object()) {
        try_patch_object(obj, cls, value, is_real_ref_obj);
    } else {
        spdlog::warn("Invalid class value for field: {}", field->Name);
    }
}

void try_patch_primitive_array_element(const RTTIContainer* type, void* element, const nlohmann::json& value) {
    const auto item_type = type->ItemType->as_primitive();
    if (integral_types.contains(item_type)) {
        if (value.is_number_integer()) {
            const auto int_value = value.get<int64_t>();
            assign_primitive_simple(item_type, element, &int_value);
        } else if (value.is_string()) {
            const StringView view(value.get<std::string>());
            uint8_t buffer[16] = {}; // Max size of an integral type
            item_type->Deserialize(view, buffer);
            assign_primitive_simple(item_type, element, buffer);
        }

        return;
    }

    if (floating_point_types.contains(item_type)) {
        if (value.is_number_float()) {
            const auto double_value = value.get<double>();
            if (item_type == type_map["HalfFloat"]) {
                const auto half_value = float_to_half((float)double_value);
                assign_primitive_simple(item_type, element, &half_value);
            } else if (item_type == type_map["float"]) {
                const auto float_value = (float)double_value;
                assign_primitive_simple(item_type, element, &float_value);
            } else if (item_type == type_map["double"]) {
                assign_primitive_simple(item_type, element, &double_value);
            }
        } else if (value.is_string()) {
            const StringView view(value.get<std::string>());
            uint8_t buffer[16] = {}; // Max size of a floating point type
            item_type->Deserialize(view, buffer);
            assign_primitive_simple(item_type, element, buffer);
        }

        return;
    }

    if (string_types.contains(item_type)) {
        if (value.is_string()) {
            const StringView view(value.get<std::string>());
            assign_primitive_simple(item_type, element, &view);
        } else {
            spdlog::warn("Invalid string value for array");
        }

        return;
    }

    if (bool_types.contains(item_type)) {
        if (value.is_boolean()) {
            const auto bool_value = value.get<bool>();
            assign_primitive_simple(item_type, element, &bool_value);
        } else if (value.is_string()) {
            const StringView view(value.get<std::string>());
            bool bool_value = false;
            item_type->Deserialize(view, &bool_value);
            assign_primitive_simple(item_type, element, &bool_value);
        }

        return;
    }


    spdlog::warn("Unknown primitive type for array: {}", type->Name);
}

void try_patch_enum_array_element(const RTTIContainer* type, void* element, const nlohmann::json& value) {
    const auto item_type = type->ItemType->as_enum();
    if (!item_type) {
        spdlog::warn("Failed to find enum type for array");
        return;
    }

    if (value.is_string()) {
        uint64_t final_value = 0;

        if (type->Kind == RTTIKind::EnumFlags) {
            const auto values = parse_enum_values(value.get<std::string>(), '|');
            for (const auto& val : values) {
                const auto enum_value = item_type->find_value(val);
                if (!enum_value) {
                    spdlog::warn("Failed to find enum value for array: {}", val);
                    continue;
                }

                final_value |= enum_value->Value;
            }
        } else {
            const auto enum_value = item_type->find_value(value.get<std::string>());
            if (!enum_value) {
                spdlog::warn("Invalid enum value for array: {}", value.get<std::string>());
                return;
            }

            final_value = enum_value->Value;
        }

        assign_enum_simple(item_type, element, &final_value);
    } else if (value.is_number_integer()) {
        const auto enum_value = value.get<int64_t>();
        assign_enum_simple(item_type, element, &enum_value);
    } else {
        spdlog::warn("Invalid enum value for array");
    }
}

void try_patch_reference_array_element(const RTTIContainer* type, void* element, const nlohmann::json& value) {
    const auto reference = type->ItemType->as_reference();
    if (strcmp(reference->Data->TypeName, "UUIDRef") == 0) {
        if (value.is_string()) {
            const auto uuid = value.get<std::string>();
            const StringView normalized(convert_to_decima_guid(uuid));
            // Deserialize directly into the element
            type_map["GGUUID"]->as_class()->Deserialize((RTTIObject*)element, normalized);
        }
    } else if (strcmp(reference->Data->TypeName, "Ref") == 0) {
        if (value.contains("_Target")) { // Replace entire reference target
            spdlog::warn("Replacing entire reference target not supported yet [array]");
        } else { // Modify reference target
            // No Getters/Setters for Ref<T> from what I've seen
            const auto target = *(RTTIRefObject**)element;
            if (deserialized_objects.contains(target)) {
                try_patch_object(target, reference->ReferenceType->as_class(), value);
            } else {
                // Can't patch the object if it hasn't been deserialized yet
                // Delay patching until the object is deserialized
                delayed_patches[target] = value;
                spdlog::debug("Reference target not deserialized yet, delaying patch [array]");
            }
        }
    } else {
        spdlog::warn("Unsupported reference type: {}", reference->Data->TypeName);
    }
}

void try_patch_class_array_element(const RTTIContainer* type, void* element, const nlohmann::json& value) {
    const auto cls = type->ItemType->as_class();
    if (!cls) {
        spdlog::warn("Failed to find class type for array");
        return;
    }
    if (value.is_object()) {
        try_patch_object((RTTIRefObject*)element, cls, value, cls->instanceof(rtti_ref_object_rtti));
    } else {
        spdlog::warn("Invalid class value for array");
    }
}

void try_patch_container(RTTIRefObject* obj, RTTIClassField* field, const nlohmann::json& value) {
    const auto container = field->Type->as_container();
    if (!container) {
        spdlog::warn("Failed to find container type for field: {}", field->Name);
        return;
    }

    if (strcmp(container->Data->TypeName, "Array") == 0) {
        if (!value.is_array()) {
            spdlog::warn("Invalid array value for field: {}", field->Name);
            return;
        }

        Array<void*> array;
        const auto field_data = (uint8_t*)obj + field->Offset;
        if (field->Get) {
            ((void(*)(void*, void*))field->Get)(field_data, &array);
        } else {
            std::memcpy(&array, field_data, sizeof(Array<void*>));
        }

        if (array.size() != value.size()) {
            spdlog::warn("Array size mismatch for field: {} ({} != {})", field->Name, array.size(), value.size());
            return;
        }

        const auto get_item = (void* (*)(const RTTIContainer*, Array<void*>*, size_t))container->Data->GetItem;
        for (size_t i = 0; i < array.size(); ++i) {
            const auto element = get_item(container, &array, i);
            if (!element) {
                spdlog::warn("Failed to find array element: {}[{}]", field->Name, i);
                continue;
            }

            switch (container->ItemType->Kind) {
            case RTTIKind::Primitive:
                try_patch_primitive_array_element(container, element, value[i]);
                break;
            case RTTIKind::Reference:
                try_patch_reference_array_element(container, element, value[i]);
                break;
            case RTTIKind::Container:
                spdlog::warn("Skipping container array element: {}", field->Name);
                break;
            case RTTIKind::Enum:
                try_patch_enum_array_element(container, element, value[i]);
                break;
            case RTTIKind::Class:
                try_patch_class_array_element(container, element, value[i]);
                break;
            case RTTIKind::EnumFlags:
                try_patch_enum_array_element(container, element, value[i]);
                break;
            case RTTIKind::Pod:
                spdlog::warn("Skipping pod array element: {}", field->Name);
                break;
            case RTTIKind::EnumBitSet:
                try_patch_enum_array_element(container, element, value[i]);
                break;
            }
        }
    }

    if (std::string_view{ container->Data->TypeName }.starts_with("HashMap_")) {
        if (container->ItemType->Kind != RTTIKind::Class) {
            spdlog::warn("HashMap item type is not a class");
            return;
        }

        if (!value.is_array()) {
            spdlog::warn("Invalid hashmap value for field: {}", field->Name);
            return;
        }

        struct {
            void* Data;
            int Size;
            int Capacity;
        } hash_map;

        const auto field_data = (uint8_t*)obj + field->Offset;
        if (field->Get) {
            ((void(*)(void*, void*))field->Get)(field_data, &hash_map);
        } else {
            std::memcpy(&hash_map, field_data, sizeof(Array<void*>));
        }

        const auto get_item = (RTTIRefObject * (*)(const RTTIContainer*, const void*, size_t))container->Data->GetItem;
        const auto get_size = (uint32_t(*)(const RTTIContainer*, const void*))container->Data->GetSize;
        const auto try_put = (void* (*)(void*, const RTTIContainer*, void*, void*))container->Data->TryPut;
        const size_t size = get_size(container, &hash_map);

        if (size != value.size()) {
            spdlog::warn("HashMap size mismatch for field: {} ({} != {})", field->Name, size, value.size());
            return;
        }

        uint8_t try_put_buffer[0x18] = {};
        for (size_t i = 0; i < size; ++i) {
            const auto element = get_item(container, &hash_map, i);
            if (!element) {
                spdlog::warn("Failed to find hashmap element: [{}]", i);
                continue;
            }
            try_patch_object(element, container->ItemType->as_class(), value[i]);

            // Update hash
            try_put(try_put_buffer, container, &hash_map, element);
        }
    }
}

void try_patch_container_index(RTTIRefObject* obj, RTTIClassField* field, const nlohmann::json& value, std::span<const int> indices) {
    const auto container = field->Type->as_container();
    if (!container) {
        spdlog::warn("Failed to find container type for field: {}", field->Name);
        return;
    }

    if (container->Data->IsArray) {
        Array<void*> array;
        const auto field_data = (uint8_t*)obj + field->Offset;
        if (field->Get) {
            ((void(*)(void*, void*))field->Get)(field_data, &array);
        } else {
            std::memcpy(&array, field_data, sizeof(Array<void*>));
        }

        if (std::ranges::any_of(indices, [&array](int idx) { return idx >= array.size(); })) {
            spdlog::warn("Index out of bounds for field: {} (Max = {})", field->Name, array.size());
            return;
        }

        const auto get_value = [](const nlohmann::json& val, size_t idx) -> const nlohmann::json& {
            return val.is_array() ? val[idx] : val;
        };

        const auto get_item = (void* (*)(const RTTIContainer*, Array<void*>*, size_t))container->Data->GetItem;
        for (const auto [json_idx, idx] : indices | std::views::enumerate) {
            const auto element = get_item(container, &array, idx);

            switch (container->ItemType->Kind) {
            case RTTIKind::Primitive:
                try_patch_primitive_array_element(container, element, get_value(value, json_idx));
                break;
            case RTTIKind::Reference:
                try_patch_reference_array_element(container, element, get_value(value, json_idx));
                break;
            case RTTIKind::Container:
                spdlog::warn("Skipping container array element: {}", field->Name);
                break;
            case RTTIKind::Enum:
                try_patch_enum_array_element(container, element, get_value(value, json_idx));
                break;
            case RTTIKind::Class:
                try_patch_class_array_element(container, element, get_value(value, json_idx));
                break;
            case RTTIKind::EnumFlags:
                spdlog::warn("Skipping enum flags array element: {}", field->Name);
                break;
            case RTTIKind::Pod:
                spdlog::warn("Skipping pod array element: {}", field->Name);
                break;
            case RTTIKind::EnumBitSet:
                spdlog::warn("Skipping enum bitset array element: {}", field->Name);
                break;
            }
        }

        return;
    }

    if (std::string_view{ container->Data->TypeName }.starts_with("HashMap_")) {
        if (container->ItemType->Kind != RTTIKind::Class) {
            spdlog::warn("HashMap item type is not a class");
            return;
        }

        struct {
            void* Data;
            int Size;
            int Capacity;
        } hash_map;

        const auto field_data = (uint8_t*)obj + field->Offset;
        if (field->Get) {
            ((void(*)(void*, void*))field->Get)(field_data, &hash_map);
        } else {
            std::memcpy(&hash_map, field_data, sizeof(Array<void*>));
        }

        const auto get_item = (RTTIRefObject * (*)(const RTTIContainer*, const void*, size_t))container->Data->GetItem;
        const auto get_size = (uint32_t(*)(const RTTIContainer*, const void*))container->Data->GetSize;
        const auto try_put = (void* (*)(void*, const RTTIContainer*, void*, void*))container->Data->TryPut;
        const size_t size = get_size(container, &hash_map);

        if (std::ranges::any_of(indices, [&size](int idx) { return idx >= size; })) {
            spdlog::warn("Index out of bounds for field: {} (Max = {})", field->Name, size);
            return;
        }

        uint8_t try_put_buffer[0x18] = {};
        for (const auto [json_idx, idx] : indices | std::views::enumerate) {
            const auto element = get_item(container, &hash_map, idx);
            if (!element) {
                spdlog::warn("Failed to find hashmap element: [{}]", idx);
                continue;
            }

            try_patch_object(element, container->ItemType->as_class(), value[json_idx]);

            // Update hash
            try_put(try_put_buffer, container, &hash_map, element);
        }
    }
}

void try_patch_text_resource(RTTIRefObject* obj, std::string_view text) {
    const auto text_buffer = (char*)alloc_mem(text.size() + 1);
    std::memcpy(text_buffer, text.data(), text.size() + 1);

    *(char**)((uint8_t*)obj + 0x20) = text_buffer;
    *(uint16_t*)((uint8_t*)obj + 0x28) = (uint16_t)text.size();
}

void try_patch_text_resource(RTTIRefObject* obj, const nlohmann::json& value) {
    const auto language_enum = type_map["ELanguage"]->as_enum();
    const auto current_language = language_enum->find_value(get_current_language());
    if (!current_language) {
        spdlog::warn("Failed to find current language enum value");
        return;
    }

    if (!value.contains(current_language->Name)) {
        if (value.contains("Default")) {
            spdlog::debug("Patching text resource with default language: {} ({:p})", obj->ObjectUUID, (void*)obj);
            try_patch_text_resource(obj, std::string_view(value["Default"].get<std::string>()));
            return;
        }

        spdlog::warn("Failed to find text resource for current language");
        return;
    }

    spdlog::debug("Patching text resource: {} ({:p})", obj->ObjectUUID, (void*)obj);

    try_patch_text_resource(obj, std::string_view(value[current_language->Name].get<std::string>()));
}

void try_patch_object(RTTIRefObject* obj, const RTTIClass* type, const nlohmann::json& patch, bool is_real_ref_obj) {
    if (is_real_ref_obj) {
        spdlog::debug("Patching object: {} ({:p})", obj->ObjectUUID, (void*)obj);
    } else {
        spdlog::debug("Patching plain object: {:p}", (void*)obj);
    }

    if (type->instanceof(type_map["RTTIObject"])) {
        type = obj->get_rtti()->as_class(); // Sometimes 'type' is a base class
    }

    if (type->instanceof(type_map["LocalizedTextResource"])) {
        try_patch_text_resource(obj, patch);
        return;
    }

    std::smatch match;
    for (const auto& [property, value] : patch.items()) {
        if (property.starts_with("_")) {
            spdlog::debug("Skipping internal property: {}", property);
            continue;
        }

        if (std::regex_match(property, match, indexer_regex)) {
            const auto field = type->find_ordered_field(match[1].str().c_str());
            if (!field) {
                spdlog::warn("Failed to find field: {}", property);
                continue;
            }

            if (field->Type->Kind != RTTIKind::Container) {
                spdlog::warn("Field is not a container: {}", property);
                continue;
            }

            try_patch_container_index(obj, field, value, parse_index_list(match[2].str()));
            continue;
        }

        RTTIClassField* field = type->find_ordered_field(property.c_str());
        if (!field) {
            field = type->find_field(property.c_str());
            if (!field) {
                spdlog::warn("Failed to find field: {}", property);
                continue;
            }
        }

        switch (field->Type->Kind) {
        case RTTIKind::Primitive:
            try_patch_primitive(obj, field, value);
            break;
        case RTTIKind::Reference:
            try_patch_reference(obj, field, value);
            break;
        case RTTIKind::Container:
            try_patch_container(obj, field, value);
            break;
        case RTTIKind::Enum:
            try_patch_enum(obj, field, value);
            break;
        case RTTIKind::Class:
            try_patch_class(obj, field, value, type->instanceof(rtti_ref_object_rtti));
            break;
        case RTTIKind::EnumFlags:
            try_patch_enum(obj, field, value);
            break;
        case RTTIKind::Pod:
            spdlog::warn("Skipping pod field: {}", property);
            break;
        case RTTIKind::EnumBitSet:
            try_patch_enum(obj, field, value);
            break;
        }
    }
}

void try_patch_object(RTTIObject* obj) {
    const auto rtti = obj->get_rtti();
    if (rtti->Kind != RTTIKind::Class) {
        return;
    }

    {
        std::unique_lock lock(deserialized_objects_mutex);
        deserialized_objects.insert(obj);
    }

    const auto cls = rtti->as_class();
    if (!cls->instanceof(rtti_ref_object_rtti)) {
        return;
    }

    if (cls->instanceof(type_map["CollectableRobot"])) {
        collectable_robots.insert(obj);
    }

    if (cls->instanceof(type_map["LootComponentResource"])) {
        loot_component_resources.insert(obj);
    }

    //for (const auto& fld : cls->get_fields()) {
    //    if (fld.Get || fld.Set) {
    //        spdlog::info("Getter/Setter - {} {}::{}", fld.Type->name(), cls->Name, fld.Name);
    //    }
    //}

    const auto ref_obj = (RTTIRefObject*)obj;

    // Check if the object has a delayed patch
    if (delayed_patches.contains(ref_obj)) {
        spdlog::debug("Resolving delayed patch for object: {} ({:p})", ref_obj->ObjectUUID, (void*)ref_obj);
        try_patch_object(ref_obj, cls, delayed_patches[ref_obj]);
        delayed_patches.erase(ref_obj);
    }

    const auto it = object_patches.find(ref_obj->ObjectUUID);
    if (it == object_patches.end()) {
        return;
    }

    try_patch_object(ref_obj, cls, it->second);
}

void init_logger() {
    // Initialize a file logger
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::debug);

    const auto file_logger = spdlog::basic_logger_mt("DecimaLoader", "loader.log", true);
    spdlog::set_default_logger(file_logger);
}

void init_console() {
    // Open a console window
    AllocConsole();
    FILE* stream;
    (void)freopen_s(&stream, "CONOUT$", "w", stdout);

    // Add console sink to the logger
    const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    const auto logger = spdlog::default_logger();
    logger->sinks().push_back(console_sink);
}

bool run() {
    init_logger();

#if 1
    init_console();
    Sleep(5000);
#endif

    spdlog::info("Initialization started");

    DecimaTypeDb::initialize();

    typedef int(*FWinMain)(HINSTANCE, HINSTANCE, LPSTR, int);
    typedef bool(*FRegisterType)(void*, RTTI*);
    typedef void(*FRegisterAllTypes)();
    typedef int(*FReadObject)(void*, RTTIObject**);
    typedef void(*FReleaseRefObj)(RTTIRefObject*);
    typedef decltype(enum_value_from_string) FEnumValueFromString;

    auto winmain = (FWinMain)PatternScanner::find_first(Pattern::from_string(winmain_pattern));
    auto register_type = (FRegisterType)PatternScanner::find_first(Pattern::from_string(register_type_pattern));
    auto register_all_types = (FRegisterAllTypes)PatternScanner::find_first(Pattern::from_string(register_all_types_pattern));
    auto read_object = (FReadObject)PatternScanner::find_first(Pattern::from_string(read_object_pattern));
    auto alloc_mem_ = PatternScanner::find_first(Pattern::from_string(alloc_mem_pattern));
    auto win32_system_ctor = PatternScanner::find_first(Pattern::from_string(win32_system_ctor_pattern));
    auto ref_object_release = (FReleaseRefObj)PatternScanner::find_first(Pattern::from_string(ref_object_release_pattern));
    auto enum_value_from_string_ = (FEnumValueFromString)PatternScanner::find_first(Pattern::from_string(enum_value_from_string_pattern));
    auto float_to_half_ = (uint16_t(*)(float))PatternScanner::find_first(Pattern::from_string(float_to_half_pattern));

    if (!winmain || !register_type || !register_all_types || !read_object || !alloc_mem_ || !win32_system_ctor) {
        spdlog::error("Failed to find pattern");
        return false;
    }

    spdlog::info("Found WinMain at: {:p}", (void*)winmain);
    spdlog::info("Found RegisterType at: {:p}", (void*)register_type);
    spdlog::info("Found RegisterAllTypes at: {:p}", (void*)register_all_types);
    spdlog::info("Found ReadObject at: {:p}", (void*)read_object);
    spdlog::info("Found AllocMem at: {:p}", (void*)alloc_mem_);
    spdlog::info("Found Win32System::ctor at: {:p}", (void*)win32_system_ctor);
    spdlog::info("Found RTTIRefObject::release at: {:p}", (void*)ref_object_release);
    spdlog::info("Found EnumValueFromString at: {:p}", (void*)enum_value_from_string_);
    spdlog::info("Found FloatToHalf at: {:p}", (void*)float_to_half_);

    ::alloc_mem = (void* (*)(size_t))(alloc_mem_ - 66);
    ::enum_value_from_string = enum_value_from_string_;
    ::float_to_half = float_to_half_;

    // mov [Win32System::Instance], rax (48 89 05 XX XX XX XX)
    win32_system_instance = (void*)(win32_system_ctor + 7 + *(int32_t*)(win32_system_ctor + 3));
    spdlog::info("Found Win32System::Instance at: {:p}", win32_system_instance);
    
    MH_Initialize();

    HookLambda(winmain, [](HINSTANCE a, HINSTANCE b, LPSTR c, int d) {
        for (const auto& options : plugin_options) {
            if (options.OnWinMain) {
                options.OnWinMain();
            }
        }

        return winmain_original(a, b, c, d);
    });
    HookLambda(register_type, [](void* a, RTTI* b) {
        for (const auto& options : plugin_options) {
            if (options.OnPreRegisterType) {
                options.OnPreRegisterType(b);
            }
        }

        const auto result = register_type_original(a, b);

        if (!rtti_ref_object_rtti && strcmp(b->name(), "RTTIRefObject") == 0) {
            rtti_ref_object_rtti = b;
        }

        type_map[b->name()] = b;

        for (const auto& options : plugin_options) {
            if (options.OnPostRegisterType) {
                options.OnPostRegisterType(b);
            }
        }

        return result;
    });
    HookLambda(register_all_types, [] {
        for (const auto& options : plugin_options) {
            if (options.OnPreRegisterAllTypes) {
                options.OnPreRegisterAllTypes();
            }
        }

        register_all_types_original();
        auto collectable_robot_dtor = type_map["CollectableRobot"]->as_class()->Destructor;
        HookLambda(collectable_robot_dtor, [](RTTI* type, void* obj) {
            collectable_robots.erase(obj);
            return collectable_robot_dtor_original(type, obj);
        });

        auto loot_component_resource_dtor = type_map["LootComponentResource"]->as_class()->Destructor;
        HookLambda(loot_component_resource_dtor, [](RTTI* type, void* obj) {
            loot_component_resources.erase(obj);
            return loot_component_resource_dtor_original(type, obj);
        });

        integral_types.emplace(type_map["int8"]);
        integral_types.emplace(type_map["int16"]);
        integral_types.emplace(type_map["int32"]);
        integral_types.emplace(type_map["int64"]);
        integral_types.emplace(type_map["uint8"]);
        integral_types.emplace(type_map["uint16"]);
        integral_types.emplace(type_map["uint32"]);
        integral_types.emplace(type_map["uint64"]);
        integral_types.emplace(type_map["int"]);
        integral_types.emplace(type_map["uint"]);
        integral_types.emplace(type_map["uint128"]);
        integral_types.emplace(type_map["intptr"]);
        integral_types.emplace(type_map["uintptr"]);
        integral_types.emplace(type_map["wchar"]);
        integral_types.emplace(type_map["tchar"]);
        integral_types.emplace(type_map["ucs4"]);

        floating_point_types.emplace(type_map["float"]);
        floating_point_types.emplace(type_map["double"]);
        floating_point_types.emplace(type_map["HalfFloat"]);

        string_types.emplace(type_map["String"]);
        string_types.emplace(type_map["WString"]);

        bool_types.emplace(type_map["bool"]);

        for (const auto& options : plugin_options) {
            if (options.OnPostRegisterAllTypes) {
                options.OnPostRegisterAllTypes();
            }
        }
    });
    HookLambda(read_object, [](void* reader, RTTIObject** pobj) {
        const auto result = read_object_original(reader, pobj);
        if (result == 0 && pobj && *pobj) {
            try_patch_object(*pobj);
        }

        return result;
    });
    HookLambda(ref_object_release, [](RTTIRefObject* obj) {
        const auto obj_ptr = (void*)obj;
        if (obj->RefCount == 1) {
            std::unique_lock lock(deserialized_objects_mutex);
            deserialized_objects.erase(obj);
        }

        return ref_object_release_original(obj);
    });

    MH_ApplyQueued();

    std::thread([] {
        while (true) {
            if (GetAsyncKeyState(VK_F7) & 1) {
                spdlog::info("Dumping CollectableRobot types");
                if (!std::filesystem::exists("dumps")) {
                    std::filesystem::create_directory("dumps");
                }

                //const auto type = type_map["CollectableRobot"];
                //for (const auto [i, obj] : std::views::enumerate(collectable_robots)) {
                //    std::ofstream file(std::format("dumps/CollectableRobot_{}.json", i));
                //    ObjectDumper dumper({
                //        .log_sink = spdlog::default_logger()->sinks().front(),
                //        .output_array_indices = true,
                //        .output_class_names = true
                //    });
                //    dumper.dump_to((const RTTIObject*)obj, file, 4);
                //}
                
                for (const auto [i, obj] : std::views::enumerate(loot_component_resources)) {
                    std::ofstream file(std::format("dumps/LootComponentResource_{}.json", i));
                    ObjectDumper dumper({
                        .log_sink = spdlog::default_logger()->sinks().back(),
                        .output_array_indices = true,
                        .output_class_names = true
                    });
                    dumper.dump_to((const RTTIObject*)obj, file, 4);
                }

                spdlog::info("Dumping CollectableRobot types complete");
            }
        }
    }).detach();

    // Load plugins
    namespace fs = std::filesystem;

    for (const auto& entry : fs::directory_iterator("plugins")) {
        if (entry.is_regular_file() && entry.path().extension() == ".dll") {
            const auto module = LoadLibraryW(entry.path().c_str());
            if (!module) {
                spdlog::error("Failed to load plugin: {}", entry.path().filename().string());
                continue;
            }

            const auto init = (void(*)(PluginInitializeOptions*))GetProcAddress(module, "plugin_initialize");
            if (!init) {
                spdlog::error("Failed to find plugin_initialize in: {}", entry.path().filename().string());
                continue;
            }

            PluginInitializeOptions options = {};
            init(&options);

            plugin_options.push_back(options);

            spdlog::info("Loaded plugin: {}", entry.path().filename().string());
        }
    }

    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Load object overrides
    for (const auto& entry : fs::directory_iterator("patches")) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            const auto json = nlohmann::json::parse(std::ifstream(entry.path()));

            spdlog::info("Loading patch file: {}", entry.path().filename().string());

            for (const auto& [uuid, obj] : json.items()) {
                GUID guid;
                // Parse the GUID using COM
                if (FAILED(IIDFromString(std::wstring(uuid.begin(), uuid.end()).c_str(), &guid))) {
                    spdlog::error("Failed to parse GUID: {}", uuid);
                    continue;
                }

                object_patches[guid] = obj;
            }
        }
    }

    spdlog::info("Initialization complete");

    return true;
}

}


BOOL APIENTRY DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        return ::run();
    }

    return true;
}

#pragma region winmm export forwarding

#pragma comment(linker, "/export:CloseDriver=\"C:\\Windows\\System32\\winmm.CloseDriver\"")
#pragma comment(linker, "/export:DefDriverProc=\"C:\\Windows\\System32\\winmm.DefDriverProc\"")
#pragma comment(linker, "/export:DriverCallback=\"C:\\Windows\\System32\\winmm.DriverCallback\"")
#pragma comment(linker, "/export:DrvGetModuleHandle=\"C:\\Windows\\System32\\winmm.DrvGetModuleHandle\"")
#pragma comment(linker, "/export:GetDriverModuleHandle=\"C:\\Windows\\System32\\winmm.GetDriverModuleHandle\"")
#pragma comment(linker, "/export:OpenDriver=\"C:\\Windows\\System32\\winmm.OpenDriver\"")
#pragma comment(linker, "/export:PlaySound=\"C:\\Windows\\System32\\winmm.PlaySound\"")
#pragma comment(linker, "/export:PlaySoundA=\"C:\\Windows\\System32\\winmm.PlaySoundA\"")
#pragma comment(linker, "/export:PlaySoundW=\"C:\\Windows\\System32\\winmm.PlaySoundW\"")
#pragma comment(linker, "/export:SendDriverMessage=\"C:\\Windows\\System32\\winmm.SendDriverMessage\"")
#pragma comment(linker, "/export:WOWAppExit=\"C:\\Windows\\System32\\winmm.WOWAppExit\"")
#pragma comment(linker, "/export:auxGetDevCapsA=\"C:\\Windows\\System32\\winmm.auxGetDevCapsA\"")
#pragma comment(linker, "/export:auxGetDevCapsW=\"C:\\Windows\\System32\\winmm.auxGetDevCapsW\"")
#pragma comment(linker, "/export:auxGetNumDevs=\"C:\\Windows\\System32\\winmm.auxGetNumDevs\"")
#pragma comment(linker, "/export:auxGetVolume=\"C:\\Windows\\System32\\winmm.auxGetVolume\"")
#pragma comment(linker, "/export:auxOutMessage=\"C:\\Windows\\System32\\winmm.auxOutMessage\"")
#pragma comment(linker, "/export:auxSetVolume=\"C:\\Windows\\System32\\winmm.auxSetVolume\"")
#pragma comment(linker, "/export:joyConfigChanged=\"C:\\Windows\\System32\\winmm.joyConfigChanged\"")
#pragma comment(linker, "/export:joyGetDevCapsA=\"C:\\Windows\\System32\\winmm.joyGetDevCapsA\"")
#pragma comment(linker, "/export:joyGetDevCapsW=\"C:\\Windows\\System32\\winmm.joyGetDevCapsW\"")
#pragma comment(linker, "/export:joyGetNumDevs=\"C:\\Windows\\System32\\winmm.joyGetNumDevs\"")
#pragma comment(linker, "/export:joyGetPos=\"C:\\Windows\\System32\\winmm.joyGetPos\"")
#pragma comment(linker, "/export:joyGetPosEx=\"C:\\Windows\\System32\\winmm.joyGetPosEx\"")
#pragma comment(linker, "/export:joyGetThreshold=\"C:\\Windows\\System32\\winmm.joyGetThreshold\"")
#pragma comment(linker, "/export:joyReleaseCapture=\"C:\\Windows\\System32\\winmm.joyReleaseCapture\"")
#pragma comment(linker, "/export:joySetCapture=\"C:\\Windows\\System32\\winmm.joySetCapture\"")
#pragma comment(linker, "/export:joySetThreshold=\"C:\\Windows\\System32\\winmm.joySetThreshold\"")
#pragma comment(linker, "/export:mciDriverNotify=\"C:\\Windows\\System32\\winmm.mciDriverNotify\"")
#pragma comment(linker, "/export:mciDriverYield=\"C:\\Windows\\System32\\winmm.mciDriverYield\"")
#pragma comment(linker, "/export:mciExecute=\"C:\\Windows\\System32\\winmm.mciExecute\"")
#pragma comment(linker, "/export:mciFreeCommandResource=\"C:\\Windows\\System32\\winmm.mciFreeCommandResource\"")
#pragma comment(linker, "/export:mciGetCreatorTask=\"C:\\Windows\\System32\\winmm.mciGetCreatorTask\"")
#pragma comment(linker, "/export:mciGetDeviceIDA=\"C:\\Windows\\System32\\winmm.mciGetDeviceIDA\"")
#pragma comment(linker, "/export:mciGetDeviceIDFromElementIDA=\"C:\\Windows\\System32\\winmm.mciGetDeviceIDFromElementIDA\"")
#pragma comment(linker, "/export:mciGetDeviceIDFromElementIDW=\"C:\\Windows\\System32\\winmm.mciGetDeviceIDFromElementIDW\"")
#pragma comment(linker, "/export:mciGetDeviceIDW=\"C:\\Windows\\System32\\winmm.mciGetDeviceIDW\"")
#pragma comment(linker, "/export:mciGetDriverData=\"C:\\Windows\\System32\\winmm.mciGetDriverData\"")
#pragma comment(linker, "/export:mciGetErrorStringA=\"C:\\Windows\\System32\\winmm.mciGetErrorStringA\"")
#pragma comment(linker, "/export:mciGetErrorStringW=\"C:\\Windows\\System32\\winmm.mciGetErrorStringW\"")
#pragma comment(linker, "/export:mciGetYieldProc=\"C:\\Windows\\System32\\winmm.mciGetYieldProc\"")
#pragma comment(linker, "/export:mciLoadCommandResource=\"C:\\Windows\\System32\\winmm.mciLoadCommandResource\"")
#pragma comment(linker, "/export:mciSendCommandA=\"C:\\Windows\\System32\\winmm.mciSendCommandA\"")
#pragma comment(linker, "/export:mciSendCommandW=\"C:\\Windows\\System32\\winmm.mciSendCommandW\"")
#pragma comment(linker, "/export:mciSendStringA=\"C:\\Windows\\System32\\winmm.mciSendStringA\"")
#pragma comment(linker, "/export:mciSendStringW=\"C:\\Windows\\System32\\winmm.mciSendStringW\"")
#pragma comment(linker, "/export:mciSetDriverData=\"C:\\Windows\\System32\\winmm.mciSetDriverData\"")
#pragma comment(linker, "/export:mciSetYieldProc=\"C:\\Windows\\System32\\winmm.mciSetYieldProc\"")
#pragma comment(linker, "/export:midiConnect=\"C:\\Windows\\System32\\winmm.midiConnect\"")
#pragma comment(linker, "/export:midiDisconnect=\"C:\\Windows\\System32\\winmm.midiDisconnect\"")
#pragma comment(linker, "/export:midiInAddBuffer=\"C:\\Windows\\System32\\winmm.midiInAddBuffer\"")
#pragma comment(linker, "/export:midiInClose=\"C:\\Windows\\System32\\winmm.midiInClose\"")
#pragma comment(linker, "/export:midiInGetDevCapsA=\"C:\\Windows\\System32\\winmm.midiInGetDevCapsA\"")
#pragma comment(linker, "/export:midiInGetDevCapsW=\"C:\\Windows\\System32\\winmm.midiInGetDevCapsW\"")
#pragma comment(linker, "/export:midiInGetErrorTextA=\"C:\\Windows\\System32\\winmm.midiInGetErrorTextA\"")
#pragma comment(linker, "/export:midiInGetErrorTextW=\"C:\\Windows\\System32\\winmm.midiInGetErrorTextW\"")
#pragma comment(linker, "/export:midiInGetID=\"C:\\Windows\\System32\\winmm.midiInGetID\"")
#pragma comment(linker, "/export:midiInGetNumDevs=\"C:\\Windows\\System32\\winmm.midiInGetNumDevs\"")
#pragma comment(linker, "/export:midiInMessage=\"C:\\Windows\\System32\\winmm.midiInMessage\"")
#pragma comment(linker, "/export:midiInOpen=\"C:\\Windows\\System32\\winmm.midiInOpen\"")
#pragma comment(linker, "/export:midiInPrepareHeader=\"C:\\Windows\\System32\\winmm.midiInPrepareHeader\"")
#pragma comment(linker, "/export:midiInReset=\"C:\\Windows\\System32\\winmm.midiInReset\"")
#pragma comment(linker, "/export:midiInStart=\"C:\\Windows\\System32\\winmm.midiInStart\"")
#pragma comment(linker, "/export:midiInStop=\"C:\\Windows\\System32\\winmm.midiInStop\"")
#pragma comment(linker, "/export:midiInUnprepareHeader=\"C:\\Windows\\System32\\winmm.midiInUnprepareHeader\"")
#pragma comment(linker, "/export:midiOutCacheDrumPatches=\"C:\\Windows\\System32\\winmm.midiOutCacheDrumPatches\"")
#pragma comment(linker, "/export:midiOutCachePatches=\"C:\\Windows\\System32\\winmm.midiOutCachePatches\"")
#pragma comment(linker, "/export:midiOutClose=\"C:\\Windows\\System32\\winmm.midiOutClose\"")
#pragma comment(linker, "/export:midiOutGetDevCapsA=\"C:\\Windows\\System32\\winmm.midiOutGetDevCapsA\"")
#pragma comment(linker, "/export:midiOutGetDevCapsW=\"C:\\Windows\\System32\\winmm.midiOutGetDevCapsW\"")
#pragma comment(linker, "/export:midiOutGetErrorTextA=\"C:\\Windows\\System32\\winmm.midiOutGetErrorTextA\"")
#pragma comment(linker, "/export:midiOutGetErrorTextW=\"C:\\Windows\\System32\\winmm.midiOutGetErrorTextW\"")
#pragma comment(linker, "/export:midiOutGetID=\"C:\\Windows\\System32\\winmm.midiOutGetID\"")
#pragma comment(linker, "/export:midiOutGetNumDevs=\"C:\\Windows\\System32\\winmm.midiOutGetNumDevs\"")
#pragma comment(linker, "/export:midiOutGetVolume=\"C:\\Windows\\System32\\winmm.midiOutGetVolume\"")
#pragma comment(linker, "/export:midiOutLongMsg=\"C:\\Windows\\System32\\winmm.midiOutLongMsg\"")
#pragma comment(linker, "/export:midiOutMessage=\"C:\\Windows\\System32\\winmm.midiOutMessage\"")
#pragma comment(linker, "/export:midiOutOpen=\"C:\\Windows\\System32\\winmm.midiOutOpen\"")
#pragma comment(linker, "/export:midiOutPrepareHeader=\"C:\\Windows\\System32\\winmm.midiOutPrepareHeader\"")
#pragma comment(linker, "/export:midiOutReset=\"C:\\Windows\\System32\\winmm.midiOutReset\"")
#pragma comment(linker, "/export:midiOutSetVolume=\"C:\\Windows\\System32\\winmm.midiOutSetVolume\"")
#pragma comment(linker, "/export:midiOutShortMsg=\"C:\\Windows\\System32\\winmm.midiOutShortMsg\"")
#pragma comment(linker, "/export:midiOutUnprepareHeader=\"C:\\Windows\\System32\\winmm.midiOutUnprepareHeader\"")
#pragma comment(linker, "/export:midiStreamClose=\"C:\\Windows\\System32\\winmm.midiStreamClose\"")
#pragma comment(linker, "/export:midiStreamOpen=\"C:\\Windows\\System32\\winmm.midiStreamOpen\"")
#pragma comment(linker, "/export:midiStreamOut=\"C:\\Windows\\System32\\winmm.midiStreamOut\"")
#pragma comment(linker, "/export:midiStreamPause=\"C:\\Windows\\System32\\winmm.midiStreamPause\"")
#pragma comment(linker, "/export:midiStreamPosition=\"C:\\Windows\\System32\\winmm.midiStreamPosition\"")
#pragma comment(linker, "/export:midiStreamProperty=\"C:\\Windows\\System32\\winmm.midiStreamProperty\"")
#pragma comment(linker, "/export:midiStreamRestart=\"C:\\Windows\\System32\\winmm.midiStreamRestart\"")
#pragma comment(linker, "/export:midiStreamStop=\"C:\\Windows\\System32\\winmm.midiStreamStop\"")
#pragma comment(linker, "/export:mixerClose=\"C:\\Windows\\System32\\winmm.mixerClose\"")
#pragma comment(linker, "/export:mixerGetControlDetailsA=\"C:\\Windows\\System32\\winmm.mixerGetControlDetailsA\"")
#pragma comment(linker, "/export:mixerGetControlDetailsW=\"C:\\Windows\\System32\\winmm.mixerGetControlDetailsW\"")
#pragma comment(linker, "/export:mixerGetDevCapsA=\"C:\\Windows\\System32\\winmm.mixerGetDevCapsA\"")
#pragma comment(linker, "/export:mixerGetDevCapsW=\"C:\\Windows\\System32\\winmm.mixerGetDevCapsW\"")
#pragma comment(linker, "/export:mixerGetID=\"C:\\Windows\\System32\\winmm.mixerGetID\"")
#pragma comment(linker, "/export:mixerGetLineControlsA=\"C:\\Windows\\System32\\winmm.mixerGetLineControlsA\"")
#pragma comment(linker, "/export:mixerGetLineControlsW=\"C:\\Windows\\System32\\winmm.mixerGetLineControlsW\"")
#pragma comment(linker, "/export:mixerGetLineInfoA=\"C:\\Windows\\System32\\winmm.mixerGetLineInfoA\"")
#pragma comment(linker, "/export:mixerGetLineInfoW=\"C:\\Windows\\System32\\winmm.mixerGetLineInfoW\"")
#pragma comment(linker, "/export:mixerGetNumDevs=\"C:\\Windows\\System32\\winmm.mixerGetNumDevs\"")
#pragma comment(linker, "/export:mixerMessage=\"C:\\Windows\\System32\\winmm.mixerMessage\"")
#pragma comment(linker, "/export:mixerOpen=\"C:\\Windows\\System32\\winmm.mixerOpen\"")
#pragma comment(linker, "/export:mixerSetControlDetails=\"C:\\Windows\\System32\\winmm.mixerSetControlDetails\"")
#pragma comment(linker, "/export:mmDrvInstall=\"C:\\Windows\\System32\\winmm.mmDrvInstall\"")
#pragma comment(linker, "/export:mmGetCurrentTask=\"C:\\Windows\\System32\\winmm.mmGetCurrentTask\"")
#pragma comment(linker, "/export:mmTaskBlock=\"C:\\Windows\\System32\\winmm.mmTaskBlock\"")
#pragma comment(linker, "/export:mmTaskCreate=\"C:\\Windows\\System32\\winmm.mmTaskCreate\"")
#pragma comment(linker, "/export:mmTaskSignal=\"C:\\Windows\\System32\\winmm.mmTaskSignal\"")
#pragma comment(linker, "/export:mmTaskYield=\"C:\\Windows\\System32\\winmm.mmTaskYield\"")
#pragma comment(linker, "/export:mmioAdvance=\"C:\\Windows\\System32\\winmm.mmioAdvance\"")
#pragma comment(linker, "/export:mmioAscend=\"C:\\Windows\\System32\\winmm.mmioAscend\"")
#pragma comment(linker, "/export:mmioClose=\"C:\\Windows\\System32\\winmm.mmioClose\"")
#pragma comment(linker, "/export:mmioCreateChunk=\"C:\\Windows\\System32\\winmm.mmioCreateChunk\"")
#pragma comment(linker, "/export:mmioDescend=\"C:\\Windows\\System32\\winmm.mmioDescend\"")
#pragma comment(linker, "/export:mmioFlush=\"C:\\Windows\\System32\\winmm.mmioFlush\"")
#pragma comment(linker, "/export:mmioGetInfo=\"C:\\Windows\\System32\\winmm.mmioGetInfo\"")
#pragma comment(linker, "/export:mmioInstallIOProcA=\"C:\\Windows\\System32\\winmm.mmioInstallIOProcA\"")
#pragma comment(linker, "/export:mmioInstallIOProcW=\"C:\\Windows\\System32\\winmm.mmioInstallIOProcW\"")
#pragma comment(linker, "/export:mmioOpenA=\"C:\\Windows\\System32\\winmm.mmioOpenA\"")
#pragma comment(linker, "/export:mmioOpenW=\"C:\\Windows\\System32\\winmm.mmioOpenW\"")
#pragma comment(linker, "/export:mmioRead=\"C:\\Windows\\System32\\winmm.mmioRead\"")
#pragma comment(linker, "/export:mmioRenameA=\"C:\\Windows\\System32\\winmm.mmioRenameA\"")
#pragma comment(linker, "/export:mmioRenameW=\"C:\\Windows\\System32\\winmm.mmioRenameW\"")
#pragma comment(linker, "/export:mmioSeek=\"C:\\Windows\\System32\\winmm.mmioSeek\"")
#pragma comment(linker, "/export:mmioSendMessage=\"C:\\Windows\\System32\\winmm.mmioSendMessage\"")
#pragma comment(linker, "/export:mmioSetBuffer=\"C:\\Windows\\System32\\winmm.mmioSetBuffer\"")
#pragma comment(linker, "/export:mmioSetInfo=\"C:\\Windows\\System32\\winmm.mmioSetInfo\"")
#pragma comment(linker, "/export:mmioStringToFOURCCA=\"C:\\Windows\\System32\\winmm.mmioStringToFOURCCA\"")
#pragma comment(linker, "/export:mmioStringToFOURCCW=\"C:\\Windows\\System32\\winmm.mmioStringToFOURCCW\"")
#pragma comment(linker, "/export:mmioWrite=\"C:\\Windows\\System32\\winmm.mmioWrite\"")
#pragma comment(linker, "/export:mmsystemGetVersion=\"C:\\Windows\\System32\\winmm.mmsystemGetVersion\"")
#pragma comment(linker, "/export:sndPlaySoundA=\"C:\\Windows\\System32\\winmm.sndPlaySoundA\"")
#pragma comment(linker, "/export:sndPlaySoundW=\"C:\\Windows\\System32\\winmm.sndPlaySoundW\"")
#pragma comment(linker, "/export:timeBeginPeriod=\"C:\\Windows\\System32\\winmm.timeBeginPeriod\"")
#pragma comment(linker, "/export:timeEndPeriod=\"C:\\Windows\\System32\\winmm.timeEndPeriod\"")
#pragma comment(linker, "/export:timeGetDevCaps=\"C:\\Windows\\System32\\winmm.timeGetDevCaps\"")
#pragma comment(linker, "/export:timeGetSystemTime=\"C:\\Windows\\System32\\winmm.timeGetSystemTime\"")
#pragma comment(linker, "/export:timeGetTime=\"C:\\Windows\\System32\\winmm.timeGetTime\"")
#pragma comment(linker, "/export:timeKillEvent=\"C:\\Windows\\System32\\winmm.timeKillEvent\"")
#pragma comment(linker, "/export:timeSetEvent=\"C:\\Windows\\System32\\winmm.timeSetEvent\"")
#pragma comment(linker, "/export:waveInAddBuffer=\"C:\\Windows\\System32\\winmm.waveInAddBuffer\"")
#pragma comment(linker, "/export:waveInClose=\"C:\\Windows\\System32\\winmm.waveInClose\"")
#pragma comment(linker, "/export:waveInGetDevCapsA=\"C:\\Windows\\System32\\winmm.waveInGetDevCapsA\"")
#pragma comment(linker, "/export:waveInGetDevCapsW=\"C:\\Windows\\System32\\winmm.waveInGetDevCapsW\"")
#pragma comment(linker, "/export:waveInGetErrorTextA=\"C:\\Windows\\System32\\winmm.waveInGetErrorTextA\"")
#pragma comment(linker, "/export:waveInGetErrorTextW=\"C:\\Windows\\System32\\winmm.waveInGetErrorTextW\"")
#pragma comment(linker, "/export:waveInGetID=\"C:\\Windows\\System32\\winmm.waveInGetID\"")
#pragma comment(linker, "/export:waveInGetNumDevs=\"C:\\Windows\\System32\\winmm.waveInGetNumDevs\"")
#pragma comment(linker, "/export:waveInGetPosition=\"C:\\Windows\\System32\\winmm.waveInGetPosition\"")
#pragma comment(linker, "/export:waveInMessage=\"C:\\Windows\\System32\\winmm.waveInMessage\"")
#pragma comment(linker, "/export:waveInOpen=\"C:\\Windows\\System32\\winmm.waveInOpen\"")
#pragma comment(linker, "/export:waveInPrepareHeader=\"C:\\Windows\\System32\\winmm.waveInPrepareHeader\"")
#pragma comment(linker, "/export:waveInReset=\"C:\\Windows\\System32\\winmm.waveInReset\"")
#pragma comment(linker, "/export:waveInStart=\"C:\\Windows\\System32\\winmm.waveInStart\"")
#pragma comment(linker, "/export:waveInStop=\"C:\\Windows\\System32\\winmm.waveInStop\"")
#pragma comment(linker, "/export:waveInUnprepareHeader=\"C:\\Windows\\System32\\winmm.waveInUnprepareHeader\"")
#pragma comment(linker, "/export:waveOutBreakLoop=\"C:\\Windows\\System32\\winmm.waveOutBreakLoop\"")
#pragma comment(linker, "/export:waveOutClose=\"C:\\Windows\\System32\\winmm.waveOutClose\"")
#pragma comment(linker, "/export:waveOutGetDevCapsA=\"C:\\Windows\\System32\\winmm.waveOutGetDevCapsA\"")
#pragma comment(linker, "/export:waveOutGetDevCapsW=\"C:\\Windows\\System32\\winmm.waveOutGetDevCapsW\"")
#pragma comment(linker, "/export:waveOutGetErrorTextA=\"C:\\Windows\\System32\\winmm.waveOutGetErrorTextA\"")
#pragma comment(linker, "/export:waveOutGetErrorTextW=\"C:\\Windows\\System32\\winmm.waveOutGetErrorTextW\"")
#pragma comment(linker, "/export:waveOutGetID=\"C:\\Windows\\System32\\winmm.waveOutGetID\"")
#pragma comment(linker, "/export:waveOutGetNumDevs=\"C:\\Windows\\System32\\winmm.waveOutGetNumDevs\"")
#pragma comment(linker, "/export:waveOutGetPitch=\"C:\\Windows\\System32\\winmm.waveOutGetPitch\"")
#pragma comment(linker, "/export:waveOutGetPlaybackRate=\"C:\\Windows\\System32\\winmm.waveOutGetPlaybackRate\"")
#pragma comment(linker, "/export:waveOutGetPosition=\"C:\\Windows\\System32\\winmm.waveOutGetPosition\"")
#pragma comment(linker, "/export:waveOutGetVolume=\"C:\\Windows\\System32\\winmm.waveOutGetVolume\"")
#pragma comment(linker, "/export:waveOutMessage=\"C:\\Windows\\System32\\winmm.waveOutMessage\"")
#pragma comment(linker, "/export:waveOutOpen=\"C:\\Windows\\System32\\winmm.waveOutOpen\"")
#pragma comment(linker, "/export:waveOutPause=\"C:\\Windows\\System32\\winmm.waveOutPause\"")
#pragma comment(linker, "/export:waveOutPrepareHeader=\"C:\\Windows\\System32\\winmm.waveOutPrepareHeader\"")
#pragma comment(linker, "/export:waveOutReset=\"C:\\Windows\\System32\\winmm.waveOutReset\"")
#pragma comment(linker, "/export:waveOutRestart=\"C:\\Windows\\System32\\winmm.waveOutRestart\"")
#pragma comment(linker, "/export:waveOutSetPitch=\"C:\\Windows\\System32\\winmm.waveOutSetPitch\"")
#pragma comment(linker, "/export:waveOutSetPlaybackRate=\"C:\\Windows\\System32\\winmm.waveOutSetPlaybackRate\"")
#pragma comment(linker, "/export:waveOutSetVolume=\"C:\\Windows\\System32\\winmm.waveOutSetVolume\"")
#pragma comment(linker, "/export:waveOutUnprepareHeader=\"C:\\Windows\\System32\\winmm.waveOutUnprepareHeader\"")
#pragma comment(linker, "/export:waveOutWrite=\"C:\\Windows\\System32\\winmm.waveOutWrite\"")

#pragma endregion
