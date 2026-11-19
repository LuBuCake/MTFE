MTFE Plugin API
===============

The API is plain C for x86: `include/MTFE/PluginAPI.h` in [CommonLibRE5](https://github.com/LuBuCake/CommonLibRE5). With CommonLibRE5 nobody needs to use it directly: the `commonlibre5.plugin` xmake rule generates `MTFEPlugin_Version` and the library wraps the rest (`MTFE/API.h`, `MTFE/Menu.h`).

## Plugin Exports

| Export | Type | |
| --- | --- | --- |
| `MTFEPlugin_Version` | `MTFEPluginVersionData` (data) | name, author, version, supported game runtimes, minimum MTFE version |
| `MTFEPlugin_Load` | `bool (const MTFELoadInterface*)` | called at the game's entry point; `false` unloads the plugin |

Both are `extern "C"` (`__cdecl`), so the names are exported undecorated. Plugins load from `MTFE\Plugins\*.dll` in alphabetical order; `MTFEPlugin_Load` runs inside `__try`, so an exception is logged instead of crashing the game.

**At load time the game's C runtime has not run yet.** Hooks can be installed (the code is unpacked), but nothing of the game that needs initialization can be used: wait for `PRE_WINMAIN` (engine types) or `GAME_INIT` (game systems).

## `MTFELoadInterface`

| Field | |
| --- | --- |
| `mtfeVersion` | `MTFE_MAKE_VERSION(1, 0, 0)` |
| `runtimeVersion` | timestamp of the game executable (`MTFE_RUNTIME_RE5_STEAM_2023` = `0x63E9CE00`) |
| `QueryInterface(id)` | `MTFE_INTERFACE_MESSAGING`, `MTFE_INTERFACE_TASK`, `MTFE_INTERFACE_MENU` |
| `GetPluginHandle()` | only valid **during** `MTFEPlugin_Load` |
| `GetPluginInfo(name)` | another loaded plugin, or `NULL` |
| `GetGamePath()`, `GetMTFEPath()` | with a trailing backslash |

## Messages (sender `"MTFE"`)

| Type | When | `data` |
| --- | --- | --- |
| `POST_LOAD` (0) | every plugin is loaded (still at the entry point) | — |
| `POST_POST_LOAD` (1) | after `POST_LOAD` | — |
| `PRE_WINMAIN` (2) | C runtime and static initializers done, every engine type registered, no game system yet | — |
| `GAME_INIT` (3) | first frame: the main system and every game system exist, window and device created | `sMain*` |

`RegisterListener(handle, "MTFE", callback)`; a `NULL` sender listens to everyone. Plugins can message each other with `Dispatch` (the sender is the plugin name).

## Tasks

`AddTask(fn, context)` runs once on the next frame; `AddFrameCallback(fn, context)` runs every frame. Both are thread safe and run on the game's main thread, at the start of the main system's update (before the game's frame).

## Settings Menu (`MTFE_INTERFACE_MENU`)

Each plugin gets one page in **Extras > Special Settings > MODS** (main menu), drawn by the game's own menu (keyboard, controller, mouse). The MODS list shows the pages in plugin load order.

| Function | |
| --- | --- |
| `SetPageTitle(handle, title)` | title of the page and of its row in the list (default: the plugin name) |
| `AddControl(handle, desc)` | appends a row; the strings are copied; returns the control (0 = invalid desc) |
| `GetValue(control)` | current value |
| `SetValue(control, value)` | clamps, saves and calls `onChange` (thread safe) |

`MTFEMenuControlDesc` (always set `size = sizeof(MTFEMenuControlDesc)`; fields are only appended):

| Type | Value | Row |
| --- | --- | --- |
| `HEADER` | — | section title, skipped by the cursor |
| `TOGGLE` | 0 / 1 | `OFF` / `ON`; left/right or confirm |
| `LIST` | index into `items` | left/right; confirm goes to the next item and wraps |
| `NUMBER` | `min..max` by `step` | shown as value / 10^`decimals` (`15` with one decimal = `1.5`) |
| `HOTKEY` | game key code (0 = none) | confirm shows "Press a key", the next key or mouse button is taken; Back cancels |
| `BUTTON` | — | confirm calls `onChange(control, 0, context)` |

* **Saving:** a control with a `key` is stored as `key=<int>` in `<game>\MTFE\Config\<plugin>.ini`, section `[Settings]`. It is read by `AddControl` (the control already has the saved value) and written on every change. Nothing goes into the game save.
* `onChange` runs on the main thread when the player changes the value (or on the caller's thread for `SetValue`).
* Texts: ASCII 0x20..0x7E (the menu font has no accented letters; other characters become `?`).
* Game key codes: keyboard 1..0x61, mouse 0x62..0x66. CommonLibRE5 has `RE::GameKey::IsDown(code)`, `RE::GameKey::Watch` (key-down edge), `RE::GameKey::ToVirtualKey(code)` (Windows virtual key) and constants like `kF8`.
* C++ wrapper: `MTFE/Menu.h` (`MTFE::Menu::AddToggle(...)` and so on, callbacks as `std::function`). Full example: `examples/HelloRE5` in CommonLibRE5.
* `[Menu] bModsMenu=0` in `MTFE.ini` hides the MODS row; the API keeps working and the values are still saved.

```cpp
#include <MTFE/MTFE.h>
#include <RE/RE5.h>

namespace
{
    MTFE::Menu::Control Enabled;
    MTFE::Menu::Control Key;
}

MTFE_PLUGIN_LOAD(const MTFE::LoadInterface* Interface)
{
    MTFE::Init(Interface);
    Enabled = MTFE::Menu::AddToggle("bEnabled", "Enabled", "Turns the mod on and off.", true);
    Key = MTFE::Menu::AddHotkey("iKey", "Hotkey", "Key that does the thing.", RE::GameKey::kF8);
    return true;
}
```

## Versions

* `MTFE_VERSION` 1.0.0, `MTFE_PLUGIN_VERSION_DATA_VERSION` 1, `MTFE_MESSAGING_INTERFACE_VERSION` 1, `MTFE_TASK_INTERFACE_VERSION` 1, `MTFE_MENU_INTERFACE_VERSION` 1.
* Structs only grow at the end; an incompatible change bumps the version of the struct or interface.
