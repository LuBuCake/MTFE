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

// Options > MODS: the row added to the game's category list (uIdOption00) and our own page (a uIdOption05 instance,
// KEYBOARD/MOUSE layout top31, with a cloned vtable whose init/move are ours). Everything is drawn with the menu's own
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
        constexpr int kModsRow = 8;                                // our row in the category list (game rows 0..7)
        constexpr int kModsRowY = 494;                             // the list's rows are 43 px apart (150..451)
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
        bool KeyboardPlayer(void* Unit)
        {
            return At<int>(Unit, 0x34) == At<int>(Global<void>(M->SPad), 0x614);
        }

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
            This<void>(M->SetHitMap, Unit, 5u, M->PageRowsMap, 0u);
            This<void>(M->SetHitMap, Unit, 5u, M->PageArrowsMap, 1u);
            This<void>(M->SetHitMap, Unit, 5u, 0u, 2u);

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
                This<void>(M->ReleaseMouse, Unit);
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

        // ---- the MODS row in the category list -----------------------------------------------------------------
        std::uintptr_t ListMoveOriginal = 0, CursorMoveOriginal = 0, DrawTitleOriginal = 0, DrawHelpOriginal = 0, MouseRowOriginal = 0, MouseClickOriginal = 0, CursorIdleOriginal = 0;

        struct ListMouse
        {
            bool Over = false, Click = false, Green = false, PrevDown = false;
            void* List = nullptr;
        } MouseState;

        bool IsMainMenuList(void* List)
        {
            return At<int>(List, 0x18C) == 0;
        }

        // The list's hit map only has rows 0..7: MODS = row 7's rectangle moved down by (row 7 - row 6).
        void UpdateListMouse(void* List)
        {
            auto* Pad = Global<void>(M->SPad);
            auto* Sid = Global<void>(M->SId);
            auto* Mouse = Global<void>(M->SMouse);
            if (!Pad || !Sid || !Mouse || At<int>(List, 0x34) != At<int>(Pad, 0x614))
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
            if (!Map || At<std::uint32_t>(Map, 0xC) < 8)
            {
                return;
            }
            auto** Rects = At<void**>(Map, 0x18);
            const auto* R6 = reinterpret_cast<const int*>(reinterpret_cast<std::uintptr_t>(Rects[6]) + 4);
            const auto* R7 = reinterpret_cast<const int*>(reinterpret_cast<std::uintptr_t>(Rects[7]) + 4);
            const int Dy = R7[1] - R6[1];
            const int X0 = R7[0] + At<int>(Sid, 0x50), X1 = R7[2] + At<int>(Sid, 0x50);
            const int Y0 = R7[1] + Dy + At<int>(Sid, 0x54), Y1 = R7[3] + Dy + At<int>(Sid, 0x54);
            const int Px = At<int>(Sid, 0x2E0), Py = At<int>(Sid, 0x2E4); // pointer in menu space (sId, this frame)
            MouseState.Over = Px >= X0 && Px < X1 && Py >= Y0 && Py < Y1;
            MouseState.Click = MouseState.Over && Pressed;
            MouseState.Green = MouseState.Over; // consumed by the next sId update (it runs after the menu units)
        }

        // state 6 with the cursor on MODS: open our page and wait for it, as the game does for its pages (0xA27AB0).
        void OpenModsPage(void* List)
        {
            auto& Sub = At<std::uint8_t>(List, 0x15);
            auto& Child = At<void*>(List, 0x19C);
            switch (Sub)
            {
            case 0:
                if (At<void*>(List, 0x128) && This<bool>(M->TransitionBusy, At<void*>(List, 0x128)))
                {
                    return;
                }
                if ((Child = CreatePage()) != nullptr)
                {
                    At<std::uint32_t>(Child, 4) |= 0x2000;
                    At<std::uint32_t>(Child, 0x34) = At<std::uint32_t>(List, 0x34);
                    At<void*>(Child, 0x128) = At<void*>(List, 0x128);
                    This<void>(M->AddUnit, Global<void>(M->SUnit), 0x1Bu, Child);
                }
                ++Sub;
                break;
            case 1:
                if (Child && !At<bool>(Child, 0x3C))
                {
                    return;
                }
                if (Child)
                {
                    At<std::uint32_t>(Child, 4) &= ~0x2000u;
                    This<void>((*reinterpret_cast<std::uint32_t**>(Child))[12], Child); // kill
                    Child = nullptr;
                }
                ++Sub;
                break;
            default:
                ++At<std::uint8_t>(List, 0x14); // -> 7: back to the list (fade in again)
                break;
            }
        }

        void __fastcall ListMove(void* List)
        {
            MouseState.Over = MouseState.Click = MouseState.Green = false;
            MouseState.List = List;
            if (IsMainMenuList(List))
            {
                const auto State = At<std::uint8_t>(List, 0x14);
                if (State == 6 && At<int>(List, 0x48) == kModsRow)
                {
                    OpenModsPage(List);
                    return;
                }
                if (State == 2 && At<int>(List, 0x190) == 0)
                {
                    UpdateListMouse(List);
                }
            }
            reinterpret_cast<void(__thiscall*)(void*)>(ListMoveOriginal)(List);
        }

        std::uint32_t __fastcall CursorMoveHook(void* List, void*, std::uint32_t Trigger, std::uint32_t Repeat, std::uint32_t CurrentRow, int Count)
        {
            if (IsMainMenuList(List) && Count == 8)
            {
                Count = 9;
            }
            return reinterpret_cast<std::uint32_t(__stdcall*)(std::uint32_t, std::uint32_t, std::uint32_t, int)>(CursorMoveOriginal)(Trigger, Repeat, CurrentRow, Count);
        }

        int __fastcall MouseRowHook(void* List, void*, int CurrentRow, int Count, int Start, int Layer)
        {
            const int Row = reinterpret_cast<int(__thiscall*)(void*, int, int, int, int)>(MouseRowOriginal)(List, CurrentRow, Count, Start, Layer);
            return IsMainMenuList(List) && MouseState.Over ? kModsRow : Row;
        }

        bool __fastcall MouseClickHook(void* Sid, void*, int First, int Last, int Kind, int Layer)
        {
            const bool Clicked = reinterpret_cast<bool(__thiscall*)(void*, int, int, int, int)>(MouseClickOriginal)(Sid, First, Last, Kind, Layer);
            return Clicked || (MouseState.Click && MouseState.List && At<int>(MouseState.List, 0x48) == kModsRow);
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

        void __fastcall DrawTitleHook(void* List, void*, std::uint32_t Message)
        {
            reinterpret_cast<void(__thiscall*)(void*, std::uint32_t)>(DrawTitleOriginal)(List, Message);
            if (!IsMainMenuList(List))
            {
                return;
            }
            const bool Selected = At<int>(List, 0x48) == kModsRow;
            if (Selected && At<std::uint8_t>(List, 0x3D) && At<int>(List, 0x190) == 0)
            {
                This<void>(M->Blink, List, At<float>(List, 0x44));
            }
            DrawText(List, "MODS", kCenterX, kModsRowY, RowColor(List, Selected), RowScale(List, Selected));
            if (At<int>(List, 0x190) == 0)
            {
                This<void>(M->Highlight, List, 1, 0x532, Selected, kModsRowY);
            }
        }

        void __fastcall DrawHelpHook(void* List, void*, std::uint32_t Message, int Y)
        {
            if (IsMainMenuList(List) && At<int>(List, 0x48) == kModsRow)
            {
                DrawText(List, "Settings of the installed mods.", kCenterX, Y, White(List), 1.0f);
                return;
            }
            reinterpret_cast<void(__thiscall*)(void*, std::uint32_t, int)>(DrawHelpOriginal)(List, Message, Y);
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
            { M->CallCursorMove, M->CursorMove },
            { M->CallDrawTitle, 0x00A17D40 },
            { M->CallDrawHelp, 0x00A17CF0 },
            { M->CallMouseRow, M->MouseRow },
            { M->CallMouseClick, M->MouseClicked },
            { M->CallCursorIdle, 0x007ADC00 },
        };
        for (const auto& Call : Calls)
        {
            const auto* Code = reinterpret_cast<const std::uint8_t*>(Call.Site);
            if (Code[0] != 0xE8)
            {
                Log::Error("mod menu: {:08X} is not a call - Options > MODS disabled", Call.Site);
                return;
            }
            if (!CallIs(Call.Site, Call.Target))
            {
                Log::Warn("mod menu: call at {:08X} already hooked by someone else - chaining", Call.Site);
            }
        }

        std::memcpy(PageVtbl.data(), reinterpret_cast<const std::uintptr_t*>(M->PageVtbl) - 1, sizeof(PageVtbl));
        PageVtbl[1 + 5] = reinterpret_cast<std::uintptr_t>(&PageInit);
        PageVtbl[1 + 6] = reinterpret_cast<std::uintptr_t>(&PageMove);

        ListMoveOriginal = Hook::WriteVFunc(M->ListVtbl, 6, reinterpret_cast<std::uintptr_t>(&ListMove));
        CursorMoveOriginal = Hook::WriteCall5(M->CallCursorMove, reinterpret_cast<std::uintptr_t>(&CursorMoveHook));
        DrawTitleOriginal = Hook::WriteCall5(M->CallDrawTitle, reinterpret_cast<std::uintptr_t>(&DrawTitleHook));
        DrawHelpOriginal = Hook::WriteCall5(M->CallDrawHelp, reinterpret_cast<std::uintptr_t>(&DrawHelpHook));
        MouseRowOriginal = Hook::WriteCall5(M->CallMouseRow, reinterpret_cast<std::uintptr_t>(&MouseRowHook));
        MouseClickOriginal = Hook::WriteCall5(M->CallMouseClick, reinterpret_cast<std::uintptr_t>(&MouseClickHook));
        CursorIdleOriginal = Hook::WriteCall5(M->CallCursorIdle, reinterpret_cast<std::uintptr_t>(&CursorIdleHook));
        Log::Info("mod menu: Options > MODS on");
    }
}
