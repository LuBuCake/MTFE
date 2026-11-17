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

#include "LauncherFix.h"

#include "Log.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>

namespace LauncherFix
{
    namespace
    {
        DWORD ParentPid()
        {
            const DWORD Self = GetCurrentProcessId();
            const HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (Snapshot == INVALID_HANDLE_VALUE)
            {
                return 0;
            }
            PROCESSENTRY32W Entry{ .dwSize = sizeof(Entry) };
            DWORD Parent = 0;
            for (BOOL Ok = Process32FirstW(Snapshot, &Entry); Ok; Ok = Process32NextW(Snapshot, &Entry))
            {
                if (Entry.th32ProcessID == Self)
                {
                    Parent = Entry.th32ParentProcessID;
                    break;
                }
            }
            CloseHandle(Snapshot);
            return Parent;
        }

        std::uint64_t CreationTime(HANDLE Process)
        {
            FILETIME Created{}, Exited{}, Kernel{}, User{};
            if (!GetProcessTimes(Process, &Created, &Exited, &Kernel, &User))
            {
                return 0;
            }
            return (static_cast<std::uint64_t>(Created.dwHighDateTime) << 32) | Created.dwLowDateTime;
        }
    }

    void CloseParentLauncher(const std::string& GamePath)
    {
        const DWORD Pid = ParentPid();
        if (!Pid)
        {
            return;
        }
        const HANDLE Parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, Pid);
        if (!Parent)
        {
            return;
        }
        char Path[MAX_PATH]{};
        DWORD Size = MAX_PATH;
        const std::string Launcher = GamePath + "Launcher.exe";
        // The parent must be <game>\Launcher.exe and older than us (a reused PID would be newer).
        if (QueryFullProcessImageNameA(Parent, 0, Path, &Size) && _stricmp(Path, Launcher.c_str()) == 0 &&
            CreationTime(Parent) <= CreationTime(GetCurrentProcess()))
        {
            if (TerminateProcess(Parent, 0))
            {
                Log::Info("closed Launcher.exe (pid {}): it would wait for a key forever after starting the game", Pid);
            }
            else
            {
                Log::Warn("could not close Launcher.exe (pid {}): error {}", Pid, GetLastError());
            }
        }
        CloseHandle(Parent);
    }
}
