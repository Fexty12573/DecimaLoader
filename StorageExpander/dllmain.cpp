#include "pch.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <thread>
#include <print>

#include <MinHook.h>
#include <nlohmann/json.hpp>
#include <ostream>
#include <guiddef.h>
#include <combaseapi.h>
#include <functional>

#include "RTTI.h"
#include "RTTIPrimitive.h"
#include "RTTIReference.h"
#include "RTTIContainer.h"
#include "RTTIEnum.h"
#include "RTTIClass.h"

#define LOADER_NO_DEFINE_RTTI
#include "Loader.h"
#include "Memory.h"
#include "research.h"

#define DLL_MODE_LOAD
// #define DLL_MODE_DUMP

#ifdef DLL_MODE_DUMP
namespace {

bool(*register_type)(void*, RTTI*) = nullptr;
void(*register_all_types)() = nullptr;
InventoryItemResource*(*IIR_ctor)(RTTI*, InventoryItemResource*) = nullptr;
void(*IIR_dtor)(InventoryItemResource*, uint8_t) = nullptr;

std::unordered_set<InventoryItemResource*> inventory_item_resources;
std::unordered_set<RTTI*> all_types;
uintptr_t module_base;
constexpr uintptr_t standard_module_base = 0x140000000;

RTTI* inventory_item_res_rtti = nullptr;

InventoryItemResource* IIR_ctor_hook(RTTI* rtti, InventoryItemResource* res);
void IIR_dtor_hook(InventoryItemResource* obj, uint8_t x);

bool register_type_hook(void* unk, RTTI* rtti) {
    const bool result = register_type(unk, rtti);

    if (rtti) {
        ::all_types.insert(rtti);

        if (rtti->Kind == RTTIKind::Class && strcmp(rtti->as_class()->Name, "InventoryItemResource") == 0) {
            inventory_item_res_rtti = rtti;

            const auto ctor = (void*)rtti->as_class()->Constructor;
            MH_CreateHook(ctor, (void*)&IIR_ctor_hook, (void**)&::IIR_ctor);
            MH_EnableHook(ctor);
        }
    }

    return result;
}

void register_all_types_hook() {
    //MessageBoxA(nullptr, "register_all_types_hook", "RTTI Dumper", MB_OK);

    register_all_types();

#if 0
    nlohmann::json j = nlohmann::json::array();
    std::ofstream out("rtti.json");

    const auto as_json = [](RTTI* rtti) {
        nlohmann::json obj = {
            { "Address", (uintptr_t)rtti - ::module_base + ::standard_module_base }
        };

        switch (rtti->Kind) {
        case RTTIKind::Primitive:
            obj["Kind"] = RTTIPrimitive::TypeName;
            obj["Name"] = rtti->as_primitive()->Name;
            break;
        case RTTIKind::Reference:
            obj["Kind"] = RTTIReference::TypeName;
            obj["Name"] = rtti->as_reference()->Name;
            break;
        case RTTIKind::Container:
            obj["Kind"] = RTTIContainer::TypeName;
            obj["Name"] = rtti->as_container()->Name;
            break;
        case RTTIKind::Enum:
            obj["Kind"] = RTTIEnum::TypeName;
            obj["Name"] = rtti->as_enum()->Name;
            break;
        case RTTIKind::Class:
            obj["Kind"] = RTTIClass::TypeName;
            obj["Name"] = rtti->as_class()->Name;
            break;
        default:
            obj["Kind"] = "Unknown";
            obj["Name"] = "Unknown";
            break;
        }

        return obj;
    };

    for (const auto r : ::all_types) {
        j.emplace_back(as_json(r));
    }

    out << j.dump(4);
    out.flush();

    exit(0);
#endif
}

void IIR_dtor_hook(InventoryItemResource* obj, uint8_t x) {
    //std::cout << "DESTROY " << (uintptr_t)obj << '\n';
    inventory_item_resources.erase(obj);
    return IIR_dtor(obj, x);
}

InventoryItemResource* IIR_ctor_hook(RTTI* rtti, InventoryItemResource* res) {
    InventoryItemResource* result = IIR_ctor(rtti, res);
    if (!res) {
        return result;
    }

    if (inventory_item_resources.empty()) {
        const auto dtor = (*(void***)res)[1];
        MH_CreateHook(dtor, (void*)&IIR_dtor_hook, (void**)&::IIR_dtor);
        MH_EnableHook(dtor);
    }

    //std::cout << "CREATE " << (uintptr_t)res << '\n';
    inventory_item_resources.emplace(res);

    return result;
}

bool is_object_safe(InventoryItemResource* obj) {
    __try {
        if (!obj) {
            return false;
        }

        if (!obj->ItemName) {
            return false;
        }

        if (!obj->ItemName->Text) {
            return false;
        }

        if (obj->MaxStackSize.empty()) {
            return false;
        }

        if (!obj->MaxStackSize.data()) {
            return false;
        }

        // Access some fields to ensure they are valid
        volatile auto c = obj->MaxStackSize[0];
        c = (int)(unsigned char)obj->ItemName->Text[0];

        if (obj->get_rtti()->TypeId != ::inventory_item_res_rtti->TypeId) {
            return false;
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

DWORD run(LPVOID) {
    AllocConsole();
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);

    module_base = (uintptr_t)GetModuleHandleA(nullptr);

    Section section;
    if (!FindSection((void*)module_base, ".text", &section)) {
        perror("Unable to find '.text' section in the executable");
        return FALSE;
    }

    void* RTTIFactory_RegisterAllTypes = nullptr;
    void* RTTIFactory_RegisterType = nullptr;
    if (!FindPattern(section.start, section.end, "40 55 48 8B EC 48 83 EC 70 80 3D ? ? ? ? ? 0F 85 ? ? ? ? 48 89 9C 24",
        &RTTIFactory_RegisterAllTypes)) {
        perror("Unable to find 'RTTIFactory::RegisterAllTypes' function in the executable");
        return FALSE;
    }

    if (!FindPattern(section.start, section.end, "40 55 53 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 0F B6 42 05 48 8B DA 48 8B",
        &RTTIFactory_RegisterType)) {
        perror("Unable to find 'RTTIFactory::RegisterType' function in the executable");
        return FALSE;
    }

    std::cout << "RTTIFactory::RegisterAllTypes: " << RTTIFactory_RegisterAllTypes << '\n';
    std::cout << "RTTIFactory::RegisterType: " << RTTIFactory_RegisterType << '\n';

    MH_Initialize();

    MH_CreateHook(RTTIFactory_RegisterAllTypes, (void*)register_all_types_hook, (void**)&register_all_types);
    MH_CreateHook(RTTIFactory_RegisterType, (void*)register_type_hook, (void**)&register_type);

    MH_QueueEnableHook(MH_ALL_HOOKS);
    MH_ApplyQueued();

    std::cout << "RTTI Dumper\n";

    std::thread([] {
        (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        const auto format_guid = [](const GUID& guid) -> std::string {
            LPOLESTR guid_str;
            (void)StringFromCLSID(guid, &guid_str);

            std::wstring ws(guid_str);
            std::string str(ws.begin(), ws.end());

            CoTaskMemFree(guid_str);

            return { ws.begin(), ws.end() };
        };

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            if (GetAsyncKeyState(VK_F7) & 1) {
                // Print number of InventoryItemResource instances
                std::cout << "InventoryItemResource instances: " << inventory_item_resources.size() << '\n';
            }

            if (GetAsyncKeyState(VK_F6) & 1) {
                // Dump all InventoryItemResource instances
                std::cout << "Dumping InventoryItemResource instances\n";
                nlohmann::json j = nlohmann::json::array();

                for (const auto iir : inventory_item_resources) {
                    if (!is_object_safe(iir)) {
                        std::cout << "Skipping unsafe InventoryItemResource instance\n";
                        continue;
                    }
                    try {
                        nlohmann::json amounts = nlohmann::json::array();
                        if (!IsBadReadPtr(iir->MaxStackSize.data(), iir->MaxStackSize.size() * 4)) {
                            for (int i = 0; i < iir->MaxStackSize.size(); i++) {
                                amounts.emplace_back(iir->MaxStackSize[i]);
                            }
                        }

                        const bool has_name = iir->ItemName && iir->ItemName->Text;
                        const std::string name = has_name
                            ? std::string{ iir->ItemName->Text, iir->ItemName->Length }
                            : "N/A";

                        std::cout << "Dumping " << name << '\n';

                        nlohmann::json obj = {
                            { "UUID", format_guid(iir->ObjectUUID) },
                            { "Name", name },
                            { "MaxStacks", amounts }
                        };

                        j.emplace_back(obj);
                    } catch (const std::exception& e) {
                        std::cerr << "Error: " << e.what() << '\n';
                    }
                }

                std::cout << "Dumped InventoryItemResource instances\n" << std::flush;
                std::cout << j.dump(4, ' ', false, 
                    nlohmann::detail::error_handler_t::ignore) << '\n' << std::flush;

                try {
                    std::ofstream out("inventory_item_resources.json");
                    out << j.dump(4);
                    out.flush();
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << '\n';
                }

                std::cout << "Dumped InventoryItemResource instances\n";
            }
        }
    }).detach();

    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        run(nullptr);
    }

    return TRUE;
}
#elif defined(DLL_MODE_LOAD)

template<>
struct std::hash<GUID> {
    std::size_t operator()(const GUID& g) const noexcept {
        using std::size_t;
        using std::hash;

        return hash<uint32_t>()(g.Data1) ^ hash<uint16_t>()(g.Data2) ^ hash<uint16_t>()(g.Data3) ^ hash<uint64_t>()(*(uint64_t*)&g.Data4);
    }
};

std::ostream& operator<<(std::ostream& os, const GUID& g) {
    os << std::format("{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
        g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return os;
}

template<>
struct std::formatter<GUID> : std::formatter<std::string> {
    template<typename FormatContext>
    auto format(const GUID& g, FormatContext& ctx) const {
        return std::formatter<std::string>::format(std::format("{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
            g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]), ctx);
    }
};

namespace {

RTTIClass* icc_rtti = nullptr;
std::unordered_map<GUID, std::vector<int>> stack_sizes;
std::vector<RTTI*> all_types;
std::ofstream log("deserializer_log.txt");

void(*handle_get_amount)(void* p, MsgGetMaxFitAmount* msg) = nullptr;
void handle_get_amount_hook(void* p, MsgGetMaxFitAmount* msg) {
    if (!msg->ItemResource) {
        return handle_get_amount(p, msg);
    }

#ifdef _DEBUG
    std::cout << "Checking stack sizes for item " << msg->item_name() << '\n';
    std::cout << "UUID: " << msg->ItemResource->ObjectUUID << '\n';
#endif

    const auto it = stack_sizes.find(msg->ItemResource->ObjectUUID);
    if (it != stack_sizes.end()) {
#ifdef _DEBUG
        std::cout << "Found stack sizes for item " << msg->ItemResource->ItemName->Text << '\n';
#endif

        if (it->second.size() != msg->ItemResource->MaxStackSize.size()) {
            const auto text = std::format("Stack sizes mismatch for item {}", msg->ItemResource->ItemName->Text);
            MessageBoxA(nullptr, text.c_str(), "Storage Expander", MB_OK);
            return handle_get_amount(p, msg);
        }

        for (int i = 0; i < it->second.size(); i++) {
            msg->ItemResource->MaxStackSize[i] = it->second[i];
        }
    }

    return handle_get_amount(p, msg);
}

int(*read_object)(void* reader, RTTIObject** pobj) = nullptr;
int read_object_hook(void* reader, RTTIObject** pobj) {
    const int result = read_object(reader, pobj);
    if (result == 0 && pobj && *pobj) {
        RTTIObject* obj = *pobj;
        const auto rtti = obj->get_rtti();
        if (rtti->Kind == RTTIKind::Class && rtti->as_class()->instanceof("RTTIRefObject")) {
            const auto ref_obj = (RTTIRefObject*)obj;
            std::println(log, "Deserialized Object [{}]: {}", rtti->name(), ref_obj->ObjectUUID);
        } else {
            std::println(log, "Deserialized Object [{}]", rtti->name());
        }
    }

    return result;
}

void on_register_type(RTTI* rtti) {
    all_types.push_back(rtti);

    if (rtti && rtti->Kind == RTTIKind::Container) {
        const auto* container = rtti->as_container();
        std::println(log, "Registered Container: {}", container->Name);
    }

    if (rtti && rtti->Kind == RTTIKind::Class && strcmp(rtti->name(), "InventoryCapacityComponent") == 0) {
        icc_rtti = rtti->as_class();

#ifdef _DEBUG
        std::cout << "Found InventoryCapacityComponent RTTI\n" << std::flush;
#endif

        for (uint8_t i = 0; i < icc_rtti->MessageHandlerCount; ++i) {
            const auto& handler = icc_rtti->MessageHandlers[i];
            if (handler.MessageType->Kind == RTTIKind::Class &&
                strcmp(handler.MessageType->as_class()->Name, "MsgGetMaxFitAmount") == 0) {
                // Hook the handler
                MH_CreateHook(handler.Handler, (void*)&handle_get_amount_hook, (void**)&handle_get_amount);
                MH_EnableHook(handler.Handler);

#ifdef _DEBUG
                std::cout << "Hooked MsgGetMaxFitAmount handler\n" << std::flush;
#endif
            }
        }
    } else if (rtti && strcmp(rtti->name(), "InventoryItemComponent") == 0) {
        const auto* iic_rtti = rtti->as_class();
        for (uint8_t i = 0; i < iic_rtti->MessageHandlerCount; ++i) {
            const auto& handler = iic_rtti->MessageHandlers[i];
            if (handler.MessageType->Kind == RTTIKind::Class &&
                strcmp(handler.MessageType->as_class()->Name, "MsgEntityInit") == 0) {
                // Hook the handler
                //MH_CreateHook(handler.Handler, (void*)&handle_get_amount_hook, (void**)&handle_get_amount);
                //MH_EnableHook(handler.Handler);
            }
        }
    }
}

void on_post_register_all_types() {
    //research::init(all_types);

    constexpr uintptr_t standard_module_base = 0x140000000;
    uintptr_t module_base = (uintptr_t)GetModuleHandleA(nullptr);

    nlohmann::json j = nlohmann::json::array();
    std::ofstream out("rtti.json");

    const auto as_json = [module_base, standard_module_base](RTTI* rtti) {
        nlohmann::json obj = {
            { "Address", (uintptr_t)rtti - module_base + standard_module_base }
        };

        switch (rtti->Kind) {
        case RTTIKind::Primitive:
            obj["Kind"] = RTTIPrimitive::TypeName;
            obj["Name"] = rtti->as_primitive()->Name;
            break;
        case RTTIKind::Reference:
            obj["Kind"] = RTTIReference::TypeName;
            obj["Name"] = rtti->as_reference()->Name;
            break;
        case RTTIKind::Container:
            obj["Kind"] = RTTIContainer::TypeName;
            obj["Name"] = rtti->as_container()->Name;
            break;
        case RTTIKind::Enum:
            obj["Kind"] = RTTIEnum::TypeName;
            obj["Name"] = rtti->as_enum()->Name;
            break;
        case RTTIKind::Class:
            obj["Kind"] = RTTIClass::TypeName;
            obj["Name"] = rtti->as_class()->Name;
            break;
        default:
            obj["Kind"] = "Unknown";
            obj["Name"] = "Unknown";
            break;
        }

        return obj;
    };

    for (const auto r : ::all_types) {
        j.emplace_back(as_json(r));
    }

    out << j.dump(4);
    out.flush();

    exit(0);
}

}

PLUGIN_API void plugin_initialize(PluginInitializeOptions* options) {
    options->OnPostRegisterType = ::on_register_type;
    //options->OnPostRegisterAllTypes = ::on_post_register_all_types;

//#ifndef _DEBUG
//    AllocConsole();
//    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
//#endif

    MH_Initialize();

    std::println("Image Base: {:p}", (void*)GetModuleHandleA(nullptr));

    //const auto hook_addr = (void*)0x140698810;
    //MH_CreateHook(hook_addr, (void*)&read_object_hook, (void**)&read_object);
    //MH_EnableHook(hook_addr);
    
    // Load the Data file
    if (!std::filesystem::exists("plugins/stack_sizes.json")) {
        MessageBoxA(nullptr, "stack_sizes.json not found", "RTTI Dumper", MB_OK);
        return;
    }

    nlohmann::json j;
    std::ifstream("plugins/stack_sizes.json") >> j;

    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (const auto& obj : j) {
        GUID uuid;
        const std::string uuid_str = obj["UUID"].get<std::string>();
        std::wstring uuid_wstr(uuid_str.begin(), uuid_str.end());
        (void)IIDFromString(uuid_wstr.c_str(), &uuid);

        const auto& amounts = obj["MaxStacks"];
        std::vector<int> sizes;
        for (const auto& amount : amounts) {
            sizes.emplace_back(amount.get<int>());
        }

        stack_sizes[uuid] = sizes;
    }
    CoUninitialize();

#ifdef _DEBUG
    std::cout << "Loaded stack sizes\n" << std::flush;
#endif
}

BOOL APIENTRY DllMain(HINSTANCE hdll, DWORD reason, LPVOID) {
    return TRUE;
}

#endif
