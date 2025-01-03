#include "pch.h"
#include "ObjectDumper.h"

#include "Decima/Types.h"
#include "RTTI/RTTI.h"
#include "RTTI/RTTIClass.h"
#include "RTTI/RTTIEnum.h"
#include "RTTI/RTTIContainer.h"
#include "RTTI/RTTIReference.h"
#include "RTTI/RTTIObject.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#define log_debug m_logger->debug
#define log_info m_logger->info
#define log_warn m_logger->warn
#define log_error m_logger->error

using ojson = nlohmann::ordered_json;

extern std::unordered_map<std::string, RTTI*> type_map;

ObjectDumper::ObjectDumper(const ObjectDumperConfig& config)
    : m_logger(std::make_shared<spdlog::logger>("ObjectDumper"))
    , m_output_array_indices(config.output_array_indices)
    , m_output_class_names(config.output_class_names)
    , m_class_hook(config.class_hook) {
    if (config.log_sink) {
        m_logger->sinks().push_back(config.log_sink);
    }
}

ojson ObjectDumper::dump(const RTTIObject* object) {
    const auto type = object->get_rtti();
    if (!type) {
        log_error("Failed to get RTTI for object");
        return ojson::object();
    }

    if (type->Kind != RTTIKind::Class) {
        log_warn("Object is not a class type");
        return ojson::object();
    }

    log_info("Dumping object of type: {} @ {:p}", type->name(), (const void*)object);

    //auto j = nlohmann::json::object();
    //const auto name = type->as_class()->instanceof("RTTIRefObject")
    //    ? std::format("{}:{}", ((const RTTIRefObject*)object)->ObjectUUID, type->name())
    //    : type->name();

    return dump_class(object, type->as_class());
}

void ObjectDumper::dump_to(const RTTIObject* object, std::ostream& stream, int indent) {
    serialize_to_stream(dump(object), stream, indent);
}

ojson ObjectDumper::dump_class(const RTTIObject* object, const RTTIClass* type) {
    if (!type->OrderedFields) {
        log_warn("Object of type {} does not have Ordered Fields generated", type->Name);
        return {};
    }

    if (type->instanceof(type_map["RTTIObject"])) {
        type = object->get_rtti()->as_class(); // Sometimes 'type' is a base class
    }

    if (m_class_hook) {
        m_class_hook(object, type);
    }

    uint8_t field_buffer[256] = {};
    auto j = ojson::object();

    if (m_output_class_names) {
        j["_Type"] = type->Name;
    }

    for (const auto& field : type->get_ordered_fields()) {
        if (field.Category && !field.Name) {
            continue; // Skip category indicators, they are not actual fields
        }

        const void* field_data;
        if (!field.Get || !field.Set) {
            field_data = (const uint8_t*)object + field.Offset;
        } else {
            const auto ctor = field.Type->get_constructor<ConstructorDelegate>();
            ctor(field.Type, field_buffer);
            ((void(*)(const void*, void*))field.Get)((const uint8_t*)object + field.Offset, field_buffer);
            field_data = field_buffer;
        }


        switch (field.Type->Kind) {
        case RTTIKind::Primitive:
            j[field.Name] = dump_primitive(field_data, field.Type->as_primitive());
            break;
        case RTTIKind::Reference:
            j[field.Name] = dump_reference(field_data, field.Type->as_reference());
            break;
        case RTTIKind::Container:
            j[field.Name] = dump_container(field_data, field.Type->as_container());
            break;
        case RTTIKind::Enum: [[fallthrough]];
        case RTTIKind::EnumFlags: [[fallthrough]];
        case RTTIKind::EnumBitSet:
            j[field.Name] = dump_enum(field_data, field.Type->as_enum());
            break;
        case RTTIKind::Class:
            if (field.Type->as_class()->instanceof("GGUUID")) {
                j[field.Name] = std::format("{{{}}}", *(const GUID*)field_data);
                break;
            }
            j[field.Name] = dump_class((const RTTIObject*)field_data, field.Type->as_class());
            break;
        case RTTIKind::Pod:
            log_warn("Skipping pod field: {}", field.Name);
            break;
        }
    }

    if (type->instanceof("LocalizedTextResource")) {
        j["Text"] = std::string(
            *(const char* const*)((const uint8_t*)object + 0x20), 
            *(const uint16_t*)((const uint8_t*)object + 0x28)
        );
    }

    return j;
}

