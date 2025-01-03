#pragma once

#include <iostream>
#include <mutex>

#include <spdlog/spdlog.h>
#include <nlohmann/json_fwd.hpp>

#include "RTTI/RTTI.h"

class RTTIObject;

struct ObjectDumperConfig;

class ObjectDumper {
public:
    explicit ObjectDumper(const ObjectDumperConfig& config);

    nlohmann::ordered_json dump(const RTTIObject* object);
    void dump_to(const RTTIObject* object, std::ostream& stream, int indent = -1);

    using ClassHook = std::function<void(const RTTIObject*, const RTTIClass*)>;

private:
    nlohmann::ordered_json dump_class(const RTTIObject* object, const RTTIClass* type);
    nlohmann::ordered_json dump_primitive(const void* data, const RTTIPrimitive* type);
    nlohmann::ordered_json dump_enum(const void* data, const RTTIEnum* type);
    nlohmann::ordered_json dump_container(const void* data, const RTTIContainer* type);
    nlohmann::ordered_json dump_reference(const void* data, const RTTIReference* type);

    void serialize_to_stream(const nlohmann::ordered_json& dump, std::ostream& stream, int indent);

private:
    std::mutex m_mutex;
    std::shared_ptr<spdlog::logger> m_logger;
    bool m_output_array_indices = false;
    bool m_output_class_names = false;
    int m_max_depth = -1;
    ClassHook m_class_hook = nullptr;
    int m_current_depth = 0;
};

struct ObjectDumperConfig {
    spdlog::sink_ptr log_sink = nullptr;
    bool output_array_indices = false;
    bool output_class_names = false;
    int max_depth = -1;
    ObjectDumper::ClassHook class_hook = nullptr;
};
