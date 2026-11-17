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

#pragma once

#include <cstddef>
#include <cstdint>

namespace Hook
{
    // Detour on a Windows API entry point. Handles the two x86 shapes of system DLL exports:
    //   hot-patchable "mov edi,edi" with 5 bytes of padding before it, and "jmp dword ptr [imp]" thunks.
    // Returns a pointer that calls the original API, or nullptr when the entry point has neither shape.
    class ApiHook
    {
    public:
        void* Install(void* Target, void* Detour);
        void Remove();

        void* Original() const { return _original; }

    private:
        std::uint8_t* _target = nullptr;
        std::uint8_t _saved[8]{};
        std::uint32_t _savedLen = 0;
        std::uint32_t _savedOffset = 0;
        void* _original = nullptr;
    };

    void SafeWrite(std::uintptr_t Address, const void* Data, std::size_t Size);

    // call rel32 at `Address` -> `Target`; returns the previous call target. Chain-preserving.
    std::uintptr_t WriteCall5(std::uintptr_t Address, std::uintptr_t Target);

    // vtable[index] = target; returns the previous entry. Chain-preserving.
    std::uintptr_t WriteVFunc(std::uintptr_t Vtable, std::size_t Index, std::uintptr_t Target);
}
