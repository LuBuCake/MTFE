/*
    This file is part of MTFE (MT Framework Extender).
    MTFE is free software: you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.
    MTFE is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with MTFE. If not, see <https://www.gnu.org/licenses/>.
*/

// MTFE loader: dinput8.dll proxy that loads MTFE plugins once Enigma has unpacked re5dx9.exe.
//
// Timeline:
//   DllMain (Enigma still unpacking)  -> hot-patch hook on kernelbase!GetSystemTimeAsFileTime
//   first call from the game's .text  -> that is __security_init_cookie at the OEP: unhook, load plugins,
//                                        POST_LOAD / POST_POST_LOAD, install the game hooks below
//   __tmainCRTStartup -> WinMain call -> PRE_WINMAIN (static initializers done, MtDTI tree built)
//   sMain per-frame vfunc             -> GAME_INIT once, then the task queue every frame

#include "Hook.h"
#include "LauncherFix.h"
#include "Log.h"
#include "ModFolders.h"
#include "ModMenu.h"
#include "PluginManager.h"
#include "Runtime.h"

#include <MTFE/PluginAPI.h>

#include <Windows.h>
#include <intrin.h>

#include <atomic>
#include <filesystem>
#include <string>

#pragma intrinsic(_ReturnAddress)

namespace
{
    HMODULE SelfModule = nullptr;
    std::string GameDir;
    std::string MTFEDir;
    bool LoadLegacy = true;
    bool ModFoldersEnabled = true;
    bool LogModFiles = true;
    bool ModsMenuEnabled = true;

    Hook::ApiHook EntryHook;
    std::atomic<bool> Entered{ false };

    using WinMain_t = int(__stdcall*)(HINSTANCE, HINSTANCE, LPSTR, int);
    WinMain_t WinMainOriginal = nullptr;

    using Frame_t = void(__thiscall*)(void*);
    Frame_t FrameOriginal = nullptr;
    bool GameInitSent = false;

    int __stdcall WinMainHook(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine, int Show)
    {
        Log::Info("PRE_WINMAIN");
        PluginManager::Broadcast(MTFE_MESSAGE_PRE_WINMAIN);
        return WinMainOriginal(Instance, PrevInstance, CmdLine, Show);
    }

    void __fastcall FrameHook(void* Self, void*)
    {
        if (!GameInitSent)
        {
            GameInitSent = true;
            Log::Info("GAME_INIT (sMain {:08X})", reinterpret_cast<std::uintptr_t>(Self));
            PluginManager::Broadcast(MTFE_MESSAGE_GAME_INIT, Self, sizeof(void*));
        }
        PluginManager::RunTasks();
        FrameOriginal(Self);
    }

    void InstallGameHooks()
    {
        const auto* Game = Runtime::Get().GameAddresses;
        if (!Game)
        {
            Log::Warn("unknown runtime: PRE_WINMAIN, GAME_INIT and tasks are disabled");
            return;
        }

        const auto* OEP = reinterpret_cast<const std::uint8_t*>(Game->OEP);
        if (OEP[0] != 0xE8 || OEP[5] != 0xE9)
        {
            Log::Error("OEP {:08X} does not look like the MSVC entry: game hooks not installed", Game->OEP);
            return;
        }

        const auto* Call = reinterpret_cast<const std::uint8_t*>(Game->WinMainCall);
        const auto CallTarget = Game->WinMainCall + 5 + *reinterpret_cast<const std::int32_t*>(Call + 1);
        if (Call[0] == 0xE8 && CallTarget == Game->WinMain)
        {
            WinMainOriginal = reinterpret_cast<WinMain_t>(Hook::WriteCall5(Game->WinMainCall, reinterpret_cast<std::uintptr_t>(&WinMainHook)));
        }
        else
        {
            Log::Error("WinMain call site {:08X} changed: PRE_WINMAIN disabled", Game->WinMainCall);
        }

        FrameOriginal = reinterpret_cast<Frame_t>(Hook::WriteVFunc(Game->SBioMainVtbl, Game->FrameSlot, reinterpret_cast<std::uintptr_t>(&FrameHook)));
        Log::Info("game hooks: WinMain {}, frame vfunc {:08X}", WinMainOriginal ? "ok" : "off", reinterpret_cast<std::uintptr_t>(FrameOriginal));

        if (ModFoldersEnabled)
        {
            ModFolders::Install(*Game, { .MTFEPath = MTFEDir, .LogEachFile = LogModFiles });
        }
        else
        {
            Log::Info("mod folders: off (bModFolders=0)");
        }

        if (ModsMenuEnabled)
        {
            ModMenu::InstallUI(*Game);
        }
        else
        {
            Log::Info("mod menu: off (bModsMenu=0)");
        }
    }

