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

#include "PluginManager.h"

#include "Log.h"
#include "ModMenu.h"
#include "Runtime.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <vector>

namespace PluginManager
{
    namespace
    {
        constexpr const char* kSender = "MTFE";

        struct Plugin
        {
            HMODULE Module = nullptr;
            std::string File;
            const MTFEPluginVersionData* Version = nullptr;
            MTFEPluginInfo Info{};
        };

        struct Listener
        {
            MTFEPluginHandle Handle;
            std::string Sender; // empty = every sender
            MTFEMessageCallback Callback;
        };

        struct Task
        {
            MTFETaskFunction Function;
            void* Context;
        };

        std::string GameDir;
        std::string MTFEDir;
        std::vector<Plugin> Plugins;
        std::vector<Listener> Listeners;
        MTFEPluginHandle CurrentPlugin = MTFE_INVALID_PLUGIN_HANDLE;

        std::mutex TaskLock;
        std::vector<Task> Tasks;
        std::vector<Task> FrameCallbacks;

        const char* NameOf(MTFEPluginHandle Handle)
        {
            return Handle < Plugins.size() ? Plugins[Handle].Info.name : nullptr;
        }

        std::uint32_t RegisterListener(MTFEPluginHandle Owner, const char* Sender, MTFEMessageCallback Handler)
        {
            if (Owner >= Plugins.size() || !Handler)
            {
                return 0;
            }
            Listeners.push_back({ Owner, Sender ? Sender : "", Handler });
            return 1;
        }

        void Deliver(const char* SenderName, std::uint32_t Type, void* Data, std::uint32_t Length, const char* Receiver)
        {
            for (const auto& Item : Listeners)
            {
                if (!Item.Sender.empty() && Item.Sender != SenderName)
                {
                    continue;
                }
                if (Receiver && _stricmp(Receiver, Plugins[Item.Handle].Info.name) != 0)
                {
                    continue;
                }
                MTFEMessage Message{ SenderName, Type, Length, Data };
                Item.Callback(&Message);
            }
        }

        std::uint32_t Dispatch(MTFEPluginHandle Sender, std::uint32_t Type, void* Data, std::uint32_t Length, const char* Receiver)
        {
            const auto* Name = NameOf(Sender);
            if (!Name)
            {
                return 0;
            }
            Deliver(Name, Type, Data, Length, Receiver);
            return 1;
        }

        void AddTask(MTFETaskFunction Function, void* Context)
        {
            if (Function)
            {
                std::lock_guard Guard(TaskLock);
                Tasks.push_back({ Function, Context });
            }
        }

        void AddFrameCallback(MTFETaskFunction Function, void* Context)
        {
            if (Function)
            {
                std::lock_guard Guard(TaskLock);
                FrameCallbacks.push_back({ Function, Context });
            }
        }

        MTFEMessagingInterface MessagingInterface{ MTFE_MESSAGING_INTERFACE_VERSION, RegisterListener, Dispatch };
        MTFETaskInterface TaskInterface{ MTFE_TASK_INTERFACE_VERSION, AddTask, AddFrameCallback };

        void* QueryInterface(std::uint32_t ID)
        {
            switch (ID)
            {
            case MTFE_INTERFACE_MESSAGING:
                return &MessagingInterface;
            case MTFE_INTERFACE_TASK:
                return &TaskInterface;
            case MTFE_INTERFACE_MENU:
                return const_cast<MTFEMenuInterface*>(ModMenu::GetInterface());
            default:
                return nullptr;
            }
        }

        MTFEPluginHandle GetPluginHandle()
        {
            return CurrentPlugin;
        }

        const MTFEPluginInfo* GetPluginInfo(const char* Name)
        {
            for (const auto& Entry : Plugins)
            {
                if (Name && _stricmp(Entry.Info.name, Name) == 0)
                {
                    return &Entry.Info;
                }
            }
            return nullptr;
        }

        const char* GetGamePath()
        {
            return GameDir.c_str();
        }
        const char* GetMTFEPath()
        {
            return MTFEDir.c_str();
        }

        MTFELoadInterface LoadInterface{
            MTFE_VERSION, 0, 0, QueryInterface, GetPluginHandle, GetPluginInfo, GetGamePath, GetMTFEPath
        };

