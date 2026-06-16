#include "globals.hpp"
#include "CanvasMode.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/SharedDefs.hpp>

// Kept alive for the life of the plugin; resetting them unregisters the hooks.
static CHyprSignalListener g_buttonListener;
static CHyprSignalListener g_moveListener;
static CHyprSignalListener g_axisListener;
static CHyprSignalListener g_openListener;
static CHyprSignalListener g_renderListener;
static CHyprSignalListener g_renderStageListener;
static CFunctionHook*      g_renderWindowHook = nullptr;

// Grab-to-pan state (middle-mouse or Ctrl+left drag).
static bool     g_grabbing = false;
static Vector2D g_grabLast;

static constexpr uint32_t BTN_LEFT_ID   = 0x110;
static constexpr uint32_t BTN_MIDDLE_ID = 0x112;
static constexpr float    ZOOM_STEP     = 0.9F; // per scroll tick (zoom out); reciprocal zooms in

// Zoom (snapshot-based render scaling) CRASHES Hyprland: makeSnapshot() does nested
// rendering inside the renderWindow hook and corrupts render state. Disabled until a
// safe scaling path exists. Toggle/pan/both-monitors are unaffected and stable.
static constexpr bool     ENABLE_ZOOM   = false;

// Render-only zoom: hook renderWindow and, for canvas windows while zoomed out,
// temporarily set the window's render geometry to a scaled value (around the
// monitor centre), render, then restore. No real resize — input/layout untouched.
using PRENDERWINDOW = void (*)(void*, PHLWINDOW, PHLMONITOR, const Time::steady_tp&, bool, Render::eRenderPassMode, bool, bool);

static bool g_inSnapshot = false; // recursion guard: makeSnapshot re-enters renderWindow
static bool g_snapValid  = false; // snapshots captured for this zoom session? (re-snap only on (re)entry)

static void hkRenderWindow(void* thisptr, PHLWINDOW w, PHLMONITOR mon, const Time::steady_tp& time, bool b, Render::eRenderPassMode mode,
                           bool ignorePos, bool standalone) {
    const auto  orig = reinterpret_cast<PRENDERWINDOW>(g_renderWindowHook->m_original);
    const float z    = g_canvas ? g_canvas->zoom() : 1.0F;

    const bool scaling = !g_inSnapshot && z < 1.0F && w && mon && w->m_isMapped && w->m_realPosition && w->m_realSize &&
        w->m_workspace && g_canvas->isCanvas(w->m_workspace);
    if (!scaling) {
        orig(thisptr, w, mon, time, b, mode, ignorePos, standalone);
        return;
    }

    // Capture the live window into its snapshot ONCE per zoom session — doing this
    // every frame (continuous nested rendering) crashes Hyprland. Recursion-guarded.
    if (!g_snapValid) {
        g_inSnapshot = true;
        g_pHyprRenderer->makeSnapshot(w);
        g_inSnapshot = false;
    }

    // Draw that snapshot texture at a scaled box (texture->box scales cleanly, no crop).
    const Vector2D C  = mon->m_position + mon->m_size / 2.0;
    Vector2D&      rp = w->m_realPosition->value();
    Vector2D&      rs = w->m_realSize->value();
    const Vector2D op = rp, os = rs;
    rp = C + (op - C) * z;
    rs = os * z;
    g_pHyprRenderer->renderSnapshot(w);
    rp = op;
    rs = os;
}

