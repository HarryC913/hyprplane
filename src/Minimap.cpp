// Minimap.cpp — the grab-pan minimap overlay and app-icon resolution. Split out of
// CanvasMode.cpp so the core canvas logic stays focused. Defines CCanvasMode::renderMinimap
// (a method may be defined in any translation unit) plus the icon cache it relies on.

#include "CanvasMode.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/gl/GLTexture.hpp>
#include <hyprland/src/helpers/Color.hpp>

#include <hyprgraphics/image/Image.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// --- tunables (Phase C will swap these for config reads) -------------------------------
namespace {
    constexpr uint32_t DRM_FMT_ARGB8888 = 0x34325241; // cairo ARGB32 in memory == DRM_FORMAT_ARGB8888

    constexpr double PANEL_W_FRAC  = 0.28; // panel width  as a fraction of the monitor
    constexpr double PANEL_H_FRAC  = 0.34; // panel height cap as a fraction of the monitor
    constexpr double PANEL_W_MAX   = 360.0;
    constexpr double MARGIN        = 24.0; // gap from the monitor's bottom-right corner
    constexpr double PAD           = 8.0;  // inner padding inside the panel
    constexpr int    PANEL_ROUND   = 10;
    constexpr float  PANEL_BG_A    = 0.55F;
    constexpr float  VIEWPORT_A    = 0.18F; // this monitor's viewport tint
    constexpr float  WINDOW_A      = 0.85F; // window plate
    constexpr double ICON_FRAC     = 0.55;  // icon size vs the smaller window-rect side
    constexpr double ICON_MIN      = 8.0;
    constexpr double ICON_MAX      = 32.0;
    constexpr float  ICON_A        = 0.95F;
    constexpr double ICON_LOAD_PX  = 64.0;  // SVG render size when loading icons
    constexpr double BADGE_FRAC    = 0.42;  // number-badge height vs the window-rect side
    constexpr double BADGE_MAX     = 11.0;
    constexpr double BADGE_W_RATIO = 0.6;   // digit width : height
    constexpr float  BADGE_BG_A    = 0.55F;
    constexpr float  NUM_A         = 0.95F;
}

// Per-app-class icon cache (texture, or null = looked up and none found — don't retry).
// File-static so it survives canvas toggles; built lazily inside the render pass.
static std::unordered_map<std::string, SP<Render::ITexture>> g_iconCache;

void clearIconCache() {
    g_iconCache.clear();
}

