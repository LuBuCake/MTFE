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

// Extras > Special Settings > MODS: the row added to the Special Settings list (uIdStartUp0c, next to COSTUME SELECT /
// FILTER SELECT / INFINITE AMMO) and our own page (a uIdOption05 instance, KEYBOARD/MOUSE layout top31, with a
// cloned vtable whose init/move are ours). Until 2026-11-19 the row was in Options, where a 9th row went past the
// list's background. Everything is drawn with the menu's own
// helpers, so it looks and behaves like the game's pages (keyboard, pad, mouse, green pointer).
// Reverse engineering and every address: CommonLibRE5/docs/MENU.md.

#include "Hook.h"
#include "Log.h"
#include "ModMenu.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ModMenu
{
    namespace
    {
        const Runtime::MenuAddresses* M = nullptr;

        template <class T>
        T& At(void* Object, std::uintptr_t Offset)
        {
            return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(Object) + Offset);
        }

        template <class T>
        T* Global(std::uint32_t Address)
        {
            return *reinterpret_cast<T**>(Address);
        }

        template <class R, class... A>
        R This(std::uint32_t Function, void* Self, A... Args)
        {
            return reinterpret_cast<R(__thiscall*)(void*, A...)>(Function)(Self, Args...);
        }

        template <class R, class... A>
        R Std(std::uint32_t Function, A... Args)
        {
            return reinterpret_cast<R(__stdcall*)(A...)>(Function)(Args...);
        }

        constexpr std::uint32_t kRight = 0x20020, kLeft = 0x80080; // pad repeat masks of the option pages
        constexpr int kVisible = 7;                                // rows of the top31 layout
        constexpr int kRowsY[kVisible] = { 150, 200, 250, 300, 350, 400, 450 };
        constexpr int kLabelX = 0x17C, kValueX = 0x33E, kCenterX = 0x280, kHelpY = 0x20D;
        constexpr std::uint32_t kAnyMsg = 0x8CD; // a message id, only used to size the row highlight

        // ---- small wrappers over the game helpers --------------------------------------------------------------
        void DrawText(void* Unit, const std::string& Text, int X, int Y, std::uint32_t Color, float Scale)
        {
            This<void>(M->DrawString, Unit, Text.c_str(), X, Y, &Color, Scale);
        }
        void DrawMsg(void* Unit, std::uint32_t Message, int X, int Y, std::uint32_t Color, float Scale)
        {
            This<void>(M->DrawMessage, Unit, Message, X, Y, &Color, Scale);
        }
        std::uint32_t RowColor(void* Unit, bool Selected)
        {
            std::uint32_t Temp = 0;
            return *This<std::uint32_t*>(M->RowColor, Unit, &Temp, Selected, std::uint32_t{ 0xFF }, 0);
        }
        float RowScale(void* Unit, bool Selected)
        {
            return This<float>(M->RowScale, Unit, Selected);
        }
        void Show(void* Unit, const char* Name, std::uint32_t Alpha)
        {
            This<void>(M->ShowElement, Unit, Name, Alpha);
        }
        std::uint32_t White(void* Unit)
        {
            return (static_cast<std::uint32_t>(At<int>(Unit, 0x78) * 0xFF / 9) << 24) | 0xFFFFFF;
        }
        void Sound(std::uint32_t Function, void* Unit)
        {
            This<void>(Function, Unit);
        }
        // The mouse belongs to the keyboard player (sPad+0x614). A screen of the title menus has no owner (+0x34 < 0:
        // Special Settings reads the pads as (-1, -1), any pad), and so has our page opened from it: mouse too then.
        bool KeyboardPlayer(void* Unit)
        {
            const int Owner = At<int>(Unit, 0x34);
            return Owner < 0 || Owner == At<int>(Global<void>(M->SPad), 0x614);
        }
        // The game's mouse helpers of a page (hit maps 0xA22DF0, row under the pointer 0xA22FC0, release 0xA22E70)
        // only run when +0x34 IS the keyboard player. A page without owner (-1, any pad) lends it to them while they
        // run; the pads keep reading -1.
        struct MouseOwner
        {
            void* Unit;
            int Saved;
            explicit MouseOwner(void* Target) : Unit(Target), Saved(At<int>(Target, 0x34))
            {
                if (Saved < 0)
                {
                    At<int>(Unit, 0x34) = At<int>(Global<void>(M->SPad), 0x614);
                }
            }
            ~MouseOwner() { At<int>(Unit, 0x34) = Saved; }
            MouseOwner(const MouseOwner&) = delete;
            MouseOwner& operator=(const MouseOwner&) = delete;
        };

        // ---- our page ------------------------------------------------------------------------------------------
        std::array<std::uintptr_t, 16> PageVtbl{}; // [0] = RTTI locator, [1..15] = slots

        struct Ui
        {
            Page* Current = nullptr; // nullptr = the list of mods
            int ListCursor = 0, ListScroll = 0;
            int Cursor = 0, Scroll = 0;
            bool Capturing = false, CaptureArmed = false;
        } UiState;

        int RowCount(const std::vector<Page*>& AllPages)
        {
            return UiState.Current ? static_cast<int>(UiState.Current->Controls.size()) : static_cast<int>(AllPages.size());
        }
        bool Selectable(const std::vector<Page*>& AllPages, int Index)
        {
            return UiState.Current ? UiState.Current->Controls[Index]->Selectable() : Index < static_cast<int>(AllPages.size());
        }
        int FirstSelectable(const std::vector<Page*>& AllPages)
        {
            for (int Index = 0; Index < RowCount(AllPages); ++Index)
            {
                if (Selectable(AllPages, Index))
                {
                    return Index;
                }
            }
            return 0;
        }
        void KeepVisible()
        {
            if (UiState.Cursor < UiState.Scroll)
            {
                UiState.Scroll = UiState.Cursor;
            }
            if (UiState.Cursor >= UiState.Scroll + kVisible)
            {
                UiState.Scroll = UiState.Cursor - kVisible + 1;
            }
        }

        void OpenPage(void* Unit, Page* Target, const std::vector<Page*>& AllPages)
        {
            UiState.ListCursor = UiState.Cursor;
            UiState.ListScroll = UiState.Scroll;
            UiState.Current = Target;
            UiState.Cursor = FirstSelectable(AllPages);
            UiState.Scroll = 0;
            KeepVisible();
            Sound(M->SndConfirm, Unit);
        }

        void BackToList(void* Unit)
        {
            UiState.Current = nullptr;
            UiState.Cursor = UiState.ListCursor;
            UiState.Scroll = UiState.ListScroll;
            Sound(M->SndCancel, Unit);
        }

        // Value change by one step (Direction = -1 / +1), for TOGGLE, LIST and NUMBER.
        bool Step(Control& Item, int Direction)
        {
            const auto Before = Item.Value.load();
            switch (Item.Type)
            {
            case MTFE_MENU_TOGGLE:
                SetValue(Item, Direction > 0 ? 1 : 0);
                break;
            case MTFE_MENU_LIST:
            case MTFE_MENU_NUMBER:
                SetValue(Item, Before + Direction * (Item.Type == MTFE_MENU_NUMBER ? Item.Step : 1));
                break;
            default:
                break;
            }
            return Item.Value.load() != Before;
        }

        bool CanStep(const Control& Item, int Direction)
        {
            const auto Value = Item.Value.load();
            switch (Item.Type)
            {
            case MTFE_MENU_TOGGLE:
                return Direction > 0 ? Value == 0 : Value != 0;
            case MTFE_MENU_LIST:
                return Direction > 0 ? Value < static_cast<int>(Item.Items.size()) - 1 : Value > 0;
            case MTFE_MENU_NUMBER:
                return Direction > 0 ? Value < Item.MaxValue : Value > Item.MinValue;
            default:
                return false;
            }
        }

        void Capture(void* Unit, Control& Item, bool Cancel)
        {
            if (Cancel)
            {
                UiState.Capturing = false;
                Sound(M->SndCancel, Unit);
                return;
            }
            auto* Pad = Global<void>(M->SPad);
            bool AnyDown = false;
            for (int Code = 1; Code <= 0x66; ++Code)
            {
                if (This<bool>(M->KeyDown, Pad, Code))
                {
                    AnyDown = true;
                    if (UiState.CaptureArmed)
                    {
                        SetValue(Item, Code);
                        UiState.Capturing = false;
                        Sound(M->SndChange, Unit);
                        return;
                    }
                }
            }
            if (!AnyDown)
            {
                UiState.CaptureArmed = true; // the key that started the capture was released
            }
        }

        void Input(void* Unit, std::uint32_t Trigger, std::uint32_t Repeat)
        {
            auto& State = At<std::uint8_t>(Unit, 0x14);
            const auto AllPages = Pages();
            auto* Sid = Global<void>(M->SId);
            const bool Mouse = KeyboardPlayer(Unit);

            // hit maps of the KEYBOARD/MOUSE page: rows (0..6 + footer buttons), the two arrow columns, the wheel
            {
                MouseOwner Lend(Unit);
                This<void>(M->SetHitMap, Unit, 5u, M->PageRowsMap, 0u);
                This<void>(M->SetHitMap, Unit, 5u, M->PageArrowsMap, 1u);
                This<void>(M->SetHitMap, Unit, 5u, 0u, 2u);
            }

            const int Count = RowCount(AllPages);
            if (Mouse)
            {
                for (int Index = 0; Index < kVisible && UiState.Scroll + Index < Count; ++Index)
                {
                    if (Selectable(AllPages, UiState.Scroll + Index))
                    {
                        At<std::uint8_t>(Sid, 0x1E0 + Index) = 1; // clickable rows (green pointer), layer 0
                    }
                }
            }

            const bool Cancel = This<bool>(M->CancelPressed, Unit);
            if (UiState.Current && UiState.Capturing)
            {
                Capture(Unit, *UiState.Current->Controls[UiState.Cursor], Cancel);
                return;
            }
            if (Cancel)
            {
                {
                    MouseOwner Lend(Unit);
                    This<void>(M->ReleaseMouse, Unit);
                }
                if (UiState.Current)
                {
                    BackToList(Unit);
                    return;
                }
                Sound(M->SndCancel, Unit);
                State = 3;
                At<std::uint8_t>(Unit, 0x3E) = 1;
                At<float>(Unit, 0x44) = 0.0f;
                return;
            }
            if (!Count)
            {
                return;
            }

            // cursor: keys/pad (wrapping, skipping headers), wheel, pointer
            auto Next = static_cast<int>(Std<std::uint32_t>(M->CursorMove, Trigger, Repeat, static_cast<std::uint32_t>(UiState.Cursor), Count));
            if (Next != UiState.Cursor)
            {
                const int Direction = Next == (UiState.Cursor + 1) % Count || Next > UiState.Cursor ? 1 : -1;
                for (int Guard = 0; Guard < Count && !Selectable(AllPages, Next); ++Guard)
                {
                    Next = (Next + Direction + Count) % Count;
                }
            }
            if (Mouse)
            {
                const int MaxScroll = Count > kVisible ? Count - kVisible : 0;
                const int Wheel = This<int>(M->MouseScroll, Sid, UiState.Scroll, 0, MaxScroll, 0, 1, 2);
                if (Wheel != UiState.Scroll)
                {
                    UiState.Scroll = Wheel;
                    Next = std::clamp(Next, UiState.Scroll, UiState.Scroll + kVisible - 1);
                }
                const int Visible = std::min(kVisible, Count - UiState.Scroll);
                MouseOwner Lend(Unit);
                const int Hovered = This<int>(M->MouseRow, Unit, Next - UiState.Scroll, Visible, 0, 0);
                if (Hovered >= 0 && Hovered < Visible && Selectable(AllPages, UiState.Scroll + Hovered))
                {
                    Next = UiState.Scroll + Hovered;
                }
            }
            if (Next != UiState.Cursor && Selectable(AllPages, Next))
            {
                UiState.Cursor = Next;
                KeepVisible();
                Sound(M->SndCursor, Unit);
                return;
            }
            if (!Selectable(AllPages, UiState.Cursor))
            {
                return;
            }

            const bool Confirm = (Trigger & At<std::uint32_t>(Global<void>(M->SPad), 0x5B4)) != 0 ||
                                 (Mouse && This<bool>(M->MouseClicked, Sid, 0, kVisible - 1, 0, 0));
            if (!UiState.Current)
            {
                if (Confirm)
                {
                    OpenPage(Unit, AllPages[UiState.Cursor], AllPages);
                }
                return;
            }

            auto& Item = *UiState.Current->Controls[UiState.Cursor];
            const bool ClickRight = Mouse && This<bool>(M->MouseClickedIdx, Sid, 1, 0, 1);
            const bool ClickLeft = Mouse && This<bool>(M->MouseClickedIdx, Sid, 0, 0, 1);
            switch (Item.Type)
            {
            case MTFE_MENU_BUTTON:
                if (Confirm)
                {
                    Sound(M->SndConfirm, Unit);
                    Press(Item);
                }
                return;
            case MTFE_MENU_HOTKEY:
                if (Confirm)
                {
                    UiState.Capturing = true;
                    UiState.CaptureArmed = false;
                    Sound(M->SndConfirm, Unit);
                }
                return;
            default:
                break;
            }
            bool Changed = false;
            if ((Repeat & kRight) || ClickRight)
            {
                Changed = Step(Item, +1);
            }
            else if ((Repeat & kLeft) || ClickLeft)
            {
                Changed = Step(Item, -1);
            }
            else if (Confirm && Item.Type == MTFE_MENU_TOGGLE)
            {
                Changed = Step(Item, Item.Value ? -1 : +1);
            }
            else if (Confirm && Item.Type == MTFE_MENU_LIST)
            {
                const auto Before = Item.Value.load();
                SetValue(Item, CanStep(Item, +1) ? Before + 1 : 0); // confirm cycles through the items
                Changed = Item.Value.load() != Before;
            }
            if (Changed)
            {
                Sound(M->SndChange, Unit);
            }
        }

        void Draw(void* Unit)
        {
            const auto AllPages = Pages();
            const auto Alpha = static_cast<std::uint32_t>(This<int>(M->FadeLerp, Unit, 0, 0xFF, 0));
            Show(Unit, "title_base", Alpha);
            Show(Unit, "title_base_02", Alpha);
            Show(Unit, "title_base_03", Alpha);
            DrawText(Unit, UiState.Current ? UiState.Current->Title : std::string("MODS"), kCenterX, 0x60, White(Unit), 1.25f);

            const int Count = RowCount(AllPages);
            if (!Count)
            {
                DrawText(Unit, "No mod settings installed", kCenterX, kRowsY[0], RowColor(Unit, false), RowScale(Unit, false));
                DrawText(Unit, "Plugins add their pages here through the MTFE menu API.", kCenterX, kHelpY, White(Unit), 1.0f);
                This<void>(M->ButtonHint, Unit, 2u, -1);
                return;
            }

            std::string Help;
            for (int Index = 0; Index < kVisible && UiState.Scroll + Index < Count; ++Index)
            {
                const int Row = UiState.Scroll + Index;
                const bool Selected = Row == UiState.Cursor;
                const auto Color = RowColor(Unit, Selected);
                const float Scale = RowScale(Unit, Selected);
                const int Y = kRowsY[Index];
                char Bar[16];
                std::snprintf(Bar, sizeof(Bar), "bar_%02d", Index + 1);

                if (!UiState.Current)
                {
                    // same columns as the game's rows: the highlight (sel_NN) starts at the label column
                    std::size_t SettingCount = 0;
                    for (const auto* Item : AllPages[Row]->Controls)
                    {
                        SettingCount += Item->Selectable() ? 1 : 0;
                    }
                    DrawText(Unit, AllPages[Row]->Title, kLabelX, Y, Color, Scale);
                    DrawText(Unit, std::to_string(SettingCount) + (SettingCount == 1 ? " setting" : " settings"), kValueX, Y, Color, Scale);
                    Show(Unit, Bar, Alpha);
                    This<void>(M->Highlight, Unit, Index + 1, kAnyMsg, Selected, -1);
                    if (Selected)
                    {
                        Help = "Open the settings of " + AllPages[Row]->Title + ".";
                    }
                    continue;
                }

                const auto& Item = *UiState.Current->Controls[Row];
                if (Item.Type == MTFE_MENU_HEADER)
                {
                    DrawText(Unit, Item.Label, kCenterX, Y, White(Unit), 1.0f);
                    continue;
                }
                DrawText(Unit, Item.Label, kLabelX, Y, Color, Scale);
                if (Item.Type == MTFE_MENU_HOTKEY)
                {
                    if (Selected && UiState.Capturing)
                    {
                        DrawText(Unit, "Press a key", kValueX, Y, Color, Scale);
                    }
                    else if (const auto Value = Item.Value.load(); Value > 0)
                    {
                        if (const auto Message = Std<std::uint32_t>(M->KeyName, Value))
                        {
                            DrawMsg(Unit, Message, kValueX, Y, Color, Scale);
                        }
                        else
                        {
                            DrawText(Unit, std::to_string(Value), kValueX, Y, Color, Scale);
                        }
                    }
                    else
                    {
                        DrawText(Unit, "-", kValueX, Y, Color, Scale);
                    }
                }
                else if (Item.Type != MTFE_MENU_BUTTON)
                {
                    DrawText(Unit, Item.ValueText(), kValueX, Y, Color, Scale);
                }
                Show(Unit, Bar, Alpha);
                This<void>(M->Highlight, Unit, Index + 1, kAnyMsg, Selected, -1);
                if (Selected)
                {
                    Help = UiState.Capturing ? std::string("Press the new key. Back cancels.") : Item.Help;
                    if (!UiState.Capturing && (Item.Type == MTFE_MENU_TOGGLE || Item.Type == MTFE_MENU_LIST || Item.Type == MTFE_MENU_NUMBER))
                    {
                        This<void>(M->Arrows, Unit, Index + 1, (CanStep(Item, -1) ? 1u : 0u) | (CanStep(Item, +1) ? 2u : 0u));
                    }
                }
            }
            if (UiState.Scroll > 0)
            {
                Show(Unit, "up", Alpha);
            }
            if (UiState.Scroll + kVisible < Count)
            {
                Show(Unit, "down", Alpha);
            }
            if (!Help.empty())
            {
                DrawText(Unit, Help, kCenterX, kHelpY, White(Unit), 1.0f);
            }
            This<void>(M->ButtonHint, Unit, 0u, -1);
        }

        void __fastcall PageInit(void* Unit)
        {
            This<void>(M->PageInit, Unit); // loads top31, resets cursor/scroll fields
            const auto AllPages = Pages();
            UiState = {};
            UiState.Cursor = FirstSelectable(AllPages);
        }

        void __fastcall PageMove(void* Unit)
        {
            auto Trigger = Std<std::uint32_t>(M->PadTrg, At<std::uint32_t>(Unit, 0x188), At<std::uint32_t>(Unit, 0x34));
            auto Repeat = Std<std::uint32_t>(M->PadRep, At<std::uint32_t>(Unit, 0x188), At<std::uint32_t>(Unit, 0x34));
            if (At<char>(Global<void>(M->SMain), 0x10428))
            {
                Trigger = Repeat = 0;
            }
            This<void>(M->PrepareFrame, Unit);
            This<void>(M->ResetLayout, Unit);
            At<int>(Unit, 300) = 0;
            This<void>(M->SetOrigin, Unit, At<int>(Unit, 0x2C), 0);

            auto& State = At<std::uint8_t>(Unit, 0x14);
            switch (State)
            {
            case 0:
                This<void>(M->FadeInStart, Unit, 0);
                ++State;
                break;
            case 1:
                if (This<bool>(M->FadeInStep, Unit, 0))
                {
                    ++State;
                }
                break;
            case 2:
                Input(Unit, Trigger, Repeat);
                break;
            case 3:
                At<float>(Unit, 0x44) -= At<float>(Global<void>(M->STimer), 0x28);
                if (At<float>(Unit, 0x44) <= 0.0f)
                {
                    State = 0x5A;
                }
                break;
            case 0x5A:
                This<void>(M->FadeOutStart, Unit, 0);
                ++State;
                break;
            case 0x5B:
                if (This<bool>(M->FadeOutStep, Unit, 0))
                {
                    ++State;
                }
                break;
            case 0x5C:
                At<bool>(Unit, 0x3C) = true;
                return;
            }
            Draw(Unit);
        }

        void* CreatePage()
        {
            auto** DTIVtbl = *reinterpret_cast<void***>(M->PageDTI);
            auto* Unit = reinterpret_cast<void*(__thiscall*)(void*)>(DTIVtbl[1])(reinterpret_cast<void*>(M->PageDTI)); // NewInstance
            if (Unit)
            {
                *reinterpret_cast<std::uintptr_t*>(Unit) = reinterpret_cast<std::uintptr_t>(&PageVtbl[1]);
            }
            return Unit;
        }

        // ---- the MODS row in Extras > Special Settings ------------------------------------------------------------
        // uIdStartUp0c (layout top10): 3 rows, COSTUME SELECT / FILTER SELECT / INFINITE AMMO, at y 250/300/350 (table
        // 0x1131ED4, highlights sel_02/04/06 at 0x1131EC8), cursor +0x48, unlocked rows +0x188 (3, or 7 once the save
        // unlocks infinite ammo), help at y 525. MODS is a 4th row at y 400, still inside the panel; confirming it fades
        // the screen out, opens our page and fades back in (the game's rows go to other screens instead, 0xA177E0).
        constexpr int kSpecialRows = 3;
        constexpr int kModsRow = 3;
        constexpr int kModsRowY = 400;
        constexpr int kModsSel = 6;           // highlight of row 2 (sel_06), moved to our row while it is selected
        constexpr int kSelRow = 2, kSelRowY = 350; // ...and moved back to row 2 (INFINITE AMMO) while that one is
        constexpr std::uint32_t kRowMsg = 0x4E1; // INFINITE AMMO: sizes the highlight
        // Our states of the screen (the game's switch has no such case: its Move only draws then).
        constexpr std::uint8_t kFadeOut = 0x70, kFadingOut = 0x71, kOpen = 0x72, kWait = 0x73, kFadingIn = 0x74;

        std::uintptr_t SpecialMoveOriginal = 0, SpecialCursorOriginal = 0, SpecialMouseRowOriginal = 0, SpecialClickOriginal = 0;
        std::uintptr_t SpecialTitleOriginal = 0, SpecialHelpOriginal = 0, CursorIdleOriginal = 0;
        void* ModsPage = nullptr;      // the page opened from Special Settings (the screen is too small to hold it)
        void* CurrentScreen = nullptr; // the Special Settings screen running its Move (the mouse hooks get sId as this)

        struct RowMouse
        {
            bool Over = false, Click = false, Green = false, PrevDown = false;
        } MouseState;

        // Special Settings' hit map has rows 0..2 only: MODS = row 2's rectangle moved down by (row 2 - row 1).
        void UpdateMouse(void* Screen)
        {
            auto* Pad = Global<void>(M->SPad);
            auto* Sid = Global<void>(M->SId);
            auto* Mouse = Global<void>(M->SMouse);
            static bool Logged = false;
            if (Pad && !Logged)
            {
                Logged = true; // diagnosis of 2026-11-19: the screen's owner against the keyboard player
                Log::Info("mod menu: Special Settings owner +0x34 = {}, keyboard player = {}", At<int>(Screen, 0x34), At<int>(Pad, 0x614));
            }
            if (!Pad || !Sid || !Mouse || !KeyboardPlayer(Screen))
            {
                return;
            }
            const int Cursor = At<int>(Sid, 0x31C);
            const bool Down = (At<std::uint32_t>(Mouse, Cursor * 0x70 + 0x40) & 1) != 0;
            const bool Pressed = Down && !MouseState.PrevDown;
            MouseState.PrevDown = Down;
            if (!At<bool>(Pad, 0x610) || At<std::uint8_t>(Pad, 0x5F0) == 1 || At<std::uint8_t>(Sid, 0x20) == 1 ||
                At<int>(Sid, 0x60) != At<int>(Sid, 0x48))
            {
                return;
            }
            const auto MapIndex = At<std::uint32_t>(Sid, 0x48);
            if (MapIndex >= At<std::uint32_t>(Sid, 0x30C))
            {
                return;
            }
            auto* Map = At<void**>(Sid, 0x318)[MapIndex];
            if (!Map || At<std::uint32_t>(Map, 0xC) < static_cast<std::uint32_t>(kSpecialRows))
            {
                return;
            }
            auto** Rects = At<void**>(Map, 0x18);
            const auto* R1 = reinterpret_cast<const int*>(reinterpret_cast<std::uintptr_t>(Rects[kSpecialRows - 2]) + 4);
            const auto* R2 = reinterpret_cast<const int*>(reinterpret_cast<std::uintptr_t>(Rects[kSpecialRows - 1]) + 4);
            const int Dy = R2[1] - R1[1];
            const int X0 = R2[0] + At<int>(Sid, 0x50), X1 = R2[2] + At<int>(Sid, 0x50);
            const int Y0 = R2[1] + Dy + At<int>(Sid, 0x54), Y1 = R2[3] + Dy + At<int>(Sid, 0x54);
            const int Px = At<int>(Sid, 0x2E0), Py = At<int>(Sid, 0x2E4); // pointer in menu space (sId, this frame)
            MouseState.Over = Px >= X0 && Px < X1 && Py >= Y0 && Py < Y1;
            MouseState.Click = MouseState.Over && Pressed;
            MouseState.Green = MouseState.Over; // consumed by the next sId update (it runs after the menu units)
        }

        // MODS confirmed: our page, as the game opens its option pages (0xA27AB0), around a fade of the screen.
        bool RunModsStates(void* Screen)
        {
            auto& State = At<std::uint8_t>(Screen, 0x14);
            switch (State)
            {
            case 3: // the confirm blink (the game would now go to screen 0xB + row)
                At<float>(Screen, 0x44) -= At<float>(Global<void>(M->STimer), 0x28);
                if (At<float>(Screen, 0x44) <= 0.0f)
                {
                    At<std::uint8_t>(Screen, 0x3D) = 0;
                    State = kFadeOut;
                }
                return true;
            case kFadeOut:
                This<void>(M->FadeOutStart, Screen, 0);
                State = kFadingOut;
                return true;
            case kFadingOut:
                if (This<bool>(M->FadeOutStep, Screen, 0))
                {
                    State = kOpen;
                }
                return true;
            case kOpen:
                if (At<void*>(Screen, 0x128) && This<bool>(M->TransitionBusy, At<void*>(Screen, 0x128)))
                {
                    return true;
                }
                if ((ModsPage = CreatePage()) != nullptr)
                {
                    At<std::uint32_t>(ModsPage, 4) |= 0x2000;
                    At<std::uint32_t>(ModsPage, 0x34) = At<std::uint32_t>(Screen, 0x34);
                    At<void*>(ModsPage, 0x128) = At<void*>(Screen, 0x128);
                    This<void>(M->AddUnit, Global<void>(M->SUnit), 0x1Bu, ModsPage);
                }
                State = kWait;
                return true;
            case kWait:
                if (ModsPage && !At<bool>(ModsPage, 0x3C))
                {
                    return true;
                }
                if (ModsPage)
                {
                    At<std::uint32_t>(ModsPage, 4) &= ~0x2000u;
                    This<void>((*reinterpret_cast<std::uint32_t**>(ModsPage))[12], ModsPage); // kill
                    ModsPage = nullptr;
                }
                This<void>(M->FadeInStart, Screen, 0);
                State = kFadingIn;
                return true;
            case kFadingIn:
                if (This<bool>(M->FadeInStep, Screen, 0))
                {
                    State = 2;
                }
                return true;
            default:
                return false;
            }
        }

        void __fastcall SpecialMove(void* Screen)
        {
            MouseState.Over = MouseState.Click = MouseState.Green = false;
            CurrentScreen = Screen;
            const auto Before = At<std::uint8_t>(Screen, 0x14);
            const bool OnMods = At<int>(Screen, 0x48) == kModsRow;
            if ((Before == 3 && OnMods) || (Before >= kFadeOut && Before <= kFadingIn))
            {
                RunModsStates(Screen);
                This<void>(M->SpecialDraw, Screen);
                return;
            }
            if (Before == 2)
            {
                UpdateMouse(Screen);
            }
            reinterpret_cast<void(__thiscall*)(void*)>(SpecialMoveOriginal)(Screen);
            if (Before == 0)
            {
                At<std::uint32_t>(Screen, 0x188) |= 1u << kModsRow; // state 0 set the unlocked rows (3 / 7): MODS always is
            }
        }

        // cursor by keys/pad: one row more
        std::uint32_t __fastcall SpecialCursorHook(void* Screen, void*, std::uint32_t Cursor, int Count)
        {
            return reinterpret_cast<std::uint32_t(__thiscall*)(void*, std::uint32_t, int)>(SpecialCursorOriginal)(Screen, Cursor,
                                                                                                                   Count == kSpecialRows ? Count + 1 : Count);
        }

        // row under the mouse (this = sId): MODS when the pointer is over it
        int __fastcall SpecialMouseRowHook(void* Sid, void*, int Cursor, int Count, int Start, int Layer)
        {
            const int Row = reinterpret_cast<int(__thiscall*)(void*, int, int, int, int)>(SpecialMouseRowOriginal)(Sid, Cursor, Count, Start, Layer);
            return MouseState.Over ? kModsRow : Row;
        }

        // mouse confirm (this = sId): a click on MODS while it is the selected row
        bool __fastcall SpecialClickHook(void* Sid, void*, int Kind, int A, int B)
        {
            const bool Clicked = reinterpret_cast<bool(__thiscall*)(void*, int, int, int)>(SpecialClickOriginal)(Sid, Kind, A, B);
            // the click is tested before the row under the mouse in Move: only once MODS is the selected row
            return Clicked || (MouseState.Click && CurrentScreen && At<int>(CurrentScreen, 0x48) == kModsRow);
        }

        void __fastcall CursorIdleHook(void* Cursor, void*, std::uint32_t Look)
        {
            if (MouseState.Green)
            {
                MouseState.Green = false;
                Look = 1; // green pointer over MODS, as over the game's own rows
            }
            reinterpret_cast<void(__thiscall*)(void*, std::uint32_t)>(CursorIdleOriginal)(Cursor, Look);
        }

        // after the title (drawn every frame the screen draws): the MODS row, styled like the game's rows
        void __fastcall SpecialTitleHook(void* Screen, void*, std::uint32_t Message)
        {
            reinterpret_cast<void(__thiscall*)(void*, std::uint32_t)>(SpecialTitleOriginal)(Screen, Message);
            const bool Selected = At<int>(Screen, 0x48) == kModsRow;
            auto Color = RowColor(Screen, Selected);
            if (Selected && At<std::uint8_t>(Screen, 0x3D))
            {
                const auto Blink = This<std::uint32_t>(M->Blink, Screen, At<float>(Screen, 0x44)) & 0xFF;
                Color = (((Color >> 24) * Blink / 0xFF) << 24) | (Color & 0xFFFFFF);
            }
            DrawText(Screen, "MODS", kCenterX, kModsRowY, Color, RowScale(Screen, Selected));
            // Highlight with a Y writes it into the element for good (the game's rows pass -1: "where it is"), so
            // sel_06 stays at y 400 after MODS was selected. Row 2 puts it back before the game draws its rows.
            if (Selected)
            {
                This<void>(M->Highlight, Screen, kModsSel, kRowMsg, true, kModsRowY);
            }
            else if (At<int>(Screen, 0x48) == kSelRow)
            {
                This<void>(M->Highlight, Screen, kModsSel, kRowMsg, true, kSelRowY);
            }
        }

        void __fastcall SpecialHelpHook(void* Screen, void*, std::uint32_t Message, int Y)
        {
            if (At<int>(Screen, 0x48) == kModsRow)
            {
                DrawText(Screen, "Settings of the installed mods.", kCenterX, Y, White(Screen), 1.0f);
                return;
            }
            reinterpret_cast<void(__thiscall*)(void*, std::uint32_t, int)>(SpecialHelpOriginal)(Screen, Message, Y);
        }

        bool CallIs(std::uint32_t Site, std::uint32_t Target)
        {
            const auto* Code = reinterpret_cast<const std::uint8_t*>(Site);
            return Code[0] == 0xE8 && Site + 5 + *reinterpret_cast<const std::int32_t*>(Code + 1) == Target;
        }
    }

    void InstallUI(const Runtime::Addresses& Game)
    {
        M = &Game.Menu;
        const struct
        {
            std::uint32_t Site, Target;
        } Calls[] = {
            { M->CallSpecialCursor, M->SpecialCursor },
            { M->CallSpecialMouseRow, M->SpecialMouseRow },
            { M->CallSpecialClick, M->SpecialClick },
            { M->CallSpecialTitle, M->DrawTitle },
            { M->CallSpecialHelp, M->DrawHelp },
            { M->CallCursorIdle, 0x007ADC00 },
        };
        for (const auto& Call : Calls)
        {
            const auto* Code = reinterpret_cast<const std::uint8_t*>(Call.Site);
            if (Code[0] != 0xE8)
            {
                Log::Error("mod menu: {:08X} is not a call - Special Settings > MODS disabled", Call.Site);
                return;
            }
            if (!CallIs(Call.Site, Call.Target))
            {
                Log::Warn("mod menu: call at {:08X} already hooked by someone else - chaining", Call.Site);
            }
        }
        if (reinterpret_cast<const std::uint32_t*>(M->SpecialVtbl)[6] == 0)
        {
            Log::Error("mod menu: Special Settings vtable not found - MODS disabled");
            return;
        }

        std::memcpy(PageVtbl.data(), reinterpret_cast<const std::uintptr_t*>(M->PageVtbl) - 1, sizeof(PageVtbl));
        PageVtbl[1 + 5] = reinterpret_cast<std::uintptr_t>(&PageInit);
        PageVtbl[1 + 6] = reinterpret_cast<std::uintptr_t>(&PageMove);

        SpecialMoveOriginal = Hook::WriteVFunc(M->SpecialVtbl, 6, reinterpret_cast<std::uintptr_t>(&SpecialMove));
        SpecialCursorOriginal = Hook::WriteCall5(M->CallSpecialCursor, reinterpret_cast<std::uintptr_t>(&SpecialCursorHook));
        SpecialMouseRowOriginal = Hook::WriteCall5(M->CallSpecialMouseRow, reinterpret_cast<std::uintptr_t>(&SpecialMouseRowHook));
        SpecialClickOriginal = Hook::WriteCall5(M->CallSpecialClick, reinterpret_cast<std::uintptr_t>(&SpecialClickHook));
        SpecialTitleOriginal = Hook::WriteCall5(M->CallSpecialTitle, reinterpret_cast<std::uintptr_t>(&SpecialTitleHook));
        SpecialHelpOriginal = Hook::WriteCall5(M->CallSpecialHelp, reinterpret_cast<std::uintptr_t>(&SpecialHelpHook));
        CursorIdleOriginal = Hook::WriteCall5(M->CallCursorIdle, reinterpret_cast<std::uintptr_t>(&CursorIdleHook));
        Log::Info("mod menu: Extras > Special Settings > MODS on");
    }
}
