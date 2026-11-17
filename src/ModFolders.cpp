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

#include "ModFolders.h"

#include "Hook.h"
#include "Log.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// MT Framework resources (RE5 Steam 2023, found in Ghidra):
//   sResource::GetResource(dti, path, flags)  cache lookup by (type, path); on a miss, flags & 1 loads the file
//                                             <root>\<path>.<ext> synchronously (CreateFileA, a failure is fatal) and
//                                             registers it in the cache. Root = nativePC_MT for resources with flag
//                                             0x10 (set by each class constructor), <game>\resource otherwise.
//   rArchive vfunc 8 (load from stream)       reads the ARC table and queues every entry: each one gets the cached
//                                             resource of that (type, path) or a new one, and the worker threads skip
//                                             every queued resource that is already loaded (state & 1).
// So: the resource loaders' CreateFileA opens the mod's file instead of the game's (this alone replaces whole .arc
// files and loose files), and before each archive's own load every entry that a mod ships as a loose file is loaded
// through GetResource: the archive then keeps a reference to the mod's version and never decompresses its own copy.
// The extra reference is released after the archive load.

namespace ModFolders
{
    namespace
    {
        const Runtime::Addresses* GameAddresses = nullptr;
        Settings ModSettings;
        std::uintptr_t ArchiveLoadOriginal = 0;

        struct ModFile
        {
            std::string Path; // full path of the mod's file
            std::string Mod;  // mod folder name ("" = loose in Mods\ itself)
        };

        std::mutex ExtLock; // ExtByType
        std::once_flag ScanOnce;
        std::string ModsRoot;                           // <game>\MTFE\Mods
        std::unordered_map<std::string, ModFile> Files; // "pawn\pl\pl00\model\pl0000.mod" (lowercase) -> mod file
        std::unordered_map<std::uint32_t, std::string> ExtByType;

