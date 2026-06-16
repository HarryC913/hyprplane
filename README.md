# canvasinfinite

A Hyprland plugin that turns a workspace into a pannable **infinite 2D canvas**. Toggle it
on and your windows float on an endless plane you can grab and drag around — across both
monitors at once — then toggle off to drop straight back into your normal tiling layout.

> Status: **working & in active development.** Toggle, grab-pan, multi-monitor, and layout
> persistence are solid. Zoom-out is experimental and currently disabled (see
> [FINDINGS.md](FINDINGS.md)). Built and tested against **Hyprland 0.55.x**.

## Features

- **Toggle canvas mode** per keybind — non-destructive: flip it off and your exact tiling
  layout returns.
- **Grab-to-pan** — middle-mouse drag (or `Ctrl`+left drag) moves the whole canvas 1:1 with
  your cursor. Keyboard panning too.
- **Both monitors at once** — toggling/panning acts on every monitor's active workspace.
- **Layout persistence** — windows remember where you left them on the canvas across
  off/on toggles. New windows join the canvas; closed windows are forgotten automatically.

## Requirements

- Hyprland **0.55.x** (the plugin is pinned to the running ABI via the API hash).
- Hyprland development headers (`hyprland` pkg-config), plus `pixman-1`, `libdrm`,
  `pangocairo` — these come with the Hyprland dev package on most distros.
- A C++26 compiler (GCC 14+/Clang 18+) and CMake ≥ 3.21.

## Build

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# -> build/canvasinfinite.so
```

## Install / use

1. Copy [`canvas.conf.example`](canvas.conf.example) to `~/.config/hypr/canvas.conf` and edit
   the plugin path to point at your built `build/canvasinfinite.so`.
2. Source it at the end of `~/.config/hypr/hyprland.conf`:
   ```
   source = ~/.config/hypr/canvas.conf
   ```
3. Reload Hyprland (or relog).

Or load it ad-hoc without touching your config:

```sh
hyprctl plugin load "$PWD/build/canvasinfinite.so"
```

### Controls (default binds)

| Action | Binding |
| --- | --- |
| Toggle canvas mode (all monitors) | `SUPER` + `` ` `` |
| Pan the canvas | middle-mouse drag, or `Ctrl` + left-drag |
| Pan (keyboard) | `SUPER`+`Ctrl`+`J` / `K` |

## How it works

Canvas windows are set floating so the tiling engine stops repositioning them; panning then
moves their real positions (so render *and* input stay in sync). Toggling off restores the
saved floating-state and re-tiles. Design notes, dead-ends, and Hyprland-internals gotchas
are documented in [FINDINGS.md](FINDINGS.md).

## Status / roadmap

- ✅ Toggle, grab-pan, multi-monitor, layout persistence
- 🚧 Zoom-out overview — renders but isn't yet crash-safe from a plugin render hook; disabled
  behind a compile flag (`ENABLE_ZOOM`) pending a safe approach. Details in FINDINGS.md.

## License

BSD 3-Clause — see [LICENSE](LICENSE).
