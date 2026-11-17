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

#include "ModMenu.h"

#include "Log.h"
#include "PluginManager.h"

#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
#include <mutex>

namespace ModMenu
{
    namespace
    {
        std::mutex ControlsLock;
        std::string ConfigDir;           // <MTFE>\Config\ (trailing backslash)
        std::deque<Control> AllControls; // id = index + 1; never removed (stable addresses)
        std::deque<Page> AllPages;       // one per plugin

        std::string Ascii(const char* Text)
        {
            std::string Out = Text ? Text : "";
            for (auto& Char : Out)
            {
                if (static_cast<unsigned char>(Char) < 0x20 || static_cast<unsigned char>(Char) > 0x7E)
                {
                    Char = '?'; // the menu font only has ASCII
                }
            }
            return Out;
        }

        Page& PageOf(MTFEPluginHandle Plugin) // under ControlsLock
        {
            for (auto& Entry : AllPages)
            {
                if (Entry.Plugin == Plugin)
                {
                    return Entry;
                }
            }
            const char* Name = PluginManager::PluginName(Plugin);
            return AllPages.emplace_back(Page{ Plugin, Ascii(Name ? Name : "Mod"), {} });
        }

        std::int32_t Clamp(const Control& Item, std::int32_t Value)
        {
            switch (Item.Type)
            {
            case MTFE_MENU_TOGGLE:
                return Value ? 1 : 0;
            case MTFE_MENU_LIST:
                return Item.Items.empty() ? 0 : std::clamp<std::int32_t>(Value, 0, static_cast<std::int32_t>(Item.Items.size()) - 1);
            case MTFE_MENU_NUMBER:
            {
                Value = std::clamp(Value, Item.MinValue, Item.MaxValue);
                return Item.MinValue + (Value - Item.MinValue) / Item.Step * Item.Step;
            }
            case MTFE_MENU_HOTKEY:
                return std::clamp<std::int32_t>(Value, 0, 0x66);
            default:
                return 0;
            }
        }

        void Save(const Control& Item)
        {
            if (!Item.Key.empty())
            {
                WritePrivateProfileStringA("Settings", Item.Key.c_str(), std::to_string(Item.Value.load()).c_str(), Item.Ini.c_str());
            }
        }

        Control* Find(MTFEMenuControl ID)
        {
            std::lock_guard Guard(ControlsLock);
            return ID >= 1 && ID <= AllControls.size() ? &AllControls[ID - 1] : nullptr;
        }

        // ---- MTFEMenuInterface ----

        void SetPageTitle(MTFEPluginHandle Plugin, const char* Title)
        {
            std::lock_guard Guard(ControlsLock);
            if (Title && *Title)
            {
                PageOf(Plugin).Title = Ascii(Title);
            }
        }