static bool modHeld(uint32_t bit) {
    const auto KB = g_pSeatManager ? g_pSeatManager->m_keyboard.lock() : nullptr;
    return KB && (KB->getModifiers() & bit);
}
static bool ctrlHeld() {
    return modHeld(4u); // CTRL bit (same convention as binds)
}
static bool superHeld() {
    return modHeld(64u); // SUPER/LOGO bit
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

// hyprctl dlsym's this from the .so and refuses to load if it doesn't match the
// running Hyprland's hash. Returns the composite hash of the headers we built
// against (commit + aquamarine/hyprutils/... versions).
APICALL EXPORT const char* __hyprland_api_get_hash() {
    return __hyprland_api_get_client_hash();
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO pluginInit(HANDLE handle) {
    PHANDLE   = handle;
    g_canvas  = makeUnique<CCanvasMode>();

    // --- the master switch -------------------------------------------------
    HyprlandAPI::addDispatcherV2(PHANDLE, "canvas:toggle", [](std::string) -> SDispatchResult {
        g_canvas->toggleAllMonitors();
        g_snapValid = false; // fresh snapshots next zoom session
        HyprlandAPI::addNotification(PHANDLE, g_canvas->anyActive() ? "[canvas] ON" : "[canvas] OFF",
                                     CHyprColor{0.2F, 0.8F, 1.0F, 1.0F}, 1500);
        return {.success = true, .error = ""};
    });

    // --- keyboard / programmatic pan (Phase 2a), GATED on canvas mode ------
    // Binds call e.g.  canvas:pan 0 -200 . Also used to verify panning headless.
    HyprlandAPI::addDispatcherV2(PHANDLE, "canvas:pan", [](std::string arg) -> SDispatchResult {
        if (!g_canvas->anyActive())
            return {.success = false, .error = "canvas: not in canvas mode"};

        const auto SP = arg.find(' ');
        if (SP == std::string::npos)
            return {.success = false, .error = "canvas:pan expects: <dx> <dy>"};
        try {
            g_canvas->panAllActive(Vector2D{std::stod(arg.substr(0, SP)), std::stod(arg.substr(SP + 1))});
        } catch (...) { return {.success = false, .error = "canvas:pan: bad args"}; }
        return {.success = true, .error = ""};
    });

    // --- zoom dispatcher (keyboard + headless testing) ---------------------
    if (ENABLE_ZOOM)
    HyprlandAPI::addDispatcherV2(PHANDLE, "canvas:zoom", [](std::string arg) -> SDispatchResult {
        if (!g_canvas->anyActive())
            return {.success = false, .error = "canvas: not in canvas mode"};
        float f = ZOOM_STEP;
        try {
            f = std::stof(arg);
        } catch (...) {}
        g_canvas->zoomBy(f);
        if (g_canvas->zoom() >= 1.0F)
            g_snapValid = false;
        for (const auto& mon : g_pCompositor->m_monitors) {
            if (!mon)
                continue;
            g_pHyprRenderer->damageMonitor(mon);
            g_pCompositor->scheduleFrameForMonitor(mon);
        }
        return {.success = true, .error = ""};
    });

    // --- grab-to-pan: middle-mouse OR Ctrl+left drag, GATED on canvas mode --
    // Press starts a grab; motion pans the canvas so it follows the cursor (like
    // dragging a map). Only active in canvas mode, so normal clicks are untouched.
    g_buttonListener = Event::bus()->m_events.input.mouse.button.listen(
        [](IPointer::SButtonEvent e, Event::SCallbackInfo& info) {
            if (!g_canvas->anyActive())
                return; // inert outside canvas mode

            const bool panBtn = e.button == BTN_MIDDLE_ID || (e.button == BTN_LEFT_ID && ctrlHeld());
            if (e.state == WL_POINTER_BUTTON_STATE_PRESSED) {
                if (!panBtn)
                    return;
                g_grabbing     = true;
                g_grabLast     = g_pInputManager->getMouseCoordsInternal();
                info.cancelled = true; // swallow press (no middle-paste / ctrl-click select)
            } else if (g_grabbing) {   // release ends the grab
                g_grabbing     = false;
                info.cancelled = true;
            }
        });

    g_moveListener = Event::bus()->m_events.input.mouse.move.listen(
        [](Vector2D, Event::SCallbackInfo& info) {
            if (!g_grabbing)
                return; // don't cancel — let the cursor move so we can track it

            const Vector2D now   = g_pInputManager->getMouseCoordsInternal();
            const Vector2D delta = now - g_grabLast;
            g_grabLast           = now;

            if (g_canvas->anyActive())
                g_canvas->panAllActive(delta); // both monitors pan together
            else
                g_grabbing = false;            // canvas turned off mid-drag
        });

    // A window opened while canvas is on → float it so it joins the canvas.
    g_openListener = Event::bus()->m_events.window.open.listen([](const PHLWINDOW& w) {
        if (g_canvas)
            g_canvas->onWindowOpened(w);
    });

    // --- SUPER + scroll = zoom OUT (canvas mode only) ----------------------
    // Adjusts the canvas viewport zoom (render-only, applied in the render hook
    // below). Capped at 1.0 — never magnifies past native. Only consumes
    // SUPER+scroll in canvas mode; normal SUPER+scroll workspace cycling preserved.
    if (ENABLE_ZOOM)
    g_axisListener = Event::bus()->m_events.input.mouse.axis.listen(
        [](IPointer::SAxisEvent e, Event::SCallbackInfo& info) {
            if (!g_canvas->anyActive() || !superHeld())
                return;
            if (e.axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
                return;

            g_canvas->zoomBy(e.delta > 0 ? ZOOM_STEP : 1.0F / ZOOM_STEP); // scroll down = out, up = in
            if (g_canvas->zoom() >= 1.0F)
                g_snapValid = false; // back to native -> refresh snapshots next zoom-out
            for (const auto& mon : g_pCompositor->m_monitors) {
                if (!mon)
                    continue;
                g_pHyprRenderer->damageMonitor(mon);
                g_pCompositor->scheduleFrameForMonitor(mon);
            }
            info.cancelled = true; // don't cycle workspaces
        });

    // --- render hook: scale canvas windows down while zoomed out -----------
    if (ENABLE_ZOOM) {
    for (const auto& m : HyprlandAPI::findFunctionsByName(PHANDLE, "renderWindow")) {
        if (m.demangled.find("IHyprRenderer::renderWindow") != std::string::npos) {
            g_renderWindowHook = HyprlandAPI::createFunctionHook(PHANDLE, m.address, reinterpret_cast<void*>(&hkRenderWindow));
            break;
        }
    }
    if (g_renderWindowHook)
        g_renderWindowHook->hook();
    else
        HyprlandAPI::addNotification(PHANDLE, "[canvas] zoom hook not found", CHyprColor{1.0F, 0.3F, 0.3F, 1.0F}, 6000);

    // While zoomed out, fully redraw each monitor every frame (the scaled snapshots
    // are drawn fresh each frame, so the un-shrunk areas must be redrawn -> no trails).
    g_renderListener = Event::bus()->m_events.render.pre.listen([](PHLMONITOR mon) {
        if (mon && g_canvas->anyActive() && g_canvas->zoom() < 1.0F)
            g_pHyprRenderer->damageMonitor(mon);
    });

    // After a frame completes, mark snapshots captured so we stop re-snapshotting.
    g_renderStageListener = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        if (stage == RENDER_POST && g_canvas->anyActive() && g_canvas->zoom() < 1.0F)
            g_snapValid = true;
    });
    } // ENABLE_ZOOM

    HyprlandAPI::addNotification(PHANDLE, "[canvasinfinite] loaded — toggle: SUPER+grave, middle/Ctrl-drag to pan",
                                 CHyprColor{0.2F, 1.0F, 0.4F, 1.0F}, 4000);

    return {"canvasinfinite", "Infinite scroll canvas (Phase 0 proof)", "hcopeland", "0.0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_canvas && g_canvas->anyActive())
        g_canvas->toggleAllMonitors(); // un-float / re-tile windows so unload leaves a clean layout
    if (g_renderWindowHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, g_renderWindowHook);
    g_buttonListener.reset();
    g_moveListener.reset();
    g_axisListener.reset();
    g_openListener.reset();
    g_renderListener.reset();
    g_renderStageListener.reset();
    g_canvas.reset();
    for (const auto& mon : g_pCompositor->m_monitors)
        if (mon)
            g_pHyprRenderer->damageMonitor(mon); // repaint at native after unload
}