ojson ObjectDumper::dump_primitive(const void* data, const RTTIPrimitive* type) {
    const auto to_string = [data, type] {
        if (type->Serialize) {
            StringView dst;
            type->Serialize(data, dst);
            return std::string(dst.c_str(), dst.size());
        }

        return std::string{};
    };
    constexpr auto half_to_float = [](uint16_t x) {
        constexpr auto as_uint = [](float f) { return *(uint32_t*)&f; };
        constexpr auto as_float = [](uint32_t i) { return *(float*)&i; };

        const uint32_t e = (x & 0x7C00) >> 10;
        const uint32_t m = (x & 0x03FF) << 13;
        const uint32_t v = as_uint((float)m) >> 23;
        return as_float((x & 0x8000) << 16 
            | (e != 0) * ((e + 112) << 23 | m) 
            | ((e == 0) & (m != 0)) * ((v - 37) << 23
            | ((m << (150 - v)) & 0x007FE000)));
    };

    if (std::strstr(type->BaseType->Name, "int")) {
        if (type->BaseType->Name[0] == 'u') {
            switch (type->AtomSize) {
            case 1: return *(const uint8_t*)data;
            case 2: return *(const uint16_t*)data;
            case 4: return *(const uint32_t*)data;
            case 8: return *(const uint64_t*)data;
            default: return to_string();
            }
        }

        switch (type->AtomSize) {
        case 1: return *(const int8_t*)data;
        case 2: return *(const int16_t*)data;
        case 4: return *(const int32_t*)data;
        case 8: return *(const int64_t*)data;
        default: return to_string();
        }
    }
    if (std::strcmp(type->BaseType->Name, "double") == 0) {
        return *(const double*)data;
    }
    if (std::strcmp(type->BaseType->Name, "float") == 0) {
        return *(const float*)data;
    }
    if (std::strcmp(type->BaseType->Name, "bool") == 0) {
        return *(const bool*)data;
    }
    if (std::strcmp(type->BaseType->Name, "HalfFloat") == 0) {
        return half_to_float(*(const uint16_t*)data);
    }
    if (std::strcmp(type->BaseType->Name, "String") == 0) {
        return *(const char* const*)data;
    }
    if (std::strcmp(type->BaseType->Name, "WString") == 0) {
        return *(const wchar_t* const*)data;
    }

    return to_string();
}

ojson ObjectDumper::dump_enum(const void* data, const RTTIEnum* type) {
    uint64_t int_value;
    std::memcpy(&int_value, data, type->EnumSize);
    const auto value = type->find_value(int_value);
    if (!value) {
        return "N/A";
    }
    return value->Name;
}

ojson ObjectDumper::dump_container(const void* data, const RTTIContainer* type) {
    auto j = ojson::array();

    const auto get_item = (void* (*)(const RTTIContainer*, const void*, size_t))type->Data->GetItem;
    const auto get_size = (uint32_t(*)(const RTTIContainer*, const void*))type->Data->GetSize;

    if (type->Data->IsArray) {
        for (size_t i = 0; i < get_size(type, data); ++i) {
            const auto element = get_item(type, data, i);
            if (!element) {
                log_warn("Failed to find array element: [{}]", i);
                continue;
            }
            switch (type->ItemType->Kind) {
            case RTTIKind::Primitive:
                j.push_back(dump_primitive(element, type->ItemType->as_primitive()));
                break;
            case RTTIKind::Reference:
                j.push_back(dump_reference(element, type->ItemType->as_reference()));
                break;
            case RTTIKind::Container:
                j.push_back(dump_container(element, type->ItemType->as_container()));
                break;
            case RTTIKind::Enum: [[fallthrough]];
            case RTTIKind::EnumFlags: [[fallthrough]];
            case RTTIKind::EnumBitSet:
                j.push_back(dump_enum(element, type->ItemType->as_enum()));
                break;
            case RTTIKind::Class:
                j.push_back(dump_class((const RTTIObject*)element, type->ItemType->as_class()));
                break;
            case RTTIKind::Pod:
                log_warn("Skipping pod array element");
                break;
            }
        }

        return j;
    }

    if (std::string_view{type->Name}.starts_with("HashMap_")) {
        if (type->ItemType->Kind != RTTIKind::Class) {
            log_warn("HashMap item type is not a class");
            return j;
        }

        for (size_t i = 0; i < get_size(type, data); ++i) {
            const auto element = (const RTTIObject*)get_item(type, data, i);
            if (!element) {
                log_warn("Failed to find hashmap element: [{}]", i);
                continue;
            }

            j.emplace_back(dump_class(element, type->ItemType->as_class()));
        }
    }

    return j;
}

ojson ObjectDumper::dump_reference(const void* data, const RTTIReference* type) {
    if (std::strcmp(type->Data->TypeName, "UUIDRef") == 0) {
        const auto uuid = *(const GUID*)data;
        return std::format("{{{}}}", uuid);
    }
    if (std::strcmp(type->Data->TypeName, "Ref") == 0) {
        const auto target = *(const RTTIObject* const*)data;
        if (!target) {
            return { ojson::value_t::null };
        }

        return dump_class(target, type->ReferenceType->as_class());
    }

    log_warn("Unknown reference type: {}", type->Data->TypeName);
    return {};
}

void ObjectDumper::serialize_to_stream(const ojson& dump, std::ostream& stream, int indent) {
    std::scoped_lock l(m_mutex);
    stream << dump.dump(indent) << '\n';
}
