#include "multiplayer.hpp"
#include "port_mapping.hpp"

#include "mxnetwork/exception.hpp"

#include <algorithm>
#include <cstring>
#include <random>

namespace space {

    MultiplayerSession::MultiplayerSession() = default;
    MultiplayerSession::~MultiplayerSession() { stop(); }

    namespace {
        std::string packet_name(const std::array<char, 32> &name) {
            return std::string(name.data(), strnlen(name.data(), name.size()));
        }

        void set_packet_name(std::array<char, 32> &output, const std::string &name) {
            const std::size_t size = std::min(name.size(), output.size() - 1U);
            std::memcpy(output.data(), name.data(), size);
            output[size] = '\0';
        }

        std::string random_join_code() {
            static constexpr char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
            std::random_device random_device;
            std::uniform_int_distribution<std::size_t> distribution(0, sizeof(ALPHABET) - 2U);
            std::string code(8, 'A');
            for (char &character : code)
                character = ALPHABET[distribution(random_device)];
            return code;
        }
    } // namespace

    void MultiplayerSession::host(const std::string &port, const std::string &player_name) {
        stop();
        socket = mxnetwork::Socket(mxnetwork::SocketType::TYPE_INET_DGRAM);
        socket.bind(port);
        if (!socket.setblocking(false)) {
            stop();
            throw mxnetwork::Exception("Could not make the host UDP socket nonblocking.");
        }
        port_mapping = std::make_unique<PortMapping>();
        port_mapping->open(port);
        session_active = true;
        host_role = true;
        assigned_player_id = 0;
        local_player_name = player_name;
        names[0] = player_name;
        connected_players[0] = true;
        peers[0].connected = true;
        peers[0].name = player_name;
        session_join_code = random_join_code();
    }

    void MultiplayerSession::join(const std::string &address, const std::string &port, const std::string &player_name, const std::string &join_code) {
        stop();
        socket = mxnetwork::Socket(mxnetwork::SocketType::TYPE_INET_DGRAM);
        if (!socket.connect(address, port)) {
            stop();
            throw mxnetwork::Exception("Could not connect the UDP socket to the host.");
        }
        if (!socket.setblocking(false)) {
            stop();
            throw mxnetwork::Exception("Could not make the client UDP socket nonblocking.");
        }
        session_active = true;
        host_role = false;
        local_player_name = player_name;
        session_join_code = join_code;
    }

    void MultiplayerSession::stop() {
        port_mapping.reset();
        if (socket.valid()) {
            socket.close();
        }
        socket = mxnetwork::Socket{};
        session_active = false;
        host_role = false;
        send_accumulator = 0.0f;
        send_sequence = 0;
        assigned_player_id = 0xFFU;
        local_player_name.clear();
        session_join_code.clear();
        peers = {};
        names = {};
        connected_players = {};
    }

