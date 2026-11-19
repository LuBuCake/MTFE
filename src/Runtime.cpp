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

#include "Runtime.h"

#include <MTFE/PluginAPI.h>

#include <Windows.h>

#include <format>

namespace Runtime
{
    namespace
    {
        constexpr Addresses kSteam2023{
            .OEP = 0x00E68A06,
            .WinMainCall = 0x00E68998,
            .WinMain = 0x00713F40,
            .SBioMainVtbl = 0x00F5F620,
            .FrameSlot = 6,
            .SResource = 0x01234580,
            .LooseRootOffset = 0x102C,
            .OtherRootOffset = 0x1028,
            .CreateFileIat = 0x00F4A250,
            .CreateFileCalls = { 0x00449129, 0x00447300 },
            .RArchiveVtbl = 0x010B6264,
            .ArchiveLoadSlot = 8,
            .ArchiveLoad = 0x00621A10,
            .GetResource = 0x00448B70,
            .DTIByHash = 0x006D0690,
            .ReleaseResource = 0x00447020,
            .Menu = {
                .SpecialVtbl = 0x00FA8CE0,
                .SpecialDraw = 0x00A388C0,
                .PageVtbl = 0x00FA8034,
                .PageDTI = 0x011F54D0,
                .PageInit = 0x00A25920,
                .CallSpecialCursor = 0x00A3BBEF,
                .CallSpecialMouseRow = 0x00A3BC01,
                .CallSpecialClick = 0x00A3BBB8,
                .CallSpecialTitle = 0x00A388DB,
                .CallSpecialHelp = 0x00A389DB,
                .CallCursorIdle = 0x007B5C34,
                .SpecialCursor = 0x009E4BD0,
                .SpecialMouseRow = 0x007B5870,
                .SpecialClick = 0x007B6420,
                .DrawTitle = 0x00A17D40,
                .DrawHelp = 0x00A17CF0,
                .DrawString = 0x00A17500,
                .DrawMessage = 0x00A17660,
                .RowColor = 0x00A17350,
                .RowScale = 0x00A173B0,
                .Highlight = 0x00A17E90,
                .Arrows = 0x00A176A0,
                .ShowElement = 0x009E4DA0,
                .ButtonHint = 0x00A18130,
                .Blink = 0x009E3710,
                .FadeInStart = 0x00A171C0,
                .FadeInStep = 0x00A171F0,
                .FadeOutStart = 0x00A17240,
                .FadeOutStep = 0x00A17280,
                .FadeLerp = 0x00A172E0,
                .PrepareFrame = 0x00A22C00,
                .ResetLayout = 0x009E3780,
                .SetOrigin = 0x009E3840,
                .ReleaseMouse = 0x00A22E70,
                .CancelPressed = 0x00A22F70,
                .TransitionBusy = 0x00A580B0,
                .PadTrg = 0x009E3420,
                .PadRep = 0x009E3480,
                .CursorMove = 0x009E35D0,
                .SndConfirm = 0x009E4A40,
                .SndCancel = 0x009E4A50,
                .SndCursor = 0x009E4AA0,
                .SndChange = 0x009E4AB0,
                .AddUnit = 0x00428B80,
                .MouseRow = 0x00A22FC0,
                .MouseClicked = 0x007B5750,
                .MouseClickedIdx = 0x007B5620,
                .MouseScroll = 0x007B59B0,
                .SetHitMap = 0x00A22DF0,
                .KeyDown = 0x00799FB0,
                .KeyName = 0x0079A6A0,
                .SUnit = 0x012340B0,
                .SMain = 0x012340A4,
                .STimer = 0x011B209C,
                .SId = 0x011B20C4,
                .SPad = 0x01249C40,
                .SMouse = 0x01249C78,
                .SCursor = 0x011B1FA4,
                .PageRowsMap = 0x8F,
                .PageArrowsMap = 0x90,
            },
        };

        Info Current;
    }

    const Info& Detect()
    {
        const auto Base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Base);
        const auto* Nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(Base + Dos->e_lfanew);
        const auto* Section = IMAGE_FIRST_SECTION(Nt);

        Current.Base = Base;
        Current.Timestamp = Nt->FileHeader.TimeDateStamp;
        Current.CodeBegin = Base + Section->VirtualAddress;
        Current.CodeEnd = Current.CodeBegin + Section->Misc.VirtualSize;

        switch (Current.Timestamp)
        {
        case MTFE_RUNTIME_RE5_STEAM_2023:
            Current.GameAddresses = Base == 0x400000 ? &kSteam2023 : nullptr;
            Current.Name = "Steam 2023 (Jan 23 2023)";
            break;
        default:
            Current.GameAddresses = nullptr;
            Current.Name = std::format("unknown ({:08X})", Current.Timestamp);
            break;
        }
        return Current;
    }

    const Info& Get()
    {
        return Current;
    }

    bool IsGameCode(const void* Address)
    {
        const auto Va = reinterpret_cast<std::uintptr_t>(Address);
        return Va >= Current.CodeBegin && Va < Current.CodeEnd;
    }
}