        std::string Lower(std::string Text)
        {
            for (auto& Char : Text)
            {
                Char = Char == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(Char)));
            }
            return Text;
        }

        const char* MtString(std::uint32_t Offset)
        {
            auto* Resource = *reinterpret_cast<std::uint8_t**>(GameAddresses->SResource);
            const auto* Chars = Resource ? *reinterpret_cast<const char* const*>(Resource + Offset) : nullptr;
            return Chars ? Chars + 8 : nullptr; // MtString: refcount, length, chars
        }

        std::vector<std::string> Split(const std::string& Relative)
        {
            std::vector<std::string> Parts;
            std::size_t From = 0;
            for (std::size_t At; (At = Relative.find('\\', From)) != std::string::npos; From = At + 1)
            {
                if (At > From)
                {
                    Parts.push_back(Relative.substr(From, At - From));
                }
            }
            Parts.push_back(Relative.substr(From));
            return Parts;
        }

        // Every file below a folder, as a lowercase path relative to it (std::filesystem::relative is slow).
        template <class F>
        void Walk(const std::string& Dir, F&& Visit)
        {
            std::error_code ErrorCode;
            for (auto It = std::filesystem::recursive_directory_iterator(Dir, ErrorCode); !ErrorCode && It != std::filesystem::recursive_directory_iterator(); It.increment(ErrorCode))
            {
                if (It->is_regular_file(ErrorCode))
                {
                    const auto Full = It->path().string();
                    Visit(Lower(Full.substr(Dir.size() + 1)), Full);
                }
            }
        }

        struct Placer
        {
            std::unordered_set<std::string> Roots;                  // game root folders (lowercase)
            std::unordered_set<std::string> RootFiles;              // files directly in nativePC_MT
            std::unordered_map<std::string, std::string> ArcByName; // "uom0845.arc" -> "image\archive\uom0845.arc"

            explicit Placer(const std::string& Native)
            {
                Roots = { "pawn", "stage", "effect", "sound", "event", "id", "etc", "system", "movie", "image", "dlc" };
                std::error_code ErrorCode;
                for (const auto& Entry : std::filesystem::directory_iterator(Native, ErrorCode))
                {
                    const auto Name = Lower(Entry.path().filename().string());
                    (Entry.is_directory(ErrorCode) ? Roots : RootFiles).insert(Name);
                }
                for (const char* Sub : { "Image", "system" })
                {
                    Walk(Native + "\\" + Sub, [&](const std::string& Relative, const std::string&)
                         {
                             if (Relative.ends_with(".arc"))
                             {
                                 ArcByName.emplace(Relative.substr(Relative.find_last_of('\\') + 1), Lower(Sub) + "\\" + Relative);
                             } });
                }
            }

            // Where a file of a mod goes, relative to nativePC_MT; "" = cannot tell.
            std::string Place(const std::string& RelInMod) const
            {
                const auto Parts = Split(RelInMod);
                for (std::size_t Index = 0; Index + 1 < Parts.size(); ++Index)
                {
                    if (Roots.contains(Parts[Index]))
                    {
                        std::string Relative = Parts[Index];
                        for (std::size_t Part = Index + 1; Part < Parts.size(); ++Part)
                        {
                            Relative += "\\" + Parts[Part];
                        }
                        return Relative;
                    }
                }
                const auto& Name = Parts.back();
                if (const auto Arc = ArcByName.find(Name); Arc != ArcByName.end())
                {
                    return Arc->second;
                }
                return RootFiles.contains(Name) ? Name : std::string{};
            }
        };

        void Scan()
        {
            ModsRoot = ModSettings.MTFEPath + "Mods";
            std::error_code ErrorCode;
            std::filesystem::create_directories(ModsRoot, ErrorCode);
            const char* Native = MtString(GameAddresses->LooseRootOffset);
            if (!Native)
            {
                Log::Error("mod folders: sResource has no root yet - mods disabled");
                return;
            }
            const Placer Placement(Native);

            // Order of priority: files loose in Mods\ first, then mod folders A..Z (the last one wins a conflict).
            std::vector<std::string> Mods{ "" };
            for (const auto& Entry : std::filesystem::directory_iterator(ModsRoot, ErrorCode))
            {
                const auto Name = Entry.path().filename().string();
                if (Entry.is_directory(ErrorCode) && !Name.starts_with("-"))
                {
                    Mods.push_back(Name);
                }
            }
            std::sort(Mods.begin() + 1, Mods.end(), [](const auto& Left, const auto& Right)
                      { return Lower(Left) < Lower(Right); });

            std::size_t Unplaced = 0, Active = 0;
            for (const auto& Mod : Mods)
            {
                const auto Label = Mod.empty() ? std::string("(Mods folder)") : Mod;
                std::size_t Count = 0;
                const auto Add = [&](const std::string& RelInMod, const std::string& Full)
                {
                    const auto Relative = Placement.Place(RelInMod);
                    if (Relative.empty())
                    {
                        ++Unplaced;
                        Log::Warn("mod '{}': {} is not a game file (no game folder in its path, not a known .arc) - ignored", Label, RelInMod);
                        return;
                    }
                    auto& Slot = Files[Relative];
                    if (!Slot.Path.empty())
                    {
                        Log::Info("mod '{}' replaces {} of mod '{}'", Label, Relative, Slot.Mod.empty() ? "(Mods folder)" : Slot.Mod);
                    }
                    Slot = { Full, Mod };
                    ++Count;
                };
                if (Mod.empty())
                {
                    for (const auto& Entry : std::filesystem::directory_iterator(ModsRoot, ErrorCode))
                    {
                        if (Entry.is_regular_file(ErrorCode))
                        {
                            Add(Lower(Entry.path().filename().string()), Entry.path().string());
                        }
                    }
                }
                else
                {
                    Walk(ModsRoot + "\\" + Mod, Add);
                }
                if (Count)
                {
                    ++Active;
                    Log::Info("mod '{}': {} file(s)", Label, Count);
                }
            }
            Log::Info("mod folders: {} mod(s), {} game file(s) replaced, {} ignored ({} archive names known)", Active, Files.size(), Unplaced,
                      Placement.ArcByName.size());
        }

        bool Ready()
        {
            std::call_once(ScanOnce, Scan);
            return !Files.empty();
        }

        // Extension of a resource type: slot 6 of its vtable (a constant string); the vtable comes from a temporary
        // instance (DTI slot 1 = NewInstance) destroyed right away, as sResource does when a load fails.
        std::string ExtOf(std::uint32_t TypeHash, void* DTI)
        {
            std::lock_guard Guard(ExtLock);
            if (const auto It = ExtByType.find(TypeHash); It != ExtByType.end())
            {
                return It->second;
            }
            auto** DTIVtbl = *reinterpret_cast<void***>(DTI);
            auto* Object = reinterpret_cast<void*(__thiscall*)(void*)>(DTIVtbl[1])(DTI);
            std::string Ext;
            if (Object)
            {
                auto** Vtbl = *reinterpret_cast<void***>(Object);
                if (const auto* ExtName = reinterpret_cast<const char*(__thiscall*)(void*)>(Vtbl[6])(Object))
                {
                    Ext = Lower(ExtName);
                }
                reinterpret_cast<void(__thiscall*)(void*, int)>(Vtbl[0])(Object, 1);
            }
            return ExtByType.emplace(TypeHash, std::move(Ext)).first->second;
        }

        void Release(void* Resource)
        {
            auto* Manager = *reinterpret_cast<void**>(GameAddresses->SResource);
            const auto Function = GameAddresses->ReleaseResource;
            __asm {
                mov eax, Manager
                mov edi, Resource
                call Function
            }
        }

