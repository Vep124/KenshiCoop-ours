// MultiplayerUi.h - the multiplayer MENU: a MULTIPLAYER button on the Kenshi
// title screen and the window it opens (Steam server browser / LAN direct-IP
// form), modelled on the F2 co-op panel (same DatapanelGUI stack + typed-input
// capture). Public header: forward declarations only, no <kenshi/...> includes.
//
// Flow (our fork):
//   title screen -> [MULTIPLAYER]
//     -> MAIN page: [STEAM - server browser] [LAN - direct IP] [Close]
//     -> STEAM page: list of public KenshiCoop lobbies (kc_* lobby data),
//        [Refresh] [Host a server] [Back]; a locked server reveals a password
//        row; clicking a server connects as the join (steaminvite::joinServer).
//     -> LAN page: IP / Port / Password rows (click a row, then type; Enter =
//        connect), [Connect] [Host LAN] [Back]. Values land in coop_config.json
//        via coop::savePeerToFile, and the normal coopUiConnect path does the
//        rest (reloadPeerFromFile picks them up).
// Text input is captured with GetAsyncKeyState (the F2 panel already proved
// MyGUI edit boxes do not take keyboard focus); characters are translated on
// the rising edge, so typing never depends on widget focus.
//
// Main thread only; SEH-guarded widget calls (same discipline as EngineUi.cpp).

#ifndef KENSHICOOP_MULTIPLAYER_UI_H
#define KENSHICOOP_MULTIPLAYER_UI_H

namespace coop {
namespace engine {

struct CoopPanelState; // defined in EngineUi.h

// Same signatures as EngineUi's callbacks (the plugin root owns the session).
typedef void (*MpConnectFn)(bool isHost, bool useSteam, unsigned long long peerId);
typedef void (*MpDisconnectFn)();

// Drive the menu UI. Call every frame from the main loop (it is a no-op outside
// the title screen). allowUi=false (gameplay started) closes the window and
// removes the title button handle. onConnect/onDisconnect may be null.
void mpMenuTick(const CoopPanelState* st, MpConnectFn onConnect,
                MpDisconnectFn onDisconnect, bool allowUi);

// Destroy the window/button state (session teardown / world reload).
void mpMenuShutdown();

} // namespace engine
} // namespace coop

#endif // KENSHICOOP_MULTIPLAYER_UI_H