        MTFEMenuControl AddControl(MTFEPluginHandle Plugin, const MTFEMenuControlDesc* Desc)
        {
            if (!Desc || Desc->size < sizeof(MTFEMenuControlDesc) || Desc->type > MTFE_MENU_BUTTON || !Desc->label ||
                (Desc->type == MTFE_MENU_LIST && (!Desc->items || !Desc->itemCount)) ||
                (Desc->type == MTFE_MENU_NUMBER && Desc->minValue > Desc->maxValue))
            {
                Log::Warn("menu: plugin {} sent an invalid control ({})", Plugin, Desc && Desc->label ? Desc->label : "no label");
                return MTFE_INVALID_MENU_CONTROL;
            }
            const char* Name = PluginManager::PluginName(Plugin);
            if (!Name)
            {
                return MTFE_INVALID_MENU_CONTROL;
            }

            std::lock_guard Guard(ControlsLock);
            auto& Item = AllControls.emplace_back();
            Item.ID = static_cast<MTFEMenuControl>(AllControls.size());
            Item.Type = Desc->type;
            Item.Key = Desc->type == MTFE_MENU_HEADER || Desc->type == MTFE_MENU_BUTTON || !Desc->key ? "" : Desc->key;
            Item.Label = Ascii(Desc->label);
            Item.Help = Ascii(Desc->help);
            Item.DefaultValue = Desc->defaultValue;
            Item.MinValue = Desc->minValue;
            Item.MaxValue = Desc->maxValue;
            Item.Step = Desc->step > 0 ? Desc->step : 1;
            Item.Decimals = std::min<std::uint32_t>(Desc->decimals, 6);
            for (std::uint32_t Index = 0; Desc->type == MTFE_MENU_LIST && Index < Desc->itemCount; ++Index)
            {
                Item.Items.push_back(Ascii(Desc->items[Index]));
            }
            Item.OnChange = Desc->onChange;
            Item.Context = Desc->context;
            Item.Ini = ConfigDir + Name + ".ini";

            std::int32_t Value = Desc->defaultValue;
            if (!Item.Key.empty())
            {
                char Text[32]{};
                GetPrivateProfileStringA("Settings", Item.Key.c_str(), "", Text, sizeof(Text), Item.Ini.c_str());
                char* End = nullptr;
                const long Saved = std::strtol(Text, &End, 0);
                if (End != Text)
                {
                    Value = static_cast<std::int32_t>(Saved);
                }
            }
            Item.Value = Clamp(Item, Value);
            PageOf(Plugin).Controls.push_back(&Item);
            return Item.ID;
        }

        std::int32_t GetValue(MTFEMenuControl ID)
        {
            const auto* Item = Find(ID);
            return Item ? Item->Value.load() : 0;
        }

        void SetValueById(MTFEMenuControl ID, std::int32_t Value)
        {
            if (auto* Item = Find(ID))
            {
                SetValue(*Item, Value);
            }
        }

        const MTFEMenuInterface MenuInterface{ MTFE_MENU_INTERFACE_VERSION, SetPageTitle, AddControl, GetValue, SetValueById };
    }

    std::string Control::ValueText() const
    {
        const auto Current = Value.load();
        switch (Type)
        {
        case MTFE_MENU_TOGGLE:
            return Current ? "ON" : "OFF";
        case MTFE_MENU_LIST:
            return Current >= 0 && Current < static_cast<std::int32_t>(Items.size()) ? Items[Current] : "";
        case MTFE_MENU_NUMBER:
        {
            if (!Decimals)
            {
                return std::to_string(Current);
            }
            double Scaled = Current;
            for (std::uint32_t Index = 0; Index < Decimals; ++Index)
            {
                Scaled /= 10.0;
            }
            return std::format("{:.{}f}", Scaled, Decimals);
        }
        default:
            return "";
        }
    }

    void SetPaths(const std::string& MTFEPath)
    {
        ConfigDir = MTFEPath + "Config\\";
        std::error_code ErrorCode;
        std::filesystem::create_directories(ConfigDir, ErrorCode);
    }

    const MTFEMenuInterface* GetInterface()
    {
        return &MenuInterface;
    }

    std::vector<Page*> Pages()
    {
        std::lock_guard Guard(ControlsLock);
        std::vector<Page*> Out;
        for (auto& Entry : AllPages)
        {
            if (!Entry.Controls.empty())
            {
                Out.push_back(&Entry);
            }
        }
        return Out;
    }

    void SetValue(Control& Item, std::int32_t Value)
    {
        if (Item.Type == MTFE_MENU_HEADER || Item.Type == MTFE_MENU_BUTTON)
        {
            return;
        }
        const auto Clamped = Clamp(Item, Value);
        if (Item.Value.exchange(Clamped) == Clamped)
        {
            return;
        }
        Save(Item);
        if (Item.OnChange)
        {
            Item.OnChange(Item.ID, Clamped, Item.Context);
        }
    }

    void Press(Control& Item)
    {
        if (Item.Type == MTFE_MENU_BUTTON && Item.OnChange)
        {
            Item.OnChange(Item.ID, 0, Item.Context);
        }
    }
}
