set_xmakever("3.0.0")

set_project("MTFE")
set_version("1.0.0")
set_languages("c++23")
set_warnings("allextra")
set_arch("x86")
set_runtimes("MT")

add_rules("mode.debug", "mode.releasedbg")

-- The ABI header lives in CommonLibRE5 (one source of truth for loader and plugins).
local commonlib = path.join(os.scriptdir(), "..", "CommonLibRE5")

target("dinput8")
    set_kind("shared")
    set_symbols("debug")
    add_files("src/**.cpp", "src/dinput8.def")
    add_headerfiles("src/**.h")
    add_includedirs("src", path.join(commonlib, "include"))
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", "UNICODE", "_UNICODE")
    add_syslinks("user32", "kernel32")
