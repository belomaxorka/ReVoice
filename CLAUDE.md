# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ReVoice is a Metamod plugin for ReHLDS (Half-Life 1 / GoldSrc dedicated server) that fixes voice chat between Steam and non-Steam clients by transcoding voice data on the server. It builds a single shared library: `revoice_mm.dll` (Windows) or `revoice_mm_i386.so` (Linux, 32-bit). C++14 / C11, CMake ≥ 3.21.

This repo (`origin` = belomaxorka/ReVoice) is a fork reviving the abandoned upstream `rehlds/ReVoice` (remote `upstream` is already configured). There are no tests in the repo; verification is done by building and by running on a real ReHLDS server.

## Fork Workflow

Sync with upstream (in case they merge PRs or push new commits):

```powershell
git fetch upstream
git log --oneline master..upstream/master   # what's new upstream (empty = in sync)
git checkout master
git merge upstream/master
git push origin master
```

Avoid GitHub's "Sync fork" button when it offers "discard commits" — that deletes our local commits; merge locally instead.

Porting upstream PRs (established convention):
- Commit with the original author: `git commit --author="Name <email>"`.
- Reference the upstream PR in the commit message, e.g. `Ported from upstream PR rehlds/ReVoice#23`.
- Upstream PRs made before April 2025 target the old tree layout: `dep/opus` maps to `external/opus/src`, `dep/silk` to `external/silk/src`, etc. MSVC `.vcxproj` hunks are skipped entirely — this repo is CMake-only.
- Already ported: rehlds/ReVoice#23 (opus DTX/frame-size fixes) and #24 (per-client steamid, samplerate) by anzz1.

## Build Commands

Primary entry point — the build script (auto-detects compiler/generator, deletes the build dir unless `-k`):

```powershell
tools\windows\build.ps1                      # defaults: Release, auto-detect
tools\windows\build.ps1 -c msvc -g ninja -b Debug
tools\windows\build.ps1 -k                   # keep build dir (incremental)
tools\windows\build.ps1 -- -DENABLE_SPEEX_VORBIS_PSY=ON
```

On Linux: `tools/linux/build.sh` (same option style). A Dev Container config exists for Linux builds.

On this machine CMake/Ninja are not in PATH; they ship with VS 2026 (v18) BuildTools. Set up the environment first:

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation
$env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
tools\windows\build.ps1 -c msvc -g ninja
```

Direct CMake presets also work (`CMakePresets.json`): e.g. `cmake --preset ninja-msvc-windows && cmake --build --preset ninja-msvc-windows-release`.

Artifacts land in `bin/<CompilerId>-<Config>/` (e.g. `bin/MSVC-Release/revoice_mm.dll`). `tools/windows/gendebconf.ps1` generates Visual Studio debug config files (launch against a local HLDS install).

## Architecture

### Plugin lifecycle and hard dependencies

`meta_api.cpp` / `h_export.cpp` implement the Metamod plugin ABI. `Revoice_Load()` ([revoice_main.cpp](revoice/src/revoice_main.cpp)) initializes in order: ReHLDS API (`revoice_rehlds_api.cpp`), **Reunion API** (`revoice_reunion_api.cpp` — a hard requirement; the plugin refuses to load without the Reunion plugin present), cvars/config (`revoice_cfg.cpp`), players, then registers ReHLDS hookchains:

- `HandleNetCommand` — intercepts `clc_voicedata` (opcode 8) and replaces the engine's voice parsing with `SV_ParseVoiceData_emu` (the heart of the plugin).
- `SV_WriteVoiceCodec` — always announces `voice_speex` quality 5 to clients regardless of actual codec used.
- `SV_DropClient` — resets per-player state.

Metamod DLL hooks (`dllapi.cpp`): `ClientConnect` triggers codec detection, `CvarValue2` receives the async `sv_version` reply.

### Codec detection per player

`CRevoicePlayer` (one per slot, `g_Players[32]`, indexed by client id) starts with `REV_DefaultCodec` (speex). On connect, protocol-48 clients get an async `sv_version` cvar query; if the engine build number in the reply is > 4554 the player is switched to `vct_opus`. HLTV clients (detected via Reunion auth type) use `REV_HltvCodec`. Cvar listeners re-run detection when `REV_DefaultCodec`/`REV_HltvCodec` change at runtime.

### Voice transcoding pipeline

`SV_ParseVoiceData_emu` decodes the incoming packet with the **sender's** codec and produces two buffers — a "silk" one (steam-format) and a "speex" one — then broadcasts to each receiver whichever matches the receiver's codec type:

- silk sender → passed through as silk + transcoded to speex
- opus sender → passed through as silk buffer + decoded with opus codec, re-encoded to speex
- speex sender → passed through as speex + transcoded to silk

All codec instances belong to the sender (`srcPlayer->Get*Codec()`), so per-sender state (steamid in packet header, encoder state) is correct. Voice rate limiting (`MAX_*_DATA_LEN`, `MAX_*_VOICE_RATE` in [revoice_shared.h](revoice/src/revoice_shared.h)) silently drops flooding packets.

### Codec class stack

`IVoiceCodec` is the common interface. Two wrappers implement framing around raw encoders:

- `VoiceCodec_Frame` (wraps `VoiceEncoder_Speex`) — plain frame packetizer.
- `CSteamP2PCodec` (wraps `VoiceEncoder_Silk` for encode, `VoiceEncoder_Opus` for decode) — Steam P2P voice packet format: 8-byte fake steamid header (low part includes the sender's client id), payload opcodes (`PLT_SamplingRate`, `PLT_Silk`, `PLT_OPUS_PLC`, ...), and trailing crc32. Encode always emits `PLT_Silk` payloads at 8000 Hz.

All audio is 8 kHz mono 16-bit; opus `FRAME_SIZE` is 160 samples (320 bytes). The opus encoder uses DTX: `opus_encode` returning <2 bytes means the frame is dropped from the wire and the receiver conceals it via PLC using the sequence-number gap; `MAX_PACKET_LOSS` (10) is deliberately aligned with `MAX_CONSECUTIVE_DTX` in the vendored opus (`external/opus/src/silk/define.h`).

### Vendored dependencies

`external/` contains vendored, locally-patched libraries (speex, silk, opus, rehlsdk headers, metamod headers), each with its own CMakeLists. Local modifications to vendored code are expected (e.g. the `MAX_CONSECUTIVE_DTX` change) — don't blindly "upgrade" them.

### Conventions

- Every `.cpp` starts with `#include "precompiled.h"` (precompiled header, enforced by CMake).
- Runtime config: `data/revoice.cfg` (`REV_HltvCodec`, `REV_DefaultCodec`), executed on `ServerActivate`. Server console command: `rev version` / `rev status`.
- Version info is generated into `appversion.h` from git at configure time (`cmake/AppVersion.cmake`).