    NetworkExchange MultiplayerSession::exchange(const NetworkState &local_state, float delta_time) {
        NetworkExchange result{};
        if (!session_active) {
            return result;
        }

        if (assigned_player_id < NETWORK_PLAYER_COUNT) {
            peers[assigned_player_id].state = local_state;
        }
        if (host_role) {
            for (std::uint8_t player = 1; player < NETWORK_PLAYER_COUNT; ++player) {
                if (!peers[player].connected)
                    continue;
                peers[player].idle_seconds += delta_time;
                if (peers[player].idle_seconds > 5.0f) {
                    peers[player] = {};
                    connected_players[player] = false;
                    names[player].clear();
                }
            }
        }

        for (;;) {
            Packet packet{};
            mxnetwork::SocketAddress sender{};
            const ssize_t received = host_role ? socket.recvfrom(&packet, sizeof(packet), sender) : socket.recvfrom(&packet, sizeof(packet));
            if (received < 0) {
                break;
            }
            if (received != static_cast<ssize_t>(sizeof(packet)) || packet.magic != Packet{}.magic ||
                packet.version != Packet{}.version || packet.size != sizeof(Packet)) {
                continue;
            }

            if (host_role) {
                const std::string supplied_code(packet.join_code.data(), strnlen(packet.join_code.data(), packet.join_code.size()));
                if (supplied_code != session_join_code) {
                    continue;
                }
                const std::optional<std::uint8_t> player = find_or_assign_peer(sender);
                if (!player.has_value()) {
                    continue;
                }
                Peer &peer = peers[*player];
                if (packet.sequence <= peer.receive_sequence && peer.receive_sequence != 0) {
                    continue;
                }
                peer.receive_sequence = packet.sequence;
                peer.idle_seconds = 0.0f;
                peer.state = packet.state;
                peer.name = packet_name(packet.player_name);
                names[*player] = peer.name;
                connected_players[*player] = true;
                result.players.push_back({*player, packet.state});
            } else {
                for (std::uint8_t player = 0; player < NETWORK_PLAYER_COUNT; ++player) {
                    connected_players[player] = (packet.connected_mask & (1U << player)) != 0U;
                }
                if (packet.assigned_player_id < NETWORK_PLAYER_COUNT) {
                    assigned_player_id = packet.assigned_player_id;
                    names[assigned_player_id] = local_player_name;
                    connected_players[assigned_player_id] = true;
                }
                if (packet.player_id < NETWORK_PLAYER_COUNT) {
                    names[packet.player_id] = packet_name(packet.player_name);
                    connected_players[packet.player_id] = true;
                    result.players.push_back({packet.player_id, packet.state});
                }
            }
        }

        send_accumulator += delta_time;
        if (send_accumulator >= (1.0f / 30.0f)) {
            send_accumulator = 0.0f;
            if (host_role) {
                for (std::uint8_t destination = 1; destination < NETWORK_PLAYER_COUNT; ++destination) {
                    if (!peers[destination].connected) {
                        continue;
                    }
                    for (std::uint8_t source = 0; source < NETWORK_PLAYER_COUNT; ++source) {
                        if (peers[source].connected) {
                            send_host_relay(source, destination);
                        }
                    }
                }
            } else {
                send_client_state(local_state);
            }
        } else if (!host_role && assigned_player_id == 0xFFU) {
            send_client_state(local_state);
        }
        return result;
    }

    bool MultiplayerSession::active() const { return session_active; }
    bool MultiplayerSession::connected() const { return player_count() > 1; }
    bool MultiplayerSession::is_host() const { return host_role; }
    std::uint8_t MultiplayerSession::local_player_id() const { return assigned_player_id; }

    std::size_t MultiplayerSession::player_count() const {
        return static_cast<std::size_t>(std::count(connected_players.begin(), connected_players.end(), true));
    }

    const std::array<std::string, NETWORK_PLAYER_COUNT> &MultiplayerSession::player_names() const { return names; }
    const std::array<bool, NETWORK_PLAYER_COUNT> &MultiplayerSession::player_connected() const { return connected_players; }
    const std::string &MultiplayerSession::join_code() const { return session_join_code; }

    const std::string &MultiplayerSession::port_mapping_status() const {
        static const std::string NO_MAPPING = "Automatic router mapping unavailable; forward the UDP port manually.";
        return port_mapping ? port_mapping->status() : NO_MAPPING;
    }

    void MultiplayerSession::send_client_state(const NetworkState &state) {
        Packet packet{};
        packet.sequence = ++send_sequence;
        packet.player_id = assigned_player_id;
        packet.state = state;
        set_packet_name(packet.player_name, local_player_name);
        const std::size_t code_size = std::min(session_join_code.size(), packet.join_code.size() - 1U);
        std::memcpy(packet.join_code.data(), session_join_code.data(), code_size);
        packet.join_code[code_size] = '\0';
        socket.sendto(&packet, sizeof(packet));
    }

    void MultiplayerSession::send_host_relay(std::uint8_t source_player, std::uint8_t destination_player) {
        Packet packet{};
        packet.sequence = ++send_sequence;
        packet.player_id = source_player;
        packet.assigned_player_id = destination_player;
        for (std::uint8_t player = 0; player < NETWORK_PLAYER_COUNT; ++player) {
            if (connected_players[player])
                packet.connected_mask |= static_cast<std::uint8_t>(1U << player);
        }
        packet.state = peers[source_player].state;
        set_packet_name(packet.player_name, names[source_player]);
        socket.sendto(&packet, sizeof(packet), peers[destination_player].address);
    }

    std::optional<std::uint8_t> MultiplayerSession::find_or_assign_peer(const mxnetwork::SocketAddress &address) {
        for (std::uint8_t player = 1; player < NETWORK_PLAYER_COUNT; ++player) {
            if (peers[player].connected && peers[player].address == address) {
                return player;
            }
        }
        for (std::uint8_t player = 1; player < NETWORK_PLAYER_COUNT; ++player) {
            if (!peers[player].connected) {
                peers[player].connected = true;
                peers[player].address = address;
                connected_players[player] = true;
                return player;
            }
        }
        return std::nullopt;
    }

} // namespace space
