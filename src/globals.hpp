#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>

// The plugin handle, set in pluginInit. Used by every HyprlandAPI:: call.
inline HANDLE PHANDLE = nullptr;

// --- config access ----------------------------------------------------------------------
// Read a registered plugin config value. Works for simple INT/FLOAT (the CUSTOM gradient type
// returns garbage via this path — see themeAccent() in main.cpp for that). Reads live, so a
// `hyprctl reload` takes effect immediately. Falls back to `def` if unset/missing.
inline Hyprlang::INT cfgInt(const char* name, Hyprlang::INT def) {
    const auto v = HyprlandAPI::getConfigValue(PHANDLE, name);
    if (!v)
        return def;
    const auto p = v->getDataStaticPtr();
    return (p && *p) ? **(Hyprlang::INT* const*)p : def;
}
inline Hyprlang::FLOAT cfgFloat(const char* name, Hyprlang::FLOAT def) {
    const auto v = HyprlandAPI::getConfigValue(PHANDLE, name);
    if (!v)
        return def;
    const auto p = v->getDataStaticPtr();
    return (p && *p) ? **(Hyprlang::FLOAT* const*)p : def;
}

// Named accessors — single source of truth for config keys + defaults. Defaults here MUST match
// the defaults registered via addConfigValue in registerConfig() (main.cpp).
namespace cfg {
    inline bool  minimap()     { return cfgInt("plugin:hyprplane:minimap", 1) != 0; }
    inline float minimapSize() { return (float)cfgFloat("plugin:hyprplane:minimap_size", 0.28F); }
    inline int   minimapPos()  { return (int)cfgInt("plugin:hyprplane:minimap_position", 0); } // 0 BR,1 BL,2 TR,3 TL
    inline float fadeSpeed()   { return (float)cfgFloat("plugin:hyprplane:fade_speed", 0.18F); }
    inline int   grabButton()   { return (int)cfgInt("plugin:hyprplane:grab_button", 2); }       // 0 mid,1 ctrl-left,2 both
    inline int   jumpOrder()    { return (int)cfgInt("plugin:hyprplane:jump_order", 0); }         // 0 size,1 spatial,2 age
    inline bool  spawnCenter()  { return cfgInt("plugin:hyprplane:spawn_center", 1) != 0; }       // new windows at view centre
}
