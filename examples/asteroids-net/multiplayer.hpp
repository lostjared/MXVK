#ifndef ASTEROIDS_NET_MULTIPLAYER_HPP
#define ASTEROIDS_NET_MULTIPLAYER_HPP

#include "mxnetwork/socket.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace space {

    constexpr std::size_t NETWORK_PROJECTILE_COUNT = 16;
    constexpr std::size_t NETWORK_ASTEROID_COUNT = 16;

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
        std::uint32_t host_kills = 0;
        std::uint32_t client_kills = 0;
        std::uint32_t host_death_serial = 0;
        std::uint32_t client_death_serial = 0;
        std::uint32_t consumed_client_projectile_id = 0;
        std::uint8_t exploding = 0;
        std::uint8_t match_started = 0;
        std::uint8_t winner = 0;
    };

    class MultiplayerSession {
      public:
        MultiplayerSession() = default;
        ~MultiplayerSession() = default;
        MultiplayerSession(const MultiplayerSession &) = delete;
        MultiplayerSession &operator=(const MultiplayerSession &) = delete;

        void host(const std::string &port, const std::string &player_name);
        void join(const std::string &address, const std::string &port, const std::string &player_name);
        void stop();
        std::optional<NetworkState> exchange(const NetworkState &local_state, float delta_time);

        [[nodiscard]] bool active() const;
        [[nodiscard]] bool connected() const;
        [[nodiscard]] bool is_host() const;
        [[nodiscard]] const std::string &peer_name() const;

      private:
        struct Packet {
            std::uint32_t magic = 0x4D584E54U;
            std::uint16_t version = 1;
            std::uint16_t size = sizeof(Packet);
            std::uint32_t sequence = 0;
            NetworkState state{};
            std::array<char, 32> player_name{};
        };
        static_assert(sizeof(Packet) <= 1200, "Multiplayer UDP packet must stay below the conservative MTU payload budget");

        mxnetwork::MXNetworkInit network_init{};
        mxnetwork::Socket socket{};
        bool session_active = false;
        bool host_role = false;
        bool peer_connected = false;
        float send_accumulator = 0.0f;
        std::uint32_t send_sequence = 0;
        std::uint32_t receive_sequence = 0;
        std::string local_player_name{};
        std::string remote_player_name{};

        void send_state(const NetworkState &state);
    };

} // namespace space

#endif
