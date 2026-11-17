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

#include <string>

namespace ModFolders
{
    // Mod folders, like the GTA San Andreas modloader: <game>\MTFE\Mods\<any mod name>\... holds replacement files, and
    // the game reads them instead of its own, without touching a single original file.
    //   - an .arc is matched by file name (RE5's archive names are unique), in any subfolder of the mod;
    //   - any other file is placed from the first component of its path that is a game root folder (pawn, stage,
    //     effect, sound, event, id, etc, system, movie, Image, dlc), so "Mod\nativePC_MT\pawn\...", "Mod\pawn\..." and
    //     "Mod\files\pawn\..." all mean nativePC_MT\pawn\...; a bare file name that exists at the root works too;
    //   - a loose file wins over the same resource inside any .arc (original or replaced by another mod);
    //   - conflicts: the mod folder that sorts last (case-insensitive) wins; folders starting with '-' are disabled.
    // nativePC_MT keeps the game's own rules. See docs/ARCHITECTURE.md, "Mod folders".
    struct Settings
    {
        std::string MTFEPath; // <game>\MTFE\ (trailing backslash)
        bool LogEachFile = true;
    };

    void Install(const Runtime::Addresses& Game, const Settings& Config);
}
