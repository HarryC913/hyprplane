<div align="center">

# 🪟 hyprplane

**Turn any Hyprland workspace into an infinite, pannable canvas.**

[![Hyprland 0.55.x](https://img.shields.io/badge/Hyprland-0.55.x-1aa3ff)](https://hypr.land)
[![C++26](https://img.shields.io/badge/C%2B%2B-26-00599C?logo=cplusplus&logoColor=white)](#requirements)
[![License: BSD-3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue)](LICENSE)
![Status: active dev](https://img.shields.io/badge/status-active%20dev-success)
![Written by an AI](https://img.shields.io/badge/written%20by-an%20AI-ff69b4)
[![Stars](https://img.shields.io/github/stars/HarryC913/hyprplane?style=social)](https://github.com/HarryC913/hyprplane/stargazers)

<!-- 📹 DEMO: record a clip (toggle → drag windows → overview → SUPER+number → minimap) and save it to assets/demo.gif, then swap the placeholder below for: <img src="assets/demo.gif" width="820"> -->
<img src="https://placehold.co/820x440/1e1e2e/89b4fa?text=hyprplane+demo+%E2%80%94+coming+soon" alt="hyprplane demo" width="820">

<sub>↑ placeholder — real demo GIF coming soon (`assets/demo.gif`)</sub>

</div>

---

Toggle it on and your windows float on an endless plane you can grab and drag around — across every monitor at once — then toggle off to snap straight back to your normal tiling layout. **Nothing is destroyed. Everything is remembered.**

Unlike generic Wayland canvas tools, hyprplane hooks directly into Hyprland's C++ plugin API for native, compositor-level window manipulation — real position moves, not overlay hacks. The companion overview uses Hyprland's `keyword monitor` path for crisp compositor downscaling, not a fuzzy render-modifier pass.

> **Ever wanted to make Hyprland *less* efficient?** This is the plugin for you. Want your compositor to render the same windows at arbitrary positions on a conceptual plane, wasting CPU cycles just so you can drag them around like a mood board? Want a minimap that draws tiny 7-segment digits in the render pass and resolves `.desktop` icons at runtime mid-frame? We've got you.
>
> *(This plugin was written by an AI. The AI apologises. It tried its best. The minimap icon resolver is — genuinely — the most unhinged thing the AI came up with all month. It works, though.)*

## Contents

- [Features](#features) · [Requirements](#requirements) · [Build](#build) · [Install](#install) · [Controls](#controls-default-binds) · [Configuration](#configuration) · [How it works](#how-it-works) · [Roadmap](#roadmap) · [FAQ](#faq) · [Contributing](#contributing) · [License](#license)

## Features

- **Toggle canvas mode** per keybind — non-destructive: flip it off and your exact tiling layout returns.
- **Grab-to-pan** — middle-mouse drag (or `Ctrl`+left drag) moves the whole canvas 1:1 with your cursor. Keyboard panning too.
- **All monitors at once** — toggling and panning acts on every monitor's active workspace simultaneously.
- **Layout persistence** — windows remember where you left them on the canvas across off/on toggles. New windows join automatically; closed ones are forgotten. Named layouts can be saved to disk.
- **Full-display overview** — zoom every monitor out via its scale (crisp compositor downscale, real resolution — not a render hack) so the whole canvas fits as one continuous space, then toggle back. Rotation-aware, spans all monitors.
- **DPI-block** — apps (Steam, GTK, Qt) keep rendering at native resolution during overview, so there's no UI reflow even when the monitor scale drops.
- **SUPER+number jump** — in canvas mode, `SUPER+1..0` *smoothly slide* the canvas to centre the Nᵗʰ window. Outside canvas mode they fall through to normal workspace switching.
- **Grab-pan minimap** — an overlay showing every canvas window's position, with **app icons** and 7-segment number badges, fading in while you drag and out when idle. Completely unnecessary. We included it anyway.

## Requirements

- Hyprland **0.55.x** (the plugin is pinned to the running ABI via the API hash).
- Hyprland development headers (`hyprland` pkg-config), plus `hyprgraphics`, `pixman-1`, `libdrm`, `pangocairo`, `cairo` — these come with the Hyprland dev package on most distros.
- A C++26 compiler (GCC 14+/Clang 18+) and CMake ≥ 3.21.

## Build

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# -> build/hyprplane.so
```

## Install

### Option A: `hyprpm` (recommended)

```sh
hyprpm add https://github.com/HarryC913/hyprplane
hyprpm enable hyprplane
```

### Option B: Manual

1. Copy [`canvas.conf.example`](canvas.conf.example) to `~/.config/hypr/canvas.conf` and edit the plugin path to point at your built `build/hyprplane.so`.
2. Source it at the end of `~/.config/hypr/hyprland.conf`:
   ```
   source = ~/.config/hypr/canvas.conf
   ```
3. Reload Hyprland.

Or load it ad-hoc:

```sh
hyprctl plugin load "$PWD/build/hyprplane.so"
```

## Controls (default binds)

| Action | Binding |
| --- | --- |
| Toggle canvas mode (all monitors) | `SUPER` + `` ` `` |
| Pan the canvas | middle-mouse drag, or `Ctrl` + left-drag |
| Pan (keyboard) | `SUPER`+`Ctrl`+`J` / `K` |
| Toggle full-display overview | `SUPER`+`Ctrl`+`O` |
| Jump to Nᵗʰ window (smooth slide) | `SUPER`+`1` through `0` (canvas mode only) |

## Configuration

Optional tunables go in a `plugin { hyprplane { … } }` block (see [`canvas.conf.example`](canvas.conf.example)). Defaults shown; change and `hyprctl reload` to apply:

| Key | Default | Meaning |
| --- | --- | --- |
| `minimap` | `1` | `0` disables the grab-pan minimap |
| `minimap_size` | `0.28` | panel width as a fraction of the monitor (0.1–0.6) |
| `minimap_position` | `0` | `0` bottom-right, `1` bottom-left, `2` top-right, `3` top-left |
| `fade_speed` | `0.18` | minimap fade easing (higher = snappier) |
| `grab_button` | `2` | pan trigger: `0` middle, `1` ctrl+left, `2` both |
| `jump_order` | `0` | `SUPER`+number order: `0` largest, `1` left-to-right, `2` oldest |
| `spawn_center` | `1` | new windows open at the view centre (best-effort); `0` = natural spot |

### Window-management dispatchers (canvas mode only)

| Dispatcher | Action |
| --- | --- |
| `canvas:gather` | pull the focused monitor's windows back into the current view |
| `canvas:savelayout <name>` | save the current arrangement to `$XDG_STATE_HOME/hyprplane/<name>.layout` |
| `canvas:loadlayout <name>` | restore a saved arrangement (matches windows by class + title) |

## How it works

Canvas windows are set floating so the tiling engine stops repositioning them; panning then moves their real positions (so render *and* input stay in sync). Toggling off restores the saved floating-state and re-tiles. Design notes, dead-ends, and Hyprland-internals gotchas are documented in [FINDINGS.md](FINDINGS.md).

## Roadmap

- [x] Non-destructive canvas toggle + 1:1 grab-pan
- [x] Multi-monitor, layout persistence, save/load named layouts
- [x] Full-display overview (spanning) + DPI-block
- [x] SUPER+number smooth-slide jump
- [x] Grab-pan minimap with app icons
- [x] Config tunables (`plugin { hyprplane { … } }`)
- [ ] Demo GIF 🙃
- [ ] Minimap click-to-jump
- [ ] Precise spawn-at-centre
- [ ] Smooth animated zoom *(blocked — Hyprland can't animate monitor scale)*

## FAQ

**Q: Won't the AI just hallucinate bugs?**
A: Look at the commit history. This plugin survived Approach A (a whole dead-end with `m_renderOffset` that Hyprland fights back to zero), Approach B (real position moves), a `makeSnapshot` crash, and a `pkill -9 grim` screencopy wedge. The AI learned these the hard way. It is, arguably, unwell.

**Q: Is this actually useful?**
A: If you have a lot of windows and want to spread them out spatially instead of switching workspaces, yes. If you want to make your compositor do more work for the same result, also yes. We support both journeys.

**Q: Why not just use a tiling layout?**
A: The tiling layout doesn't let you drag your entire desktop sideways to reveal more desktop. hyprplane does. It's a completely different (and worse) paradigm. You'll love it.

## Contributing

Issues and PRs welcome. It builds with a plain `cmake --build build`; for live iteration, load from a unique path each time (Hyprland's `dlopen` caches by path) — see [FINDINGS.md](FINDINGS.md) for the dev loop and the Hyprland-internals notes that took real time to learn.

## Star History

<a href="https://star-history.com/#HarryC913/hyprplane&Date">
  <img src="https://api.star-history.com/svg?repos=HarryC913/hyprplane&type=Date" alt="Star History Chart" width="600">
</a>

## License

BSD 3-Clause — see [LICENSE](LICENSE).

<div align="center"><sub>Built with Hyprland's plugin API, far too many render-pass experiments, and one (1) apologetic AI.</sub></div>
