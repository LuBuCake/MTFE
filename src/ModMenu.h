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

#include "Runtime.h"

#include <MTFE/PluginAPI.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Mod settings hub: Extras > Special Settings > MODS in the game's own menu (src/ModMenuUI.cpp draws it), with the pages and controls
// that plugins register through MTFEMenuInterface (this file). See docs/PLUGIN-API.md, "Menu".
namespace ModMenu
{
    struct Control
    {
        MTFEMenuControl ID = 0;
        std::uint32_t Type = MTFE_MENU_HEADER;
        std::string Key, Label, Help;
        std::int32_t DefaultValue = 0, MinValue = 0, MaxValue = 0, Step = 1;
        std::uint32_t Decimals = 0;
        std::vector<std::string> Items;
        MTFEMenuCallback OnChange = nullptr;
        void* Context = nullptr;
        std::string Ini; // <MTFE>\Config\<plugin>.ini
        std::atomic<std::int32_t> Value{ 0 };

        bool Selectable() const { return Type != MTFE_MENU_HEADER; }
        std::string ValueText() const; // "" for HEADER / BUTTON / HOTKEY (the key name is a game message)
    };

    struct Page
    {
        MTFEPluginHandle Plugin = 0;
        std::string Title;
        std::vector<Control*> Controls;
    };

    void SetPaths(const std::string& MTFEPath);
    const MTFEMenuInterface* GetInterface();

    // UI side (main thread). The pointers stay valid for the whole session.
    std::vector<Page*> Pages(); // pages with at least one control, in plugin load order
    void SetValue(Control& Item, std::int32_t Value);
    void Press(Control& Item); // BUTTON

    void InstallUI(const Runtime::Addresses& Game);
}
