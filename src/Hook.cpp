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

#include "Hook.h"

#include <Windows.h>

#include <cstring>

namespace Hook
{
    void SafeWrite(std::uintptr_t Address, const void* Data, std::size_t Size)
    {
        DWORD OldProtect = 0;
        VirtualProtect(reinterpret_cast<void*>(Address), Size, PAGE_EXECUTE_READWRITE, &OldProtect);
        std::memcpy(reinterpret_cast<void*>(Address), Data, Size);
        VirtualProtect(reinterpret_cast<void*>(Address), Size, OldProtect, &OldProtect);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(Address), Size);
    }

    std::uintptr_t WriteCall5(std::uintptr_t Address, std::uintptr_t Target)
    {
        const auto* Code = reinterpret_cast<const std::uint8_t*>(Address);
        if (Code[0] != 0xE8)
        {
            return 0;
        }
        std::int32_t Relative = 0;
        std::memcpy(&Relative, Code + 1, 4);
        const auto Previous = Address + 5 + Relative;

        std::uint8_t Patch[5] = { 0xE8 };
        const auto NewRelative = static_cast<std::int32_t>(Target - (Address + 5));
        std::memcpy(Patch + 1, &NewRelative, 4);
        SafeWrite(Address, Patch, sizeof(Patch));
        return Previous;
    }

    std::uintptr_t WriteVFunc(std::uintptr_t Vtable, std::size_t Index, std::uintptr_t Target)
    {
        const auto Slot = Vtable + Index * sizeof(std::uintptr_t);
        const auto Previous = *reinterpret_cast<std::uintptr_t*>(Slot);
        SafeWrite(Slot, &Target, sizeof(Target));
        return Previous;
    }

    void* ApiHook::Install(void* Target, void* Detour)
    {
        auto* Entry = static_cast<std::uint8_t*>(Target);
        if (!Entry)
        {
            return nullptr;
        }

        auto Padding = true;
        for (int Offset = 1; Offset <= 5; ++Offset)
        {
            Padding = Padding && (Entry[-Offset] == 0xCC || Entry[-Offset] == 0x90);
        }

        if (Entry[0] == 0x8B && Entry[1] == 0xFF && Padding)
        {
            _target = Entry;
            _savedOffset = 5;
            _savedLen = 7;
            std::memcpy(_saved, Entry - 5, 7);

            std::uint8_t Patch[7] = { 0xE9, 0, 0, 0, 0, 0xEB, 0xF9 };
            const auto Relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(Detour) - reinterpret_cast<std::uintptr_t>(Entry));
            std::memcpy(Patch + 1, &Relative, 4);
            _original = Entry + 2;
            SafeWrite(reinterpret_cast<std::uintptr_t>(Entry - 5), Patch, sizeof(Patch));
            return _original;
        }

        if (Entry[0] == 0xFF && Entry[1] == 0x25)
        {
            std::uint32_t Slot = 0;
            std::memcpy(&Slot, Entry + 2, 4);
            _target = Entry;
            _savedOffset = 0;
            _savedLen = 6;
            std::memcpy(_saved, Entry, 6);
            _original = *reinterpret_cast<void**>(Slot);

            std::uint8_t Patch[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
            const auto Relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(Detour) - (reinterpret_cast<std::uintptr_t>(Entry) + 5));
            std::memcpy(Patch + 1, &Relative, 4);
            SafeWrite(reinterpret_cast<std::uintptr_t>(Entry), Patch, sizeof(Patch));
            return _original;
        }

        return nullptr;
    }

    void ApiHook::Remove()
    {
        if (!_target)
        {
            return;
        }
        if (_savedOffset == 5)
        {
            // "mov edi,edi" back first (one aligned 2-byte write), then the padding: no thread can land on a half patch.
            SafeWrite(reinterpret_cast<std::uintptr_t>(_target), _saved + 5, 2);
            SafeWrite(reinterpret_cast<std::uintptr_t>(_target - 5), _saved, 5);
        }
        else
        {
            SafeWrite(reinterpret_cast<std::uintptr_t>(_target), _saved, _savedLen);
        }
        _target = nullptr;
    }
}
