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

#include <string>

namespace LauncherFix
{
    // Capcom's Launcher.exe (2009, the process Steam starts) runs re5dx9.exe, then checks GetLastError() even after a
    // successful CreateProcessA. On Windows 10/11 that value is left over (e.g. 0x7E), so the launcher prints
    // "res = 1, err = 0x0000007e..." and spins on _kbhit() until a key is pressed: Steam keeps the game as "running"
    // after it closes. When our parent process is <game>\Launcher.exe, terminate it. See docs/ARCHITECTURE.md.
    void CloseParentLauncher(const std::string& GamePath);
}
