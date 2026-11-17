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

#include <cstdint>
#include <string>

namespace Runtime
{
    // Options menu (src/ModMenuUI.cpp; see CommonLibRE5/docs/MENU.md). All helpers take the page as `this`.
    struct MenuAddresses
    {
        // category list (uIdOption00) and the page we clone (uIdOption05, KEYBOARD/MOUSE)
        std::uint32_t ListVtbl, PageVtbl, PageDTI, PageInit;
        std::uint32_t CallCursorMove, CallDrawTitle, CallDrawHelp, CallMouseRow, CallMouseClick; // inside the list
        std::uint32_t CallCursorIdle;                                                            // sId update: call sCursor::SetLook(0)
        // helpers
        std::uint32_t DrawString, DrawMessage, RowColor, RowScale, Highlight, Arrows, ShowElement, ButtonHint, Blink;
        std::uint32_t FadeInStart, FadeInStep, FadeOutStart, FadeOutStep, FadeLerp;
        std::uint32_t PrepareFrame, ResetLayout, SetOrigin, ReleaseMouse, CancelPressed, TransitionBusy;
        std::uint32_t PadTrg, PadRep, CursorMove, SndConfirm, SndCancel, SndCursor, SndChange, AddUnit;
        std::uint32_t MouseRow, MouseClicked, MouseClickedIdx, MouseScroll, SetHitMap, KeyDown, KeyName;
        // globals
        std::uint32_t SUnit, SMain, STimer, SId, SPad, SMouse, SCursor;
        std::uint32_t PageRowsMap, PageArrowsMap; // sId hit maps of the KEYBOARD/MOUSE page
    };

    // Game addresses the loader needs, per runtime (VA; the exe has no ASLR, base 0x400000).
    struct Addresses
    {
        std::uint32_t OEP;         // entry: call __security_init_cookie; jmp __tmainCRTStartup
        std::uint32_t WinMainCall; // __tmainCRTStartup: call WinMain
        std::uint32_t WinMain;
        std::uint32_t SBioMainVtbl; // sBioMain::vftable (the game's sMain)
        std::uint32_t FrameSlot;    // sMain vfunc called once per frame by the main loop

        // Resources (loose files over ARC, src/LooseFiles.cpp)
        std::uint32_t SResource;          // sResource* global
        std::uint32_t LooseRootOffset;    // sResource: MtString* of "<game>\nativePC_MT" (resources with flag 0x10)
        std::uint32_t OtherRootOffset;    // sResource: MtString* of "<game>\resource" (resources without it)
        std::uint32_t CreateFileIat;      // IAT slot of CreateFileA
        std::uint32_t CreateFileCalls[2]; // "call [CreateFileA]" (6 bytes) in the sync and the async resource loaders
        std::uint32_t RArchiveVtbl;       // rArchive::vftable
        std::uint32_t ArchiveLoadSlot;    // rArchive vfunc "load from stream" (bool __thiscall(archive, MtStream*))
        std::uint32_t ArchiveLoad;        // its expected target (checked before hooking)
        std::uint32_t GetResource;        // sResource::GetResource: cResource* __thiscall(sResource, MtDTI*, const char*, flags)
        std::uint32_t DTIByHash;          // MtDTI* __cdecl(uint32 hash)
        std::uint32_t ReleaseResource;    // refcount-- (EAX = sResource, EDI = resource)

        MenuAddresses Menu;
    };

    struct Info
    {
        std::uintptr_t Base = 0;
        std::uint32_t Timestamp = 0;
        std::uintptr_t CodeBegin = 0; // first section (the game's .text)
        std::uintptr_t CodeEnd = 0;
        const Addresses* GameAddresses = nullptr; // nullptr = unknown runtime (plugins still load, no game hooks)
        std::string Name;
    };

    const Info& Detect();
    const Info& Get();

    bool IsGameCode(const void* Address);
}