static std::string lower(std::string s) {
    for (auto& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

// A "core" key for a window class: strips the chromium PWA wrapper (chrome-<core>...-Default)
// and takes the token before the first . _ or space — e.g. "chrome-discord.com__x-Default" -> "discord".
static std::string classKey(const std::string& cls) {
    std::string k = lower(cls);
    for (const char* pre : {"chrome-", "chromium-", "msedge-", "brave-"})
        if (k.rfind(pre, 0) == 0) {
            k = k.substr(std::string(pre).size());
            break;
        }
    if (const auto p = k.rfind("-default"); p != std::string::npos && p == k.size() - 8)
        k = k.substr(0, p);
    return k.substr(0, k.find_first_of("._ "));
}

// Resolve a window class to a freedesktop icon NAME via .desktop files, reading Icon=. Tries,
// strongest first: StartupWMClass match, filename==class, filename==core key, then a fuzzy
// substring match on the core key (handles PWAs like Discord). Falls back to the class itself.
static std::string desktopIconName(const std::string& cls) {
    const std::string        lc  = lower(cls);
    const std::string        key = classKey(cls);
    std::vector<std::string> dirs;
    if (const char* h = getenv("HOME"))
        dirs.push_back(std::string(h) + "/.local/share/applications");
    dirs.push_back("/usr/share/applications");
    dirs.push_back("/usr/local/share/applications");

    std::string best;
    int         bestScore = 0;
    for (const auto& d : dirs) {
        std::error_code ec;
        if (!fs::is_directory(d, ec))
            continue;
        for (const auto& e : fs::directory_iterator(d, ec)) {
            if (e.path().extension() != ".desktop")
                continue;
            std::ifstream f(e.path());
            std::string   line, icon, wmclass;
            while (std::getline(f, line)) {
                if (line.rfind("Icon=", 0) == 0)
                    icon = line.substr(5);
                else if (line.rfind("StartupWMClass=", 0) == 0)
                    wmclass = line.substr(15);
            }
            if (icon.empty())
                continue;
            const std::string stem = lower(e.path().stem().string());

            int score = 0;
            if (!wmclass.empty() && lower(wmclass) == lc)
                score = 100;
            else if (stem == lc)
                score = 80;
            else if (!key.empty() && key.size() >= 3 && stem == key)
                score = 60;
            else if (!key.empty() && key.size() >= 3 && (stem.find(key) != std::string::npos || key.find(stem) != std::string::npos))
                score = 40;

            if (score > bestScore) {
                bestScore = score;
                best      = icon;
                if (score == 100)
                    return best;
            }
        }
    }
    return best.empty() ? cls : best;
}

// Resolve an icon name to a file via the standard hicolor / pixmaps locations (direct path
// probes — fast, no recursive theme scan). Prefers SVG and larger sizes.
static std::string findIconFile(const std::string& name) {
    if (name.empty())
        return "";
    std::error_code ec;
    if (name.front() == '/' && fs::exists(name, ec))
        return name;

    std::vector<std::string> roots;
    if (const char* h = getenv("HOME"))
        roots.push_back(std::string(h) + "/.local/share/icons/hicolor");
    roots.push_back("/usr/share/icons/hicolor");

    static const char* SIZES[] = {"scalable", "512x512", "256x256", "128x128", "96x96", "64x64", "48x48", "32x32"};
    for (const auto& root : roots)
        for (const char* sz : SIZES)
            for (const char* ext : {"svg", "png"}) {
                const std::string p = root + "/" + sz + "/apps/" + name + "." + ext;
                if (fs::exists(p, ec))
                    return p;
            }
    for (const char* ext : {"svg", "png"}) {
        const std::string p = std::string("/usr/share/pixmaps/") + name + "." + ext;
        if (fs::exists(p, ec))
            return p;
    }
    return "";
}

// class -> GL texture, cached. Must be called with the GL context live (render pass).
static SP<Render::ITexture> iconTexture(const std::string& cls) {
    if (cls.empty())
        return nullptr;
    if (const auto it = g_iconCache.find(cls); it != g_iconCache.end())
        return it->second;

    SP<Render::ITexture> tex;
    const std::string    path = findIconFile(desktopIconName(cls));
    if (!path.empty()) {
        Hyprgraphics::CImage img(path, {ICON_LOAD_PX, ICON_LOAD_PX});
        if (img.success())
            if (const auto surf = img.cairoSurface()) {
                cairo_surface_flush(surf->cairo());
                tex = makeShared<Render::GL::CGLTexture>(DRM_FMT_ARGB8888, surf->data(), (uint32_t)surf->stride(),
                                                         surf->size(), true);
            }
    }
    g_iconCache[cls] = tex; // cache even on failure so we don't re-probe every frame
    return tex;
}

void CCanvasMode::renderMinimap(const PHLMONITOR& mon, float alpha, const CHyprColor& accent) {
    if (!mon || m_canvasWorkspaces.empty() || alpha <= 0.01F)
        return;

    // Draw via render-pass elements (immediate-mode renderRect doesn't composite in 0.55).
    const auto addRect = [](const CBox& b, const CHyprColor& c, int round) {
        CRectPassElement::SRectData d;
        d.box   = b;
        d.color = c;
        d.round = round;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(d));
    };

    // A SUPER+number label drawn as a 7-segment digit out of small rects (no text textures).
    const auto drawDigit = [&](int digit, const CBox& b, const CHyprColor& col) {
        if (digit < 1 || digit > 9)
            return;
        static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
        const uint8_t       s  = SEG[digit];
        const double        X = b.x, Y = b.y, W = b.width, H = b.height;
        const double        t  = std::max(1.0, std::min(W, H) * 0.22);
        const double        hh = (H - t) / 2.0;
        if (s & 0x01) addRect(CBox(X, Y, W, t), col, 0);             // a  top
        if (s & 0x02) addRect(CBox(X + W - t, Y, t, hh + t), col, 0); // b  top-right
        if (s & 0x04) addRect(CBox(X + W - t, Y + hh, t, hh + t), col, 0); // c  bottom-right
        if (s & 0x08) addRect(CBox(X, Y + H - t, W, t), col, 0);     // d  bottom
        if (s & 0x10) addRect(CBox(X, Y + hh, t, hh + t), col, 0);    // e  bottom-left
        if (s & 0x20) addRect(CBox(X, Y, t, hh + t), col, 0);        // f  top-left
        if (s & 0x40) addRect(CBox(X, Y + hh, W, t), col, 0);        // g  middle
    };

    // A texture pass element (the app icon).
    const auto addTex = [](const SP<Render::ITexture>& t, const CBox& b, float a) {
        CTexPassElement::SRenderData d;
        d.tex = t;
        d.box = b;
        d.a   = a;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(d));
    };

    // Whole-canvas bounding box: every canvas window + every active viewport, in global coords.
    struct SRect {
        Vector2D    pos, size;
        std::string cls;
    };
    std::vector<SRect> wins;
    Vector2D           bbMin(1e9, 1e9), bbMax(-1e9, -1e9);
    const auto         grow = [&](const Vector2D& p, const Vector2D& s) {
        bbMin.x = std::min(bbMin.x, p.x);
        bbMin.y = std::min(bbMin.y, p.y);
        bbMax.x = std::max(bbMax.x, p.x + s.x);
        bbMax.y = std::max(bbMax.y, p.y + s.y);
    };

    for (const auto& w : g_pCompositor->m_windows) {
        if (!w || !w->m_isMapped || !w->m_workspace || !m_canvasWorkspaces.contains(w->m_workspace->m_id))
            continue;
        const Vector2D p = w->m_realPosition->value(), s = w->m_realSize->value();
        wins.push_back({p, s, w->m_class.empty() ? w->m_initialClass : w->m_class});
        grow(p, s);
    }
    for (const auto& M : g_pCompositor->m_monitors)
        if (M && isCanvas(M->m_activeWorkspace))
            grow(M->m_position, M->m_size);

    if (wins.empty())
        return;
    const Vector2D bb = bbMax - bbMin;
    if (bb.x < 1.0 || bb.y < 1.0)
        return;

    // Largest-area first so the labels match the SUPER+number (canvas:jump) ordering.
    std::sort(wins.begin(), wins.end(),
              [](const SRect& a, const SRect& b) { return a.size.x * a.size.y > b.size.x * b.size.y; });

    // Theme: accent (active border) for windows/viewport; a number colour that contrasts it.
    const double     lum    = 0.3 * accent.r + 0.6 * accent.g + 0.1 * accent.b;
    const CHyprColor numCol = lum > 0.6 ? CHyprColor(0.06, 0.06, 0.08, NUM_A * alpha) : CHyprColor(0.96, 0.97, 1.0, NUM_A * alpha);
    const CHyprColor winCol(accent.r, accent.g, accent.b, WINDOW_A * alpha);
    const CHyprColor vpCol(accent.r, accent.g, accent.b, VIEWPORT_A * alpha);

    // Panel anchored bottom-right (monitor-local logical coords), sized to the canvas aspect.
    double pw = std::min(PANEL_W_MAX, mon->m_size.x * PANEL_W_FRAC);
    double ph = pw * (bb.y / bb.x);
    if (ph > mon->m_size.y * PANEL_H_FRAC) {
        ph = mon->m_size.y * PANEL_H_FRAC;
        pw = ph * (bb.x / bb.y);
    }
    const CBox panel(mon->m_size.x - pw - MARGIN, mon->m_size.y - ph - MARGIN, pw, ph);
    addRect(panel, CHyprColor(0.0, 0.0, 0.0, PANEL_BG_A * alpha), PANEL_ROUND);

    const double   sc      = std::min((pw - 2 * PAD) / bb.x, (ph - 2 * PAD) / bb.y);
    const Vector2D origin(panel.x + PAD, panel.y + PAD);
    const auto     toPanel = [&](const Vector2D& g) { return origin + (g - bbMin) * sc; };

    // This monitor's viewport behind the windows (faint accent fill).
    const Vector2D vp = toPanel(mon->m_position);
    addRect(CBox(vp.x, vp.y, mon->m_size.x * sc, mon->m_size.y * sc), vpCol, 2);

    // Windows on top: a subtle plate, the app icon (so you can tell which is which), and the
    // SUPER+number badge in the corner.
    for (size_t i = 0; i < wins.size(); ++i) {
        const Vector2D tp = toPanel(wins[i].pos);
        const double   rw = std::max(2.0, wins[i].size.x * sc), rh = std::max(2.0, wins[i].size.y * sc);
        addRect(CBox(tp.x, tp.y, rw, rh), winCol, 2);

        // App icon, centred, aspect-preserved, modest size (capped so big windows don't get huge).
        if (const auto tex = iconTexture(wins[i].cls)) {
            const double box = std::clamp(std::min(rw, rh) * ICON_FRAC, ICON_MIN, ICON_MAX);
            if (rw >= box && rh >= box) {
                double iw = tex->m_size.x, ih = tex->m_size.y;
                if (iw < 1.0 || ih < 1.0)
                    iw = ih = 1.0;
                const double k = box / std::max(iw, ih); // fit the longer side, keep aspect
                const double dw = iw * k, dh = ih * k;
                addTex(tex, CBox(tp.x + (rw - dw) / 2.0, tp.y + (rh - dh) / 2.0, dw, dh), ICON_A * alpha);
            }
        }

        // SUPER+number badge: small, top-left, on a dark chip so it reads over any icon.
        const double dh = std::min({rh * BADGE_FRAC, rw * BADGE_FRAC, BADGE_MAX});
        const double dw = dh * BADGE_W_RATIO;
        if (i < 9 && dh >= 5.0 && rw > dw + 4.0 && rh > dh + 4.0) {
            const double bx = tp.x + 1.5, by = tp.y + 1.5;
            addRect(CBox(bx - 0.5, by - 0.5, dw + 3.0, dh + 3.0), CHyprColor(0.0, 0.0, 0.0, BADGE_BG_A * alpha), 2);
            drawDigit((int)i + 1, CBox(bx + 1.0, by + 1.0, dw, dh), numCol);
        }
    }
}
