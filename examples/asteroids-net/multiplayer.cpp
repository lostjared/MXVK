#include "multiplayer.hpp"

#include "mxnetwork/exception.hpp"

#include <algorithm>
#include <cstring>

namespace space {

    void MultiplayerSession::host(const std::string &port, const std::string &player_name) {
        stop();
        socket = mxnetwork::Socket(mxnetwork::SocketType::TYPE_INET_DGRAM);
        socket.bind(port);
        if (!socket.setblocking(false)) {
            stop();
            throw mxnetwork::Exception("Could not make the host UDP socket nonblocking.");
        }
        session_active = true;
        host_role = true;
        local_player_name = player_name;
    }

    void MultiplayerSession::join(const std::string &address, const std::string &port, const std::string &player_name) {
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
    }

    void MultiplayerSession::stop() {
        if (socket.valid()) {
            socket.close();
        }
        socket = mxnetwork::Socket{};
        session_active = false;
        host_role = false;
        peer_connected = false;
        send_accumulator = 0.0f;
        send_sequence = 0;
        receive_sequence = 0;
        local_player_name.clear();
        remote_player_name.clear();
    }

    std::optional<NetworkState> MultiplayerSession::exchange(const NetworkState &local_state, float delta_time) {
        if (!session_active) {
            return std::nullopt;
        }

        send_accumulator += delta_time;
        if (send_accumulator >= (1.0f / 30.0f)) {
            send_accumulator = 0.0f;
            if (!host_role || peer_connected) {
                send_state(local_state);
            }
        }

        std::optional<NetworkState> newest_state;
        for (;;) {
            Packet packet{};
            const ssize_t received = socket.recvfrom(&packet, sizeof(packet));
            if (received < 0) {
                break;
            }
            if (received != static_cast<ssize_t>(sizeof(packet)) || packet.magic != Packet{}.magic ||
                packet.version != Packet{}.version || packet.size != sizeof(Packet)) {
                continue;
            }
            if (packet.sequence <= receive_sequence && receive_sequence != 0) {
                continue;
            }
            receive_sequence = packet.sequence;
            peer_connected = true;
            remote_player_name.assign(packet.player_name.data(), strnlen(packet.player_name.data(), packet.player_name.size()));
            newest_state = packet.state;
        }

        if (!host_role && !peer_connected) {
            send_state(local_state);
        }
        return newest_state;
    }

    bool MultiplayerSession::active() const {
        return session_active;
    }

    bool MultiplayerSession::connected() const {
        return peer_connected;
    }

    bool MultiplayerSession::is_host() const {
        return host_role;
    }

    const std::string &MultiplayerSession::peer_name() const {
        return remote_player_name;
    }

    void MultiplayerSession::send_state(const NetworkState &state) {
        Packet packet{};
        packet.sequence = ++send_sequence;
        packet.state = state;
        const std::size_t copy_size = std::min(local_player_name.size(), packet.player_name.size() - 1U);
        std::memcpy(packet.player_name.data(), local_player_name.data(), copy_size);
        packet.player_name[copy_size] = '\0';
        socket.sendto(&packet, sizeof(packet));
    }

} // namespace space
