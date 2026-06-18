# hyprplane — roadmap / TODO

Done so far: toggle, grab-pan (1:1), multi-monitor, layout persistence across toggles,
full-display overview (companion script, always steps back to a visible zoom, spans all
monitors as one continuous canvas via seam-anchored repositioning), DPI-block (apps keep
native resolution during overview — no reflow), overview gated on canvas mode,
SUPER+number jumps to the Nth-largest window in canvas mode (workspace switch otherwise),
grab-pan minimap with app icon resolution, public repo.

## Features / ideas

- [x] ~~**Smooth zoom animation.**~~ **Shelved — compositor-blocked.** Hyprland can't animate
  monitor scale (instant output reconfigure). A render-transform tween can't substitute: it
  only scales the composited framebuffer, so it can't reveal the off-screen windows the
  overview exists to show, and pops when handing off to the real scale. The least-bad trick
  (apply real scale, then animate a counter-magnify away) is undercut on multi-monitor by the
  wallpaper-reanchor (forcerendererreload) flicker. Decision: keep the instant snap (crisp).

## Sharing / polish

- [ ] `hyprpm.toml` so users can `hyprpm add` this repo.
- [ ] GitHub topics (hyprland, wayland, hyprland-plugin) for discoverability.
- [ ] Demo GIF in the README.
