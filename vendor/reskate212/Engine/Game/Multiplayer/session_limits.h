#pragma once
namespace dingosdk {
// Everyone a session can hold: the wire protocol's limit, reached by dedicated servers.
inline constexpr int multiplayer_player_limit = 250;
// Players in a lobby a player hosts (their own game relays everyone).
inline constexpr int multiplayer_lobby_player_limit = 32;
static_assert(multiplayer_lobby_player_limit <= multiplayer_player_limit);
}
