MTFE Architecture
=================

Addresses below are for Resident Evil 5, Steam, build of Jan 23 2023 (the executable has no ASLR: base `0x400000`).

## The Problem: the Enigma Protector

The Steam `re5dx9.exe` ships with SteamStub and the Enigma Protector: the game code is packed in the file and only exists in memory after the Enigma stub has run. Windows loads `dinput8.dll` **before** that, with the game's `.text` still packed, so anything patched from `DllMain` is written over bytes the unpacker is about to overwrite.

## Startup Timeline

| Moment | How MTFE sees it | What it does |
| --- | --- | --- |
| `DllMain` of `dinput8.dll` | — | reads the INI, opens the log, detects the game version (PE timestamp), installs the entry hook |
| Entry point (`0xE68A06`): `call __security_init_cookie` | first `GetSystemTimeAsFileTime` call whose return address is in the game's `.text` | removes the entry hook, installs the game hooks, loads the plugins, `POST_LOAD`, `POST_POST_LOAD` |
| `__tmainCRTStartup` calls `WinMain` (`0xE68998`) | call rewritten | `PRE_WINMAIN`: C runtime and static initializers done, every engine type (MtDTI) registered, no game system yet |
| First frame of the main loop | slot 6 of the `sBioMain` vtable (`0xF5F620`) | `GAME_INIT` (data = the main system) |
| Every frame | same slot | runs `AddTask` tasks and per-frame callbacks |

### The Entry Hook

* Target: `kernelbase!GetSystemTimeAsFileTime` (the `kernel32` export only jumps there). On Windows x86 it has the hot-patch prologue (`mov edi,edi` with 5 padding bytes before it): the hook writes `jmp detour` into the padding and `jmp $-5` over `mov edi,edi`; the original is `function + 2`. A `jmp dword ptr [import]` thunk is handled too.
* Why this function: it is the first API the game's C runtime calls (`__security_init_cookie`, right at the entry point). The Enigma resolves the import table before that, so the game calls the real export, which now goes through the detour.
* Filter: only calls whose `_ReturnAddress()` is inside the **first section** of the executable (the game's `.text`) count. Calls from the Enigma itself and from other DLLs pass through.
* Removal: `mov edi,edi` is restored first (one aligned 2-byte write), then the padding, so no thread can run a half-written patch.
* Loaded late (injected into a running game): the security cookie is no longer the MSVC default, so plugins load on a new thread, without `PRE_WINMAIN`.

### Game Hooks

Only for a known game version and only after checking the bytes: the entry point (`E8 … E9`) and the `WinMain` call (`E8` to `0x713F40`). The per-frame slot is replaced with a vtable write and the hook calls the previous entry. Every hook in MTFE keeps the chain this way, so other mods hooking the same call or slot keep working.

## Mod Folders

How the engine loads resources:

* `sResource::GetResource(type, path, flags)` (`0x448B70`) looks the resource up in its cache by (type, path); on a miss, `flags & 1` loads `<root>\<path>.<ext>` right away (`0x448FD0`, `CreateFileA` at `0x449129`; a failure is the fatal "Failed open file") and the loader thread does the same for asynchronous requests (`0x447080`, `CreateFileA` at `0x447300`). The root is `<game>\nativePC_MT` for resource classes with flag `0x10` (set by each class constructor), `<game>\resource` otherwise.
* `rArchive` vtable slot 8 (`0x621A10`) reads the archive table (`ARC\0`, version 7, 0x50-byte entries: name[0x40], type id, compressed size, size | quality << 29, offset) and queues every entry with the cached resource of that (type, path) or a new one; the worker threads (`0x449730`) **skip every resource that is already loaded**.

What MTFE does:

1. The two `CreateFileA` calls of the resource loaders (6-byte `call [import]`, rewritten into our call plus a `nop`; the import slot is read on every call, so import hooks keep working) open the mod's file when a mod replaces that path, under either root. This alone replaces whole `.arc` files and loose files.
2. Before each archive's own load, the hook (`rArchive` slot 8) reads the table of the archive that will be used (the mod's one, if any) and, for every entry a mod ships as a loose file, calls `GetResource` synchronously: the resource enters the cache already loaded (through step 1) and the archive skips its own copy. The extra reference is released afterwards (`0x447020`, `EAX` = `sResource`, `EDI` = resource). The extension of each resource type comes from slot 6 of a temporary instance's vtable (`DTI->NewInstance()`), cached per type.

Placement: an `.arc` by its file name (RE5's 1,243 archive names are unique); any other file by the first component of its path that is a game root folder (`pawn`, `stage`, `effect`, `sound`, `event`, `id`, `etc`, `system`, `movie`, `Image`, `dlc`, plus the real folders of `nativePC_MT`); a bare name that exists at the root of `nativePC_MT` works too. Files that cannot be placed are logged and ignored.

`nativePC_MT` itself keeps the game's rules (the archive wins). Character and weapon mods replace `.arc` files and rely on a missing-loose-file patch, and RE5 ships about 6,000 loose files that duplicate archive entries, many of them older builds of those files.

## Settings Menu

Options > MODS adds a 9th row to the game's option category list (`uIdOption00`) with chain-keeping hooks on its per-frame function and on the calls it makes for cursor movement, the title, the help line, the hovered row, the click and the mouse pointer look. Our page is an instance of the game's KEYBOARD/MOUSE page (`uIdOption05`, layout `top31`: a 7-row scrolling list) with a cloned vtable whose init and per-frame slots are ours; it draws with the menu's own helpers and activates the page's mouse hit maps every frame, so it behaves like the game's pages.

## Launcher.exe

Steam starts Capcom's `Launcher.exe` (2009), which only does `CreateProcessA("re5dx9.exe", args)`, reads `GetLastError()` even after a successful call and, unless the value is 0 or `0x715`, prints `res = %d, err = 0x%08x, %s` and spins on `_kbhit()` until a key is pressed. On Windows 10/11 a value is left over after a successful `CreateProcessA`, so it always gets stuck, and Steam keeps the game as running. On a thread started from `DllMain` (it runs after `DllMain` returns), MTFE terminates its parent process when it is `<game>\Launcher.exe` and older than the game.