#pragma pack(push, 1)
        struct ArcHeader
        {
            char Magic[4]; // "ARC\0"
            std::uint16_t Version;
            std::uint16_t Count;
        };
        struct ArcEntry
        {
            char Name[0x40];
            std::uint32_t Type; // MtDTI id of the resource class
            std::uint32_t CompressedSize;
            std::uint32_t SizeAndQuality;
            std::uint32_t Offset;
        };
#pragma pack(pop)
        static_assert(sizeof(ArcEntry) == 0x50);

        bool ReadTable(const std::string& File, std::vector<ArcEntry>& Out)
        {
            const HANDLE Handle = CreateFileA(File.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (Handle == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            ArcHeader Header{};
            DWORD BytesRead = 0;
            bool Ok = ReadFile(Handle, &Header, sizeof(Header), &BytesRead, nullptr) && BytesRead == sizeof(Header) && std::memcmp(Header.Magic, "ARC\0", 4) == 0 &&
                      Header.Version == 7;
            if (Ok)
            {
                Out.resize(Header.Count);
                const DWORD Bytes = static_cast<DWORD>(Out.size() * sizeof(ArcEntry));
                Ok = ReadFile(Handle, Out.data(), Bytes, &BytesRead, nullptr) && BytesRead == Bytes;
            }
            CloseHandle(Handle);
            return Ok;
        }

        struct Preload
        {
            std::string Name;
            void* DTI;
        };

        std::vector<Preload> FindOverrides(const char* ArchivePath)
        {
            std::vector<Preload> Found;
            const char* NativeRoot = MtString(GameAddresses->LooseRootOffset);
            if (!Ready() || !NativeRoot)
            {
                return Found;
            }
            std::vector<ArcEntry> Entries;
            const auto Arc = Files.find(Lower(ArchivePath) + ".arc"); // a mod may replace the archive itself
            if (!ReadTable(Arc != Files.end() ? Arc->second.Path : std::string(NativeRoot) + "\\" + ArchivePath + ".arc", Entries))
            {
                return Found;
            }
            for (auto& Entry : Entries)
            {
                Entry.Name[sizeof(Entry.Name) - 1] = '\0';
                void* DTI = reinterpret_cast<void*(__cdecl*)(std::uint32_t)>(GameAddresses->DTIByHash)(Entry.Type);
                if (!DTI)
                {
                    continue;
                }
                const auto Ext = ExtOf(Entry.Type, DTI);
                if (!Ext.empty() && Files.contains(Lower(Entry.Name) + "." + Ext))
                {
                    Found.push_back({ Entry.Name, DTI });
                }
            }
            return Found;
        }

        bool __fastcall ArchiveLoad(void* Archive, void*, void* Stream)
        {
            std::vector<void*> Held;
            const char* ArchivePath = reinterpret_cast<const char*>(Archive) + 4; // cResource: path at +4
            for (const auto& Override : FindOverrides(ArchivePath))
            {
                auto* Manager = *reinterpret_cast<void**>(GameAddresses->SResource);
                auto* Resource = reinterpret_cast<void*(__thiscall*)(void*, void*, const char*, std::uint32_t)>(GameAddresses->GetResource)(Manager, Override.DTI, Override.Name.c_str(), 1);
                if (Resource)
                {
                    Held.push_back(Resource);
                }
                if (ModSettings.LogEachFile || !Resource)
                {
                    Log::Info("mod file {} {}: {}", Resource ? "wins over" : "FAILED, keeping", ArchivePath, Override.Name);
                }
            }
            const bool Ok = reinterpret_cast<bool(__thiscall*)(void*, void*)>(ArchiveLoadOriginal)(Archive, Stream);
            for (auto* Resource : Held)
            {
                Release(Resource);
            }
            return Ok;
        }

        // "<Root>\<Relative>" -> Relative (lowercase) when Path is under Root.
        bool RelativeTo(const char* Path, const char* Root, std::string& Relative)
        {
            if (!Root)
            {
                return false;
            }
            const auto Length = std::strlen(Root);
            if (_strnicmp(Path, Root, Length) != 0 || Path[Length] != '\\')
            {
                return false;
            }
            Relative = Lower(Path + Length + 1);
            return true;
        }

        using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

        // The resource loaders' CreateFileA: a read open of a file that a mod replaces goes to the mod's file, whichever
        // root the game built the path with (nativePC_MT or <game>\resource).
        HANDLE WINAPI CreateFileHook(LPCSTR Path, DWORD Access, DWORD Share, LPSECURITY_ATTRIBUTES Security, DWORD Disposition, DWORD Flags, HANDLE Template)
        {
            std::string Target;
            if (Path && Disposition == OPEN_EXISTING && !(Access & GENERIC_WRITE) && Ready())
            {
                std::string Relative;
                if ((RelativeTo(Path, MtString(GameAddresses->LooseRootOffset), Relative) || RelativeTo(Path, MtString(GameAddresses->OtherRootOffset), Relative)) &&
                    Files.contains(Relative))
                {
                    Target = Files.at(Relative).Path;
                }
            }
            const auto Real = *reinterpret_cast<CreateFileA_t*>(GameAddresses->CreateFileIat); // read every call: keeps IAT hooks
            return Real(Target.empty() ? Path : Target.c_str(), Access, Share, Security, Disposition, Flags, Template);
        }

        bool PatchIatCall(std::uintptr_t At)
        {
            const auto* Code = reinterpret_cast<const std::uint8_t*>(At);
            std::uint32_t Slot = 0;
            std::memcpy(&Slot, Code + 2, 4);
            if (Code[0] != 0xFF || Code[1] != 0x15 || Slot != GameAddresses->CreateFileIat)
            {
                Log::Warn("mod folders: {:08X} is not call [CreateFileA] - not redirected there", At);
                return false;
            }
            std::uint8_t Patch[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
            const auto Relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&CreateFileHook) - (At + 5));
            std::memcpy(Patch + 1, &Relative, 4);
            Hook::SafeWrite(At, Patch, sizeof(Patch));
            return true;
        }
    }

    void Install(const Runtime::Addresses& Game, const Settings& Config)
    {
        GameAddresses = &Game;
        ModSettings = Config;
        const auto Slot = reinterpret_cast<const std::uintptr_t*>(Game.RArchiveVtbl)[Game.ArchiveLoadSlot];
        if (Slot != Game.ArchiveLoad)
        {
            Log::Warn("mod folders: rArchive load is {:08X}, expected {:08X} (another hook?) - chaining anyway", Slot, Game.ArchiveLoad);
        }
        ArchiveLoadOriginal = Hook::WriteVFunc(Game.RArchiveVtbl, Game.ArchiveLoadSlot, reinterpret_cast<std::uintptr_t>(&ArchiveLoad));
        const bool Sync = PatchIatCall(Game.CreateFileCalls[0]);
        const bool Async = PatchIatCall(Game.CreateFileCalls[1]);
        Log::Info("mod folders: on (rArchive load {:08X}, file redirect sync {} async {})", ArchiveLoadOriginal, Sync, Async);
    }
}
