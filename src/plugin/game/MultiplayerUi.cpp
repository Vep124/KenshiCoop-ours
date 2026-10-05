// MultiplayerUi.cpp - the title-screen MULTIPLAYER button + its window:
// Steam server browser and LAN direct-IP form. See MultiplayerUi.h for the
// design notes. Mirrors EngineUi.cpp's DatapanelGUI/SEH discipline:
//   * widget mutation calls get const-ref/POD arguments and live in tiny SEH
//     frames; no std::string/MyGUI::UString temporary is constructed inside one.
//   * delegates (MyGUI::newDelegate) are assigned OUTSIDE SEH after a build.
//   * typed input is captured with GetAsyncKeyState on rising edges, exactly
//     like the F2 panel's F2 toggle, because MyGUI edit boxes in this build do
//     not take keyboard focus. The same trick makes the whole window keyboard
//     navigable (F10 toggle, Up/Down select, Enter activate) - which is also
//     the only way to drive it in automated runs: Kenshi ignores synthetic
//     mouse input, but GetAsyncKeyState sees synthetic keys.

#include "EngineInternal.h"
#include "EngineUi.h"
#include "MultiplayerUi.h"

#include <kenshi/gui/DatapanelGUI.h>
#include <kenshi/gui/DataPanelLine.h>
#include <kenshi/gui/TitleScreen.h>
#include <mygui/MyGUI_Delegate.h>
#include <windows.h>
#include <cstring>
#include <cstdlib>

#include "../core/Config.h"
#include "../net/SteamInvite.h"

