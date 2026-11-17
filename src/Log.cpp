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

#include "Log.h"

#include <Windows.h>

#include <share.h>

#include <cstdio>
#include <mutex>

namespace
{
    std::mutex LogLock;
    FILE* LogFile = nullptr;
    bool ConsoleOpen = false;
}

namespace Log
{
    void Open(const std::string& Path, bool Console)
    {
        std::lock_guard Guard(LogLock);
        if (!LogFile)
        {
            LogFile = _fsopen(Path.c_str(), "w", _SH_DENYWR); // readable while the game runs
        }
        if (Console && !ConsoleOpen && AllocConsole())
        {
            FILE* Dummy = nullptr;
            freopen_s(&Dummy, "CONOUT$", "w", stdout);
            SetConsoleTitleA("MTFE");
            ConsoleOpen = true;
        }
    }

    void Write(std::string_view Level, std::string_view Text)
    {
        SYSTEMTIME Now;
        GetLocalTime(&Now);
        const auto Line = std::format("[{:02}:{:02}:{:02}.{:03}] [{}] {}\n", Now.wHour, Now.wMinute, Now.wSecond,
                                      Now.wMilliseconds, Level, Text);

        std::lock_guard Guard(LogLock);
        if (LogFile)
        {
            std::fwrite(Line.data(), 1, Line.size(), LogFile);
            std::fflush(LogFile);
        }
        if (ConsoleOpen)
        {
            std::fwrite(Line.data(), 1, Line.size(), stdout);
        }
        OutputDebugStringA(Line.c_str());
    }
}