    void OnGameEntry(const char* How)
    {
        Log::Info("game entry ({}): code unpacked, loading plugins", How);
        InstallGameHooks();
        PluginManager::LoadAll(LoadLegacy);
        PluginManager::Broadcast(MTFE_MESSAGE_POST_LOAD);
        PluginManager::Broadcast(MTFE_MESSAGE_POST_POST_LOAD);
    }

    using GetSystemTimeAsFileTime_t = void(WINAPI*)(LPFILETIME);

    void WINAPI GetSystemTimeAsFileTimeHook(LPFILETIME Time)
    {
        const auto Original = reinterpret_cast<GetSystemTimeAsFileTime_t>(EntryHook.Original());
        if (!Entered.load(std::memory_order_relaxed) && Runtime::IsGameCode(_ReturnAddress()))
        {
            if (!Entered.exchange(true))
            {
                EntryHook.Remove();
                OnGameEntry("first GetSystemTimeAsFileTime from the game's code");
            }
        }
        Original(Time);
    }

    bool AlreadyRunning()
    {
        // Loaded after the OEP (e.g. by LoadLibrary at runtime): the security cookie is no longer the MSVC default.
        const auto* Game = Runtime::Get().GameAddresses;
        if (!Game)
        {
            return false;
        }
        const auto* OEP = reinterpret_cast<const std::uint8_t*>(Game->OEP);
        return OEP[0] == 0xE8 && OEP[5] == 0xE9 && *reinterpret_cast<const std::uint32_t*>(0x011AC150) != 0xBB40E64E;
    }

    DWORD WINAPI LateEntryThread(void*)
    {
        if (!Entered.exchange(true))
        {
            OnGameEntry("late load, game already running");
        }
        return 0;
    }