        // No C++ objects in here: __try cannot unwind them.
        int CallLoad(MTFEPlugin_LoadFunc LoadFunction, const MTFELoadInterface* Interface, DWORD* Code)
        {
            __try
            {
                return LoadFunction(Interface) ? 1 : 0;
            }
            __except (*Code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
        }

        std::string CheckCompatibility(const MTFEPluginVersionData& VersionData)
        {
            if (VersionData.dataVersion != MTFE_PLUGIN_VERSION_DATA_VERSION)
            {
                return std::format("version data {} (expected {})", VersionData.dataVersion, MTFE_PLUGIN_VERSION_DATA_VERSION);
            }
            if (VersionData.mtfeVersionRequired > MTFE_VERSION)
            {
                return std::format("needs MTFE {}.{}.{}", MTFE_VERSION_MAJOR(VersionData.mtfeVersionRequired),
                                   MTFE_VERSION_MINOR(VersionData.mtfeVersionRequired), MTFE_VERSION_PATCH(VersionData.mtfeVersionRequired));
            }
            if (VersionData.addressIndependence & MTFE_ADDRESS_INDEPENDENT_SIGNATURES)
            {
                return {};
            }
            const auto CurrentRuntime = Runtime::Get().Timestamp;
            for (auto Compatible : VersionData.compatibleVersions)
            {
                if (Compatible == 0)
                {
                    break;
                }
                if (Compatible == CurrentRuntime)
                {
                    return {};
                }
            }
            return std::format("not built for this game runtime ({})", Runtime::Get().Name);
        }

        void LoadPlugin(const std::filesystem::path& Path)
        {
            const auto File = Path.filename().string();
            HMODULE Module = LoadLibraryW(Path.c_str());
            if (!Module)
            {
                Log::Error("{}: LoadLibrary failed ({})", File, GetLastError());
                return;
            }

            const auto* Version = reinterpret_cast<const MTFEPluginVersionData*>(GetProcAddress(Module, "MTFEPlugin_Version"));
            const auto LoadFunction = reinterpret_cast<MTFEPlugin_LoadFunc>(GetProcAddress(Module, "MTFEPlugin_Load"));
            if (!Version || !LoadFunction)
            {
                Log::Error("{}: not an MTFE plugin (missing MTFEPlugin_Version/MTFEPlugin_Load)", File);
                FreeLibrary(Module);
                return;
            }
            if (const auto Why = CheckCompatibility(*Version); !Why.empty())
            {
                Log::Error("{} ({}): skipped, {}", File, Version->name, Why);
                FreeLibrary(Module);
                return;
            }

            Plugins.push_back({ Module, File, Version, { 1, Version->name, Version->pluginVersion } });
            const auto Handle = static_cast<MTFEPluginHandle>(Plugins.size() - 1);
            CurrentPlugin = Handle;

            DWORD Code = 0;
            const auto Result = CallLoad(LoadFunction, &LoadInterface, &Code);
            CurrentPlugin = MTFE_INVALID_PLUGIN_HANDLE;

            if (Result == 1)
            {
                Log::Info("{} ({} {}.{}.{} by {}): loaded", File, Version->name, MTFE_VERSION_MAJOR(Version->pluginVersion),
                          MTFE_VERSION_MINOR(Version->pluginVersion), MTFE_VERSION_PATCH(Version->pluginVersion), Version->author);
                return;
            }
            if (Result == -1)
            {
                Log::Error("{}: crashed in MTFEPlugin_Load (exception {:08X}); left loaded, its hooks may be half installed", File, Code);
                return;
            }
            Log::Warn("{}: MTFEPlugin_Load returned false, unloading", File);
            std::erase_if(Listeners, [&](const Listener& Item)
                          { return Item.Handle == Handle; });
            Plugins[Handle].Info.name = "";
            Plugins[Handle].Version = nullptr;
            FreeLibrary(Module);
        }

        std::vector<std::filesystem::path> DllsIn(const std::filesystem::path& Folder)
        {
            std::vector<std::filesystem::path> Result;
            std::error_code ErrorCode;
            for (const auto& Entry : std::filesystem::directory_iterator(Folder, ErrorCode))
            {
                if (Entry.is_regular_file() && _wcsicmp(Entry.path().extension().c_str(), L".dll") == 0)
                {
                    Result.push_back(Entry.path());
                }
            }
            std::sort(Result.begin(), Result.end());
            return Result;
        }
    }

    void SetPaths(std::string GamePath, std::string MTFEPath)
    {
        ModMenu::SetPaths(MTFEPath);
        GameDir = std::move(GamePath);
        MTFEDir = std::move(MTFEPath);
        LoadInterface.runtimeVersion = Runtime::Get().Timestamp;
    }

    void LoadAll(bool Legacy)
    {
        const auto Folder = std::filesystem::path(MTFEDir) / "Plugins";
        const auto Dlls = DllsIn(Folder);
        Plugins.reserve(Dlls.size()); // GetPluginInfo hands out pointers into this vector
        Log::Info("plugins: {} DLL(s) in {}", Dlls.size(), Folder.string());
        for (const auto& Dll : Dlls)
        {
            LoadPlugin(Dll);
        }

        if (Legacy)
        {
            const auto LegacyFolder = std::filesystem::path(GameDir) / "plugins";
            for (const auto& Dll : DllsIn(LegacyFolder))
            {
                if (LoadLibraryW(Dll.c_str()))
                {
                    Log::Info("legacy plugin {}: loaded", Dll.filename().string());
                }
                else
                {
                    Log::Error("legacy plugin {}: LoadLibrary failed ({})", Dll.filename().string(), GetLastError());
                }
            }
        }
    }

    void Broadcast(std::uint32_t Type, void* Data, std::uint32_t Length)
    {
        Deliver(kSender, Type, Data, Length, nullptr);
    }

    const char* PluginName(MTFEPluginHandle Handle)
    {
        return NameOf(Handle);
    }

    void RunTasks()
    {
        std::vector<Task> Once;
        std::vector<Task> Every;
        {
            std::lock_guard Guard(TaskLock);
            Once.swap(Tasks);
            Every = FrameCallbacks;
        }
        for (const auto& Entry : Once)
        {
            Entry.Function(Entry.Context);
        }
        for (const auto& Entry : Every)
        {
            Entry.Function(Entry.Context);
        }
    }
}