namespace coop {
namespace engine {

namespace {

std::string g_empty;

// Title-button constants live at namespace scope: static locals with
// destructors are not allowed inside an SEH frame (C2712).
const std::string kBtnName("kc_multiplayer");
const std::string kBtnSkin("Kenshi_Button1");
const std::string kBtnText("MULTIPLAYER");

// ---- typed-input capture ------------------------------------------------------

struct KeyLatch {
    unsigned char down[256];
    KeyLatch() { std::memset(down, 0, sizeof(down)); }
    bool pressed(int vk) {
        if (vk < 0 || vk > 255) return false;
        bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool rise = d && !down[vk];
        down[vk] = d ? 1 : 0;
        return rise;
    }
    void clear() { std::memset(down, 0, sizeof(down)); }
};

bool shiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }
bool capsOn()    { return (GetKeyState(VK_CAPITAL) & 1) != 0; }

// US-ASCII translation of a virtual key (physical key -> letter). Layout
// independent on purpose: both players translate the same physical keys the
// same way, so a password typed on a RU layout on one machine matches the same
// keys pressed on the other. Returns false for keys we do not accept.
bool vkToAscii(int vk, char* out) {
    if (vk >= '0' && vk <= '9') { *out = (char)vk; return true; }
    if (vk >= 'A' && vk <= 'Z') {
        char c = (char)vk;
        if (!(shiftDown() ^ capsOn())) c = (char)(c - 'A' + 'a');
        *out = c;
        return true;
    }
    if (vk == VK_SPACE)        { *out = ' ';  return true; }
    if (vk == VK_OEM_PERIOD || vk == VK_DECIMAL) { *out = '.'; return true; }
    if (vk == VK_OEM_MINUS)    { *out = '-';  return true; }
    return false;
}

// ---- UI state -----------------------------------------------------------------

enum Page { PAGE_MAIN = 0, PAGE_STEAM = 1, PAGE_LAN = 2 };

// Action ids for the unified button dispatcher (mouse callback + keyboard Enter
// both land in runAct()).
enum BtnAct {
    ACT_NONE = 0, ACT_STEAM, ACT_LAN, ACT_CLOSE, ACT_BACK,
    ACT_REFRESH, ACT_HOST_STEAM, ACT_HOST_LAN, ACT_CONNECT_LAN, ACT_CONNECT_STEAM,
    ACT_FIELD0, ACT_FIELD1, ACT_FIELD2, ACT_FIELD3, ACT_SERVER
};

struct MpState {
    DatapanelGUI* panel;
    bool open, built, needsRebuild, wantClose;
    int  page;
    int  focus;          // -1 none; LAN 0=ip 1=port 2=password 3=name; STEAM 2=password
    int  pendingJoin;    // STEAM: server row awaiting CONNECT (password prompt)
    int  sel;            // keyboard selection among the current buttons
    unsigned lastServerN;
    char ip[64];
    char port[8];
    char password[64];
    char name[40];
    std::string status;
    KeyLatch keys;
    MpState()
        : panel(0), open(false), built(false), needsRebuild(false), wantClose(false),
          page(PAGE_MAIN), focus(-1), pendingJoin(-1), sel(0), lastServerN(0) {
        std::memset(ip, 0, sizeof(ip));
        std::memset(port, 0, sizeof(port));
        std::memset(password, 0, sizeof(password));
        std::memset(name, 0, sizeof(name));
        std::strcpy(ip, "127.0.0.1");
        std::strcpy(port, "27800");
    }
};

MpState g_mp;

MyGUI::Button* g_menuBtn = 0;
void*          g_menuRoot = 0;

// Buttons of the page currently built (recreated on every rebuild) plus the
// action and server mapping filled by buildPanel; callbacks are wired after.
const int MAX_BTNS = 24;
DataPanelLine_Button* g_btn[MAX_BTNS];
int  g_btnN = 0;
int  g_acts[MAX_BTNS];
int  g_srvOf[MAX_BTNS];

MpConnectFn    g_onConnect = 0;
MpDisconnectFn g_onDisconnect = 0;

// ---- SEH widget helpers (POD/const-ref only) -----------------------------------

void panelClearSeh(DatapanelGUI* p) {
    __try { p->_NV_clear(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void panelCaptionSeh(DatapanelGUI* p, const std::string* t) {
    __try { p->setCaption(*t); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

DataPanelLine_Button* panelBtnSeh(DatapanelGUI* p, const std::string* key,
                                  const std::string* cap) {
    __try { return p->setLineButton(*key, *cap, 0); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

DataPanelLine* panelLineSeh(DatapanelGUI* p, const std::string* key,
                            const std::string* val) {
    __try { return p->setLine(*key, *val, g_empty, 0, false, true); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void panelSpaceSeh(DatapanelGUI* p, float h) {
    __try { p->addSpace(0, h); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void panelUpdateSeh(DatapanelGUI* p) {
    __try { p->_NV_update(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool panelArmSeh(ForgottenGUI* g, DatapanelGUI* p) {
    __try {
        g->addDatapanelToUpdateList(p);
        p->_NV_show(true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void panelDestroySeh(ForgottenGUI* g, DatapanelGUI* p) {
    if (!g || !p) return;
    __try {
        g->removeDatapanelFromUpdateList(p);
        g->destroy(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void menuBtnDestroySeh(ForgottenGUI* g, MyGUI::Widget* w) {
    if (!g || !w) return;
    __try { g->destroyWidget(w); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// One row recipe: 'B' = button line, 'L' = read-only line, 'S' = spacer.
// Button keys are auto-generated (kcrow<i>) so selection decoration can change
// the caption without disturbing the panel's line keys.
struct Row {
    char kind;
    const std::string* val;
};

void panelRowsSeh(DatapanelGUI* p, const Row* rows, int n) {
    g_btnN = 0;
    for (int i = 0; i < n; ++i) {
        if (rows[i].kind == 'S') {
            panelSpaceSeh(p, 0.35f);
            continue;
        }
        char kbuf[16];
        _snprintf(kbuf, sizeof(kbuf) - 1, "kcrow%d", i);
        kbuf[sizeof(kbuf) - 1] = '\0';
        std::string key(kbuf);
        if (rows[i].kind == 'B') {
            std::string cap;
            if (g_btnN == g_mp.sel) cap = "> "; // keyboard selection marker
            cap += *rows[i].val;
            DataPanelLine_Button* b = panelBtnSeh(p, &key, &cap);
            if (b && g_btnN < MAX_BTNS) {
                g_btn[g_btnN] = b;
                g_acts[g_btnN] = ACT_NONE;
                g_srvOf[g_btnN] = -1;
                ++g_btnN;
            }
        } else {
            panelLineSeh(p, &key, rows[i].val);
        }
    }
    panelUpdateSeh(p);
}

// ---- page text builders ---------------------------------------------------------

std::string fieldText(const char* label, const char* value, bool focused, bool mask) {
    std::string v;
    if (mask && value[0]) {
        for (const char* q = value; *q; ++q) v += '*';
    } else {
        v = value[0] ? value : std::string("(not set)");
    }
    std::string s = std::string(label) + ":  " + v;
    if (focused) s += "    <type now>";
    return s;
}

std::string serverText(int i) {
    using namespace coop::steaminvite;
    std::string s;
    if (serverName(i) && serverName(i)[0]) { s = "< "; s += serverName(i); s += " >"; }
    else s = "< server >";
    char b[48];
    _snprintf(b, sizeof(b) - 1, "   %d/2", serverPlayers(i));
    b[sizeof(b) - 1] = '\0';
    s += b;
    if (serverLocked(i)) s += "   [password]";
    if (!serverCompatible(i)) s += "   [old version]";
    return s;
}

// ---- actions ---------------------------------------------------------------------

void closePanel();        // deferred: sets wantClose, destroyed on the next tick
void reallyClosePanel();  // the actual destroy; tick + teardown only

void openPanel() {
    g_mp.open = true;
    g_mp.wantClose = false;
    g_mp.page = PAGE_MAIN;
    g_mp.focus = -1;
    g_mp.pendingJoin = -1;
    g_mp.sel = 0;
    g_mp.needsRebuild = true;
    g_mp.keys.clear();
    coop::logLine("[mp-ui] multiplayer menu opened");
}

void lanConnect(bool host) {
    int p = std::atoi(g_mp.port);
    if (p <= 0 || p > 65535) p = 27800;
    coop::savePeerToFile(g_mp.ip, p, g_mp.password, g_mp.name, "udp");
    g_mp.status = host ? "Opening LAN server..." : "Connecting over LAN...";
    coop::steaminvite::reset();
    if (g_onConnect) g_onConnect(host, false, 0);
    coop::logLine(host ? "[mp-ui] LAN host start" : "[mp-ui] LAN join connect");
    g_mp.needsRebuild = true;
}

void steamHost() {
    int p = std::atoi(g_mp.port);
    if (p <= 0 || p > 65535) p = 27800;
    coop::savePeerToFile(g_mp.ip, p, g_mp.password, g_mp.name, "steam");
    if (!coop::steaminvite::ready()) {
        g_mp.status = "Steam is not running - start Steam and retry";
        g_mp.needsRebuild = true;
        return;
    }
    coop::steaminvite::hostPublicServer(g_mp.name[0] ? g_mp.name : "Kenshi Co-op server",
                                        g_mp.password[0] != 0, g_mp.password);
    g_mp.status = "Opening Steam server...";
    g_mp.needsRebuild = true;
}

void steamJoinSelected() {
    int i = g_mp.pendingJoin;
    if (i < 0 || i >= coop::steaminvite::serverCount()) return;
    if (coop::steaminvite::serverLocked(i) && g_mp.password[0] == 0) {
        g_mp.focus = 2;
        g_mp.status = "This server needs a password";
        g_mp.needsRebuild = true;
        return;
    }
    int p = std::atoi(g_mp.port);
    if (p <= 0 || p > 65535) p = 27800;
    coop::savePeerToFile(g_mp.ip, p, g_mp.password, g_mp.name, "steam");
    g_mp.status = "Connecting to server...";
    coop::steaminvite::joinServer(i); // -> onLobbyEnter -> onConnect(false,true,owner)
    coop::logLine("[mp-ui] steam join requested");
    closePanel();
}

// Mouse callback for every button: map the sender to its slot, then runAct.
void onAnyBtn(DataPanelLine* l);

void runAct(int slot) {
    if (slot < 0 || slot >= g_btnN) return;
    switch (g_acts[slot]) {
        case ACT_STEAM:
            g_mp.page = PAGE_STEAM;
            g_mp.focus = -1;
            g_mp.pendingJoin = -1;
            g_mp.sel = 0;
            if (coop::steaminvite::ready()) coop::steaminvite::browseServers();
            g_mp.needsRebuild = true;
            break;
        case ACT_LAN:
            g_mp.page = PAGE_LAN;
            g_mp.focus = -1;
            g_mp.sel = 0;
            g_mp.needsRebuild = true;
            break;
        case ACT_CLOSE: closePanel(); break;
        case ACT_BACK:
            g_mp.page = PAGE_MAIN;
            g_mp.focus = -1;
            g_mp.pendingJoin = -1;
            g_mp.sel = 0;
            g_mp.needsRebuild = true;
            break;
        case ACT_REFRESH:
            if (coop::steaminvite::ready()) coop::steaminvite::browseServers();
            g_mp.needsRebuild = true;
            break;
        case ACT_HOST_STEAM: steamHost(); break;
        case ACT_HOST_LAN:   lanConnect(true); break;
        case ACT_CONNECT_LAN: lanConnect(false); break;
        case ACT_CONNECT_STEAM: steamJoinSelected(); break;
        case ACT_FIELD0: g_mp.focus = 0; g_mp.needsRebuild = true; break;
        case ACT_FIELD1: g_mp.focus = 1; g_mp.needsRebuild = true; break;
        case ACT_FIELD2: g_mp.focus = 2; g_mp.needsRebuild = true; break;
        case ACT_FIELD3: g_mp.focus = 3; g_mp.needsRebuild = true; break;
        case ACT_SERVER: {
            int srv = g_srvOf[slot];
            if (srv < 0 || srv >= coop::steaminvite::serverCount()) break;
            g_mp.pendingJoin = srv;
            g_mp.needsRebuild = true;
            if (coop::steaminvite::serverLocked(srv)) {
                g_mp.focus = 2;
                g_mp.status = "Password required - type it and press CONNECT";
            } else {
                steamJoinSelected();
            }
            break;
        }
        default: break;
    }
}

void onAnyBtn(DataPanelLine* l) {
    for (int i = 0; i < g_btnN; ++i) {
        if (l == g_btn[i]) { runAct(i); return; }
    }
}

// ---- title-screen button ------------------------------------------------------------

void onMenuBtn(MyGUI::Widget*) { openPanel(); }

// Current title-screen root (null when the title screen is gone). SEH-guarded:
// after the game tears the title down, touching its singletons can fault.
void* titleRootSeh() {
    __try {
        TitleScreen* ts = TitleScreen::getSingleton();
        if (!ts) return 0;
        MyGUI::Widget* root = ts->mMainWidget ? ts->mMainWidget : ts->getWidget();
        return (void*)root;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool menuBtnCreateSeh(ForgottenGUI* g, MyGUI::Button** out) {
    *out = 0;
    // Throttled diagnostics: the title tick retries every frame until the
    // button exists, so log at most once per 2 s per failure kind.
    static unsigned s_lastDiag = 0;
    unsigned now = GetTickCount();
    bool diag = (s_lastDiag == 0) || (now - s_lastDiag >= 2000);
    __try {
        TitleScreen* ts = TitleScreen::getSingleton();
        if (!ts) { if (diag) { s_lastDiag = now; coop::logLine("[mp-ui] no TitleScreen singleton yet"); } return false; }
        // The title screen's real root is the BaseLayout main widget; GUIWindow::getWidget()
        // is the generic window handle and stays null for the title (measured 2026-10-05).
        MyGUI::Widget* root = ts->mMainWidget ? ts->mMainWidget : ts->getWidget();
        if (!root) { if (diag) { s_lastDiag = now; coop::logLine("[mp-ui] TitleScreen has no root widget"); } return false; }
        if (g_menuRoot == (void*)root && g_menuBtn) return true;
        g_menuRoot = (void*)root;
        MyGUI::Widget* existing = root->findWidget(kBtnName);
        if (existing) {
            MyGUI::Button* b = existing->castType<MyGUI::Button>(false);
            if (b) { *out = b; return true; }
        }
        MyGUI::IntCoord rc = root->getClientCoord();
        int W = rc.width > 0 ? rc.width : 1920;
        int H = rc.height > 0 ? rc.height : 1080;
        int w = (int)(W * 0.225f), h = (int)(H * 0.046f);
        int top = (int)(H * 0.885f), left = (int)(W * 0.266f);
        *out = g->createButtonAbs(root, top, left, w, h, kBtnName, kBtnText, kBtnSkin);
        return *out != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (diag) { s_lastDiag = now; coop::logLine("[mp-ui] createButtonAbs raised (SEH caught)"); }
        *out = 0;
        return false;
    }
}

void ensureMenuButton(ForgottenGUI* g) {
    if (g_menuBtn && g_menuRoot) return; // still armed from a previous tick
    MyGUI::Button* b = 0;
    if (!menuBtnCreateSeh(g, &b) || !b) {
        static bool warned = false;
        if (!warned) { warned = true; coop::logErrLine("[mp-ui] MULTIPLAYER button creation failed"); }
        return;
    }
    g_menuBtn = b;
    // Delegate assignment OUTSIDE SEH (fresh widget pointer is valid here).
    g_menuBtn->eventMouseButtonClick += MyGUI::newDelegate(&onMenuBtn);
    coop::logLine("[mp-ui] MULTIPLAYER button armed on the title screen");
}

// ---- panel build -----------------------------------------------------------------

void buildPanel(ForgottenGUI* g) {
    DatapanelGUI* p = g_mp.panel;
    if (!p) return;
    panelClearSeh(p);

    // All captions built OUTSIDE SEH: strings may unwind (C2712 rule).
    std::string title, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10;
    Row rows[32];
    int n = 0;

    if (g_mp.page == PAGE_MAIN) {
        title = "MULTIPLAYER";
        s1 = "Steam - browse servers (lobby list)";
        s2 = "LAN - direct IP and password";
        s3 = "Close";
        s4 = g_mp.status.empty() ? std::string("Pick how to play") : g_mp.status;
        s5 = "Status";
        rows[n].kind = 'L'; rows[n].val = &s5; ++n;
        s6 = "  (Up/Down + Enter, or mouse)";
        rows[n].kind = 'L'; rows[n].val = &s6; ++n;
        rows[n].kind = 'S'; ++n;
        rows[n].kind = 'B'; rows[n].val = &s1; ++n;
        rows[n].kind = 'B'; rows[n].val = &s2; ++n;
        rows[n].kind = 'B'; rows[n].val = &s3; ++n;
        panelCaptionSeh(p, &title);
        panelRowsSeh(p, rows, n);
        g_acts[0] = ACT_STEAM;
        g_acts[1] = ACT_LAN;
        g_acts[2] = ACT_CLOSE;
        return;
    }

    if (g_mp.page == PAGE_STEAM) {
        using namespace coop::steaminvite;
        title = "STEAM SERVERS";
        std::string st = g_mp.status;
        const char* iv = status();
        if (iv && iv[0]) { if (!st.empty()) st += "  |  "; st += iv; }
        if (st.empty()) st = "Searching...";
        s1 = st;
        s2 = "Status";
        rows[n].kind = 'L'; rows[n].val = &s2; ++n;

        int fieldSlots = 0;
        if (g_mp.pendingJoin >= 0) {
            s3 = fieldText("Password", g_mp.password, g_mp.focus == 2, true);
            rows[n].kind = 'B'; rows[n].val = &s3; ++n; fieldSlots = 1;
            s4 = "CONNECT to selected server";
            rows[n].kind = 'B'; rows[n].val = &s4; ++n; fieldSlots = 2;
        }

        int nsrv = serverCount();
        if (nsrv > 12) nsrv = 12;
        g_mp.lastServerN = (unsigned)nsrv;
        std::string srvText[12];
        for (int i = 0; i < nsrv; ++i) srvText[i] = serverText(i);
        int firstSrvRow = n;
        for (int i = 0; i < nsrv; ++i) {
            rows[n].kind = 'B'; rows[n].val = &srvText[i]; ++n;
        }
        if (nsrv == 0) {
            s5 = "(no servers found - host one, or refresh)";
            s6 = "Servers";
            rows[n].kind = 'L'; rows[n].val = &s6; ++n;
            s7 = "(list empty)";
            rows[n].kind = 'L'; rows[n].val = &s7; ++n;
        }
        s8 = "Refresh";
        s9 = "Host a server";
        s10 = "Back";
        int tail = n;
        rows[n].kind = 'B'; rows[n].val = &s8; ++n;
        rows[n].kind = 'B'; rows[n].val = &s9; ++n;
        rows[n].kind = 'B'; rows[n].val = &s10; ++n;

        panelCaptionSeh(p, &title);
        panelRowsSeh(p, rows, n);

        // Slot mapping: [0]=password (optional), [1]=connect (optional),
        // then server rows, then refresh/host/back.
        int slot = 0;
        if (g_mp.pendingJoin >= 0) {
            g_acts[slot++] = ACT_FIELD2;
            g_acts[slot++] = ACT_CONNECT_STEAM;
        }
        (void)fieldSlots;
        for (int i = 0; i < nsrv && slot < g_btnN; ++i) {
            g_acts[slot] = ACT_SERVER;
            g_srvOf[slot] = i;
            ++slot;
        }
        if (slot < g_btnN) g_acts[slot++] = ACT_REFRESH;
        if (slot < g_btnN) g_acts[slot++] = ACT_HOST_STEAM;
        if (slot < g_btnN) g_acts[slot++] = ACT_BACK;
        return;
    }

    // PAGE_LAN
    title = "LAN GAME";
    s1 = fieldText("IP", g_mp.ip, g_mp.focus == 0, false);
    s2 = fieldText("Port", g_mp.port, g_mp.focus == 1, false);
    s3 = fieldText("Password", g_mp.password, g_mp.focus == 2, true);
    s4 = fieldText("Server name", g_mp.name, g_mp.focus == 3, false);
    s5 = "CONNECT (join)";
    s6 = "HOST (open server)";
    s7 = "Back";
    s8 = g_mp.status.empty() ? std::string("Click a field, type; Enter = connect") : g_mp.status;
    s9 = "Status";
    rows[n].kind = 'B'; rows[n].val = &s1; ++n;
    rows[n].kind = 'B'; rows[n].val = &s2; ++n;
    rows[n].kind = 'B'; rows[n].val = &s3; ++n;
    rows[n].kind = 'B'; rows[n].val = &s4; ++n;
    rows[n].kind = 'S'; ++n;
    rows[n].kind = 'B'; rows[n].val = &s5; ++n;
    rows[n].kind = 'B'; rows[n].val = &s6; ++n;
    rows[n].kind = 'B'; rows[n].val = &s7; ++n;
    rows[n].kind = 'L'; rows[n].val = &s9; ++n;

    panelCaptionSeh(p, &title);
    panelRowsSeh(p, rows, n);
    if (g_btnN >= 7) {
        g_acts[0] = ACT_FIELD0;
        g_acts[1] = ACT_FIELD1;
        g_acts[2] = ACT_FIELD2;
        g_acts[3] = ACT_FIELD3;
        g_acts[4] = ACT_CONNECT_LAN;
        g_acts[5] = ACT_HOST_LAN;
        g_acts[6] = ACT_BACK;
    }
}

void closePanel() {
    // Never destroy the window from inside a MyGUI event callback (the mouse
    // path runs there): flag it and let mpMenuTick do the destroy.
    if (!g_mp.open && !g_mp.panel) return;
    g_mp.open = false;
    g_mp.wantClose = true;
}

void reallyClosePanel() {
    ForgottenGUI* g = ::gui;
    if (g && g_mp.panel) panelDestroySeh(g, g_mp.panel);
    g_mp.panel = 0;
    g_mp.built = false;
    g_mp.open = false;
    g_mp.focus = -1;
    g_mp.pendingJoin = -1;
    g_btnN = 0;
    coop::logLine("[mp-ui] multiplayer menu closed");
}

// ---- field editing -----------------------------------------------------------------

char* activeField() {
    if (g_mp.page == PAGE_LAN) {
        switch (g_mp.focus) {
            case 0: return g_mp.ip;
            case 1: return g_mp.port;
            case 2: return g_mp.password;
            case 3: return g_mp.name;
            default: return 0;
        }
    }
    if (g_mp.page == PAGE_STEAM && g_mp.focus == 2) return g_mp.password;
    return 0;
}

size_t activeFieldMax() {
    if (g_mp.page == PAGE_LAN) {
        if (g_mp.focus == 0) return sizeof(g_mp.ip) - 1;
        if (g_mp.focus == 1) return sizeof(g_mp.port) - 1;
        if (g_mp.focus == 2) return sizeof(g_mp.password) - 1;
        if (g_mp.focus == 3) return sizeof(g_mp.name) - 1;
    }
    if (g_mp.page == PAGE_STEAM && g_mp.focus == 2) return sizeof(g_mp.password) - 1;
    return 0;
}

bool fieldAccepts(int field, char c) {
    if (g_mp.page == PAGE_LAN) {
        if (field == 0) return (c >= '0' && c <= '9') || c == '.';
        if (field == 1) return c >= '0' && c <= '9';
        if (field == 2) return c >= 33 && c <= 126;
        if (field == 3) return c >= 32 && c <= 126;
    }
    if (g_mp.page == PAGE_STEAM && field == 2) return c >= 33 && c <= 126;
    return false;
}

// Typing (focus >= 0) and menu navigation (focus < 0). Rising-edge reads, so
// synthetic keys (GetAsyncKeyState-visible) drive it exactly like F2 does.
void captureInput() {
    if (g_mp.focus < 0) {
        if (g_mp.keys.pressed(VK_UP) || g_mp.keys.pressed(VK_LEFT)) {
            if (g_btnN > 0) { g_mp.sel = (g_mp.sel + g_btnN - 1) % g_btnN; g_mp.needsRebuild = true; }
        } else if (g_mp.keys.pressed(VK_DOWN) || g_mp.keys.pressed(VK_RIGHT)) {
            if (g_btnN > 0) { g_mp.sel = (g_mp.sel + 1) % g_btnN; g_mp.needsRebuild = true; }
        } else if (g_mp.keys.pressed(VK_RETURN)) {
            runAct(g_mp.sel);
        } else if (g_mp.keys.pressed(VK_ESCAPE)) {
            if (g_mp.page != PAGE_MAIN) { g_mp.page = PAGE_MAIN; g_mp.pendingJoin = -1; g_mp.sel = 0; g_mp.needsRebuild = true; }
            else closePanel();
        }
        return;
    }

    char* buf = activeField();
    if (!buf) { g_mp.focus = -1; return; }

    if (g_mp.keys.pressed(VK_RETURN)) {
        if (g_mp.page == PAGE_LAN) lanConnect(false);
        else if (g_mp.page == PAGE_STEAM && g_mp.pendingJoin >= 0) steamJoinSelected();
        return;
    }
    if (g_mp.keys.pressed(VK_ESCAPE) || g_mp.keys.pressed(VK_TAB)) {
        g_mp.focus = -1;
        g_mp.needsRebuild = true;
        return;
    }
    if (g_mp.keys.pressed(VK_BACK)) {
        size_t n = std::strlen(buf);
        if (n > 0) { buf[n - 1] = '\0'; g_mp.needsRebuild = true; }
        return;
    }
    bool changed = false;
    size_t len = std::strlen(buf);
    size_t maxLen = activeFieldMax();
    for (int vk = 'A'; vk <= 'Z' && !changed; ++vk) {
        if (g_mp.keys.pressed(vk)) {
            char c;
            if (vkToAscii(vk, &c) && len < maxLen && fieldAccepts(g_mp.focus, c)) { buf[len] = c; buf[len + 1] = '\0'; changed = true; }
        }
    }
    for (int vk = '0'; vk <= '9' && !changed; ++vk) {
        if (g_mp.keys.pressed(vk)) {
            char c = (char)vk;
            if (len < maxLen && fieldAccepts(g_mp.focus, c)) { buf[len] = c; buf[len + 1] = '\0'; changed = true; }
        }
    }
    int extras[3] = { VK_OEM_PERIOD, VK_OEM_MINUS, VK_SPACE };
    for (int i = 0; i < 3 && !changed; ++i) {
        if (g_mp.keys.pressed(extras[i])) {
            char c;
            if (vkToAscii(extras[i], &c) && len < maxLen && fieldAccepts(g_mp.focus, c)) { buf[len] = c; buf[len + 1] = '\0'; changed = true; }
        }
    }
    if (changed) g_mp.needsRebuild = true;
}

KeyLatch g_menuKeys; // F10 toggle latch (separate from the panel's input latch)

} // namespace

// ---- public entry ---------------------------------------------------------------------

void mpMenuTick(const CoopPanelState* st, MpConnectFn onConnect,
                MpDisconnectFn onDisconnect, bool allowUi) {
    g_onConnect = onConnect;
    g_onDisconnect = onDisconnect;
    (void)st;

    ForgottenGUI* g = ::gui;
    if (!g) return;

    if (!allowUi) {
        if (g_mp.open || g_mp.panel) reallyClosePanel();
        if (g_menuBtn) { menuBtnDestroySeh(g, g_menuBtn); g_menuBtn = 0; g_menuRoot = 0; }
        return;
    }

    // The title screen can be torn down (Esc at the menu, leaving to desktop)
    // or recreated. Drop our handle whenever the root changes so it can never
    // dangle into freed MyGUI widgets - this was a crash source (2026-10-05).
    void* root = titleRootSeh();
    if (root == 0) {
        if (g_mp.open || g_mp.panel) reallyClosePanel();
        if (g_menuBtn) { menuBtnDestroySeh(g, g_menuBtn); g_menuBtn = 0; }
        g_menuRoot = 0;
        return;
    }
    if (root != g_menuRoot) { g_menuBtn = 0; g_menuRoot = 0; }

    ensureMenuButton(g);

    // Deferred destroy requested by a callback / key handler.
    if (g_mp.wantClose) {
        g_mp.wantClose = false;
        reallyClosePanel();
    }

    // F10 toggles the window (keyboard-only path for tests + convenience).
    if (g_menuKeys.pressed(VK_F10)) {
        if (g_mp.open) closePanel();
        else openPanel();
    }

    if (g_mp.open && !g_mp.panel) {
        std::string layer("Info");
        g_mp.panel = g->createDatapanel(0.24f, 0.33f, 0.34f, 0.34f, false, layer, true);
        g_mp.built = false;
        if (!g_mp.panel) {
            coop::logErrLine("[mp-ui] createDatapanel FAILED");
            g_mp.open = false;
            return;
        }
        if (!panelArmSeh(g, g_mp.panel)) coop::logErrLine("[mp-ui] panel arm FAILED");
        coop::logLine("[mp-ui] multiplayer panel created");
    }

    if (!g_mp.open) return;

    captureInput();

    // The Steam browser is async: refresh the window when the row count moved.
    if (g_mp.page == PAGE_STEAM && coop::steaminvite::serverCount() != (int)g_mp.lastServerN) {
        g_mp.needsRebuild = true;
    }

    if (!g_mp.built || g_mp.needsRebuild) {
        buildPanel(g);
        // Wire the single mouse delegate on every fresh button (OUTSIDE SEH).
        for (int i = 0; i < g_btnN; ++i) {
            if (g_btn[i]) g_btn[i]->callback = MyGUI::newDelegate(&onAnyBtn);
        }
        g_mp.built = true;
        g_mp.needsRebuild = false;
    }
}

void mpMenuShutdown() {
    ForgottenGUI* g = ::gui;
    if (g_mp.panel) panelDestroySeh(g, g_mp.panel);
    g_mp.panel = 0;
    g_mp.built = false;
    g_mp.open = false;
    if (g && g_menuBtn) menuBtnDestroySeh(g, g_menuBtn);
    g_menuBtn = 0;
    g_menuRoot = 0;
}

} // namespace engine
} // namespace coop
