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

#include <format>
#include <string>
#include <string_view>

namespace Log
{
    void Open(const std::string& Path, bool Console);
    void Write(std::string_view Level, std::string_view Text);

    template <class... Args>
    void Info(std::format_string<Args...> Format, Args&&... Arguments)
    {
        Write("info", std::format(Format, std::forward<Args>(Arguments)...));
    }

    template <class... Args>
    void Warn(std::format_string<Args...> Format, Args&&... Arguments)
    {
        Write("warn", std::format(Format, std::forward<Args>(Arguments)...));
    }

    template <class... Args>
    void Error(std::format_string<Args...> Format, Args&&... Arguments)
    {
        Write("error", std::format(Format, std::forward<Args>(Arguments)...));
    }
}
