#ifndef ASTEROIDS_NET_MULTIPLAYER_HPP
#define ASTEROIDS_NET_MULTIPLAYER_HPP

#include "mxnetwork/socket.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace space {

    class PortMapping;

    constexpr std::size_t NETWORK_PROJECTILE_COUNT = 15;
    constexpr std::size_t NETWORK_ASTEROID_COUNT = 16;
    constexpr std::size_t NETWORK_PLAYER_COUNT = 4;

    struct NetworkProjectile {
        std::uint32_t id = 0;
        std::array<float, 3> position{};
        std::array<float, 3> velocity{};
        float lifetime = 0.0f;
        std::uint8_t active = 0;
    };

    struct NetworkAsteroid {
        std::array<float, 3> position{};
        std::array<float, 3> rotation{};
        float radius = 0.0f;
        std::uint8_t slot = 0;
        std::uint8_t active = 0;
    };

    struct NetworkState {
        std::array<float, 3> position{};
        std::array<float, 3> rotation{};
        float current_speed = 1.0f;
        std::array<NetworkProjectile, NETWORK_PROJECTILE_COUNT> projectiles{};
        std::array<NetworkAsteroid, NETWORK_ASTEROID_COUNT> asteroids{};
        std::array<std::uint32_t, NETWORK_PLAYER_COUNT> kills{};
        std::array<std::uint32_t, NETWORK_PLAYER_COUNT> death_serials{};
        std::array<std::uint32_t, NETWORK_PLAYER_COUNT> consumed_projectile_ids{};
        std::uint8_t exploding = 0;
        std::uint8_t match_started = 0;
        std::uint8_t winner = 0;
    };

    struct NetworkPlayerUpdate {
        std::uint8_t player_id = 0;
        NetworkState state{};
    };

    struct NetworkExchange {
        std::vector<NetworkPlayerUpdate> players{};
    };

    class MultiplayerSession {
      public:
        MultiplayerSession();
        ~MultiplayerSession();
        MultiplayerSession(const MultiplayerSession &) = delete;
        MultiplayerSession &operator=(const MultiplayerSession &) = delete;

        void host(const std::string &port, const std::string &player_name);
        void join(const std::string &address, const std::string &port, const std::string &player_name, const std::string &join_code);
        void stop();
        NetworkExchange exchange(const NetworkState &local_state, float delta_time);

        [[nodiscard]] bool active() const;
        [[nodiscard]] bool connected() const;
        [[nodiscard]] bool is_host() const;
        [[nodiscard]] std::uint8_t local_player_id() const;
        [[nodiscard]] std::size_t player_count() const;
        [[nodiscard]] const std::array<std::string, NETWORK_PLAYER_COUNT> &player_names() const;
        [[nodiscard]] const std::array<bool, NETWORK_PLAYER_COUNT> &player_connected() const;
        [[nodiscard]] const std::string &join_code() const;
        [[nodiscard]] const std::string &port_mapping_status() const;

      private:
        struct Packet {
            std::uint32_t magic = 0x4D584E54U;
            std::uint16_t version = 2;
            std::uint16_t size = sizeof(Packet);
            std::uint32_t sequence = 0;
            std::uint8_t player_id = 0xFFU;
            std::uint8_t assigned_player_id = 0xFFU;
            std::uint8_t connected_mask = 0;
            NetworkState state{};
            std::array<char, 32> player_name{};
            std::array<char, 9> join_code{};
        };
        static_assert(sizeof(Packet) <= 1200, "Multiplayer UDP packet must stay below the conservative MTU payload budget");

        mxnetwork::MXNetworkInit network_init{};
        mxnetwork::Socket socket{};
        bool session_active = false;
        bool host_role = false;
        struct Peer {
            mxnetwork::SocketAddress address{};
            NetworkState state{};
            std::string name{};
            std::uint32_t receive_sequence = 0;
            float idle_seconds = 0.0f;
            bool connected = false;
        };
        std::array<Peer, NETWORK_PLAYER_COUNT> peers{};
        std::array<std::string, NETWORK_PLAYER_COUNT> names{};
        std::array<bool, NETWORK_PLAYER_COUNT> connected_players{};
        std::uint8_t assigned_player_id = 0xFFU;
        float send_accumulator = 0.0f;
        std::uint32_t send_sequence = 0;
        std::string local_player_name{};
        std::string session_join_code{};
        std::unique_ptr<PortMapping> port_mapping{};

        void send_client_state(const NetworkState &state);
        void send_host_relay(std::uint8_t source_player, std::uint8_t destination_player);
        std::optional<std::uint8_t> find_or_assign_peer(const mxnetwork::SocketAddress &address);
    };

} // namespace space

#endif
