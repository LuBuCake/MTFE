MTFE (MT Framework Extender)
============================

Plugin loader and runtime extender for games on Capcom's **MT Framework**, in the spirit of F4SE for Fallout 4 and REFramework for the RE Engine. It currently supports **Resident Evil 5** (Steam, Gold Edition, the 2023 update).

MTFE ships as a `dinput8.dll` proxy. It waits for the game to unpack itself (the executable is protected by the Enigma Protector, so nothing can be patched from `DllMain`), loads DLL plugins from `MTFE\Plugins`, gives them a stable C API and tells them when each stage of the game's startup is done. Plugins are written with [CommonLibRE5](https://github.com/LuBuCake/CommonLibRE5).

## Features

* **Plugin loader** that enters at the game's real entry point, after the Enigma Protector has unpacked the code. Messages at plugin load, before `WinMain` (all engine types registered) and on the first frame (every game system created); tasks and per-frame callbacks on the game's main thread.
* **Mod folders**, like the GTA San Andreas modloader: drop a mod in `MTFE\Mods\<any name>\` and the game reads its files instead of its own, without replacing a single original file. An `.arc` is matched by its name, anywhere in the mod folder; any other file by its game path (`pawn\`, `stage\`, `effect\`..., a leading `nativePC_MT\` is fine). A loose file also wins over the same file inside an `.arc`, original or from another mod. The mod folder that sorts last wins a conflict; a folder whose name starts with `-` is disabled.
* **Mod settings menu**: plugins get their own page in the game's **Extras > Special Settings > MODS**, drawn by the game's own menu (keyboard, controller and mouse), with toggles, lists, numbers, hotkeys and buttons. Values are saved in `MTFE\Config\<plugin>.ini`, never in the game save.
* **Launcher fix**: Capcom's `Launcher.exe` waits for a key forever on Windows 10/11 after starting the game, so Steam keeps the game as running after it closes. MTFE closes it.
* Old plugin DLLs in `<game>\plugins` (the earlier RE5 Plugin Loader) still load, now after the game is unpacked.

## Supported Runtimes

| Game | Build | Executable timestamp |
| --- | --- | --- |
| Resident Evil 5 (Steam, Gold Edition) | Jan 23 2023 | `0x63E9CE00` |

On an unknown executable, plugins still load but the game hooks, the mod folders and the menu stay off.

## Installing

1. Copy `dinput8.dll` and the `MTFE` folder (with `MTFE.ini`) into the game folder, next to `re5dx9.exe`.
2. Put plugins in `MTFE\Plugins` and mods in `MTFE\Mods\<mod name>`.
3. Logs: `MTFE\Logs\mtfe.log` and one log per plugin.

`MTFE.ini`:

| Setting | Default | |
| --- | --- | --- |
| `[Loader] bEnablePlugins` | 1 | 0 loads nothing (the proxy keeps working) |
| `[Loader] bLoadLegacyPlugins` | 1 | also load `<game>\plugins\*.dll` |
| `[Loader] bCloseLauncher` | 1 | close the stuck `Launcher.exe` |
| `[Resources] bModFolders` | 1 | mod folders in `MTFE\Mods` |
| `[Resources] bLogModFiles` | 1 | log every mod file that replaces an archive entry |
| `[Menu] bModsMenu` | 1 | Extras > Special Settings > MODS |
| `[Debug] bConsole` | 0 | console window with the log |

## Writing Plugins

Use [CommonLibRE5](https://github.com/LuBuCake/CommonLibRE5): its xmake rule generates the plugin's version data and the library wraps the API. The C API itself is `include/MTFE/PluginAPI.h` in CommonLibRE5; see [docs/PLUGIN-API.md](docs/PLUGIN-API.md).

## Building

### Pre-requisites

* [Visual Studio](https://visualstudio.microsoft.com/) with the *Desktop development with C++* workload (MSVC x86) and the Windows SDK 10.0.26100.
* [XMake](https://xmake.io) 3.0 or newer.
* [CommonLibRE5](https://github.com/LuBuCake/CommonLibRE5) cloned **next to** this repository (the build reads the plugin API header from `../CommonLibRE5`):

```
GitHub\
  MTFE\
  CommonLibRE5\
```

### Building

From a PowerShell or Developer prompt in this folder:

```
xmake f -y -p windows -a x86 -m releasedbg --vs_sdkver=10.0.26100.0
xmake build
```

The DLL is `build\windows\x86\releasedbg\dinput8.dll`. `deploy.ps1 -Game "<game folder>" [-Plugins <dll>,...]` copies it (and plugins) into the game.

## Project Layout

```
src/main.cpp          DllMain, entry hook, game hooks, dinput8 proxy
src/Hook.*            API hook (hot-patch), call/vtable rewrites (they return the previous target)
src/Runtime.*         game version detection and per-version addresses
src/PluginManager.*   plugin loading, messages, tasks, interfaces
src/ModFolders.*      mod folders (MTFE\Mods)
src/ModMenu*.*        Extras > Special Settings > MODS and the menu API
src/LauncherFix.*     closes the stuck Launcher.exe
src/Log.*             MTFE\Logs\mtfe.log
dist/MTFE/MTFE.ini    default settings
docs/                 plugin API and architecture
```

Every hook keeps the chain: a call or vtable rewrite returns the previous target and the hook calls it, so other mods hooking the same place keep working.

## License

[GPL-3.0](LICENSE) with the [modding exception](EXCEPTIONS).
