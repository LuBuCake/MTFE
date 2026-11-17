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

#include <MTFE/PluginAPI.h>

#include <cstdint>
#include <string>

namespace PluginManager
{
    void SetPaths(std::string GamePath, std::string MTFEPath);

    // Loads <MTFE>\Plugins\*.dll (MTFE plugins) and, when asked, <game>\plugins\*.dll (old Plugin Loader DLLs).
    void LoadAll(bool Legacy);

    // Loader -> every listener of "MTFE".
    void Broadcast(std::uint32_t Type, void* Data = nullptr, std::uint32_t Length = 0);

    // Main thread, once per frame.
    void RunTasks();

    // Name of a loaded plugin (nullptr for a bad handle).
    const char* PluginName(MTFEPluginHandle Handle);
}
