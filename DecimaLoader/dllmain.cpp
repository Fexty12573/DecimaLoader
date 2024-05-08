#include "pch.h"
#include "PatternScan.h"
#include "Loader.h"

#include <MinHook.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <filesystem>
#include <memory>
#include <vector>


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

std::vector<PluginInitializeOptions> plugin_options;

void init_logger() {
    // Initialize a file logger
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::debug);

    const auto file_logger = spdlog::basic_logger_mt("logger", "loader.log");
    spdlog::set_default_logger(file_logger);
}

bool run() {
    init_logger();

    spdlog::info("Initialization started");

    typedef int(*FWinMain)(HINSTANCE, HINSTANCE, LPSTR, int);
    typedef bool(*FRegisterType)(void*, RTTI*);
    typedef void(*FRegisterAllTypes)();

    auto winmain = (FWinMain)PatternScanner::find_first(Pattern::from_string(winmain_pattern));
    auto register_type = (FRegisterType)PatternScanner::find_first(Pattern::from_string(register_type_pattern));
    auto register_all_types = (FRegisterAllTypes)PatternScanner::find_first(Pattern::from_string(register_all_types_pattern));

    if (!winmain || !register_type || !register_all_types) {
        spdlog::error("Failed to find pattern");
        return false;
    }

    spdlog::info("Found WinMain at: {:p}", (void*)winmain);
    spdlog::info("Found RegisterType at: {:p}", (void*)register_type);
    spdlog::info("Found RegisterAllTypes at: {:p}", (void*)register_all_types);
    
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

        for (const auto& options : plugin_options) {
            if (options.OnPostRegisterAllTypes) {
                options.OnPostRegisterAllTypes();
            }
        }
    });

    MH_ApplyQueued();

    // Load plugins
    namespace fs = std::filesystem;

    for (const auto& entry : fs::directory_iterator("plugins")) {
        if (entry.is_regular_file() && entry.path().extension() == ".dll") {
            const auto module = LoadLibraryW(entry.path().c_str());
            if (!module) {
                spdlog::error("Failed to load plugin: {}", entry.path().filename().string());
                return false;
            }

            const auto init = (void(*)(PluginInitializeOptions*))GetProcAddress(module, "plugin_initialize");
            if (!init) {
                spdlog::error("Failed to find plugin_initialize in: {}", entry.path().filename().string());
                return false;
            }

            PluginInitializeOptions options = {};
            init(&options);

            plugin_options.push_back(options);

            spdlog::info("Loaded plugin: {}", entry.path().filename().string());
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