    void Startup()
    {
        char Path[MAX_PATH];
        GetModuleFileNameA(nullptr, Path, MAX_PATH);
        GameDir = std::filesystem::path(Path).parent_path().string() + "\\";
        MTFEDir = GameDir + "MTFE\\";
        std::error_code ErrorCode;
        std::filesystem::create_directories(MTFEDir + "Logs", ErrorCode);
        std::filesystem::create_directories(MTFEDir + "Plugins", ErrorCode);

        const auto Ini = MTFEDir + "MTFE.ini";
        const bool Console = GetPrivateProfileIntA("Debug", "bConsole", 0, Ini.c_str()) != 0;
        LoadLegacy = GetPrivateProfileIntA("Loader", "bLoadLegacyPlugins", 1, Ini.c_str()) != 0;
        const bool Enabled = GetPrivateProfileIntA("Loader", "bEnablePlugins", 1, Ini.c_str()) != 0;
        const bool CloseLauncher = GetPrivateProfileIntA("Loader", "bCloseLauncher", 1, Ini.c_str()) != 0;
        ModFoldersEnabled = GetPrivateProfileIntA("Resources", "bModFolders", 1, Ini.c_str()) != 0;
        LogModFiles = GetPrivateProfileIntA("Resources", "bLogModFiles", 1, Ini.c_str()) != 0;
        ModsMenuEnabled = GetPrivateProfileIntA("Menu", "bModsMenu", 1, Ini.c_str()) != 0;

        Log::Open(MTFEDir + "Logs\\mtfe.log", Console);
        Log::Info("MTFE {}.{}.{}", MTFE_VERSION_MAJOR(MTFE_VERSION), MTFE_VERSION_MINOR(MTFE_VERSION), MTFE_VERSION_PATCH(MTFE_VERSION));

        const auto& Detected = Runtime::Detect();
        Log::Info("game: {} base {:08X}, runtime {}", Path, Detected.Base, Detected.Name);
        PluginManager::SetPaths(GameDir, MTFEDir);

        if (CloseLauncher)
        {
            // in a thread: it starts after DllMain returns (no work under the loader lock)
            CloseHandle(CreateThread(nullptr, 0, [](void*) -> DWORD
                                     { LauncherFix::CloseParentLauncher(GameDir); return 0; }, nullptr, 0, nullptr));
        }

        if (!Enabled)
        {
            Log::Info("bEnablePlugins=0: nothing loaded");
            return;
        }

        if (AlreadyRunning())
        {
            Log::Warn("loaded after the game started: no PRE_WINMAIN for plugins");
            CloseHandle(CreateThread(nullptr, 0, LateEntryThread, nullptr, 0, nullptr));
            return;
        }

        auto* Target = GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetSystemTimeAsFileTime");
        if (!EntryHook.Install(Target, reinterpret_cast<void*>(&GetSystemTimeAsFileTimeHook)))
        {
            Target = GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemTimeAsFileTime");
            if (!EntryHook.Install(Target, reinterpret_cast<void*>(&GetSystemTimeAsFileTimeHook)))
            {
                Log::Error("could not hook GetSystemTimeAsFileTime: plugins will not load");
                return;
            }
        }
        Log::Info("waiting for the game entry point");
    }

    // ---- dinput8 proxy ----

    HMODULE RealDll = nullptr;

    FARPROC Real(const char* Name)
    {
        if (!RealDll)
        {
            char SystemDir[MAX_PATH];
            GetSystemDirectoryA(SystemDir, MAX_PATH);
            RealDll = LoadLibraryA((std::string(SystemDir) + "\\dinput8.dll").c_str());
            if (!RealDll)
            {
                MessageBoxA(nullptr, "Could not load the system dinput8.dll.", "MTFE", MB_ICONERROR);
                ExitProcess(1);
            }
        }
        return GetProcAddress(RealDll, Name);
    }
}

extern "C"
{
    HRESULT __stdcall Proxy_DirectInput8Create(HINSTANCE Instance, DWORD Version, REFIID Riid, void** Out, void* Outer)
    {
        using Fn = HRESULT(__stdcall*)(HINSTANCE, DWORD, REFIID, void**, void*);
        return reinterpret_cast<Fn>(Real("DirectInput8Create"))(Instance, Version, Riid, Out, Outer);
    }

    HRESULT __stdcall Proxy_DllCanUnloadNow()
    {
        return reinterpret_cast<HRESULT(__stdcall*)()>(Real("DllCanUnloadNow"))();
    }

    HRESULT __stdcall Proxy_DllGetClassObject(REFCLSID Clsid, REFIID Riid, void** Out)
    {
        using Fn = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);
        return reinterpret_cast<Fn>(Real("DllGetClassObject"))(Clsid, Riid, Out);
    }

    HRESULT __stdcall Proxy_DllRegisterServer()
    {
        return reinterpret_cast<HRESULT(__stdcall*)()>(Real("DllRegisterServer"))();
    }

    HRESULT __stdcall Proxy_DllUnregisterServer()
    {
        return reinterpret_cast<HRESULT(__stdcall*)()>(Real("DllUnregisterServer"))();
    }

    const void* __stdcall Proxy_GetdfDIJoystick()
    {
        return reinterpret_cast<const void*(__stdcall*)()>(Real("GetdfDIJoystick"))();
    }
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        SelfModule = Module;
        DisableThreadLibraryCalls(Module);
        Startup();
    }
    return TRUE;
}
