MTFE (MT Framework Extender)
============================

Plugin loader for games on Capcom's **MT Framework**. It currently supports **Resident Evil 5** (Steam, Gold Edition, version 1.2.0).

## Features

* **Plugins**: loads the DLL plugins in `MTFE\Plugins`. Plugins are written with [CommonLibRE5](https://github.com/LuBuCake/CommonLibRE5).
* **Mod folders**: put a mod in `MTFE\Mods\<mod name>\` and the game uses its files instead of the original ones, without replacing any game file. `.arc` files are matched by name; any other file by its game path (`pawn\`, `stage\`, `effect\`...). When two mods have the same file, the folder that comes last in alphabetical order wins. A folder whose name starts with `-` is disabled.
* **Mod settings menu**: plugins get their own settings page in the game's **Extras > Special Settings > MODS**, usable with keyboard, controller and mouse. Settings are saved in `MTFE\Config\<plugin>.ini`.
* **Launcher fix**: closes Capcom's `Launcher.exe`, which otherwise keeps running after the game closes, so Steam shows the game as running.
* Also loads the plugins of the older RE5 Plugin Loader (`<game>\plugins`).

## Supported Games

| Game | Version |
| --- | --- |
| Resident Evil 5 (Steam, Gold Edition) | 1.2.0 |

## Installing

1. Copy `dinput8.dll` and the `MTFE` folder into the game folder, next to `re5dx9.exe`.
2. Put plugins in `MTFE\Plugins` and mods in `MTFE\Mods\<mod name>`.
3. Logs are written to `MTFE\Logs`.

### Settings

`MTFE\MTFE.ini`:

| Setting | Default | |
| --- | --- | --- |
| `[Loader] bEnablePlugins` | 1 | load the plugins |
| `[Loader] bLoadLegacyPlugins` | 1 | also load the plugins in `<game>\plugins` |
| `[Loader] bCloseLauncher` | 1 | close `Launcher.exe` |
| `[Resources] bModFolders` | 1 | mod folders in `MTFE\Mods` |
| `[Resources] bLogModFiles` | 1 | log every mod file used |
| `[Menu] bModsMenu` | 1 | the MODS menu |
| `[Debug] bConsole` | 0 | console window with the log |

## Writing Plugins

Use [CommonLibRE5](https://github.com/LuBuCake/CommonLibRE5). The plugin API is described in [docs/PLUGIN-API.md](docs/PLUGIN-API.md).

## Building

Requirements:

* [Visual Studio](https://visualstudio.microsoft.com/) with *Desktop development with C++* (MSVC x86) and the Windows SDK 10.0.26100.
* [XMake](https://xmake.io) 3.0 or newer.
* [CommonLibRE5](https://github.com/LuBuCake/CommonLibRE5) cloned next to this repository:

```
GitHub\
  MTFE\
  CommonLibRE5\
```

From a PowerShell or Developer prompt in this folder:

```
xmake f -y -p windows -a x86 -m releasedbg --vs_sdkver=10.0.26100.0
xmake build
```

The DLL is `build\windows\x86\releasedbg\dinput8.dll`. `deploy.ps1 -Game "<game folder>"` installs it.

## License

[GPL-3.0](LICENSE) with the [modding exception](EXCEPTIONS).
