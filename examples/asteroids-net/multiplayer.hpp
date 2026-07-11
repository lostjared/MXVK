#ifndef ASTEROIDS_NET_MULTIPLAYER_HPP
#define ASTEROIDS_NET_MULTIPLAYER_HPP

/**
 * @file multiplayer.hpp
 * @brief UDP multiplayer state exchange for Asteroids.
 */

#include "mxnetwork/socket.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace space {

    class PortMapping;

    /** @brief Maximum projectile snapshots carried in one network state. */
    constexpr std::size_t NETWORK_PROJECTILE_COUNT = 15;
    /** @brief Maximum asteroid snapshots carried in one network state. */
    constexpr std::size_t NETWORK_ASTEROID_COUNT = 16;
    /** @brief Maximum number of players in a session. */
    constexpr std::size_t NETWORK_PLAYER_COUNT = 4;

    /**
     * @struct NetworkProjectile
     * @brief Serialized gameplay state for one projectile.
     */
    struct NetworkProjectile {
        std::uint32_t id = 0;            ///< Projectile identifier assigned by its owner.
        std::array<float, 3> position{}; ///< World-space position.
        std::array<float, 3> velocity{}; ///< World-space velocity.
        float lifetime = 0.0f;           ///< Remaining lifetime in seconds.
        std::uint8_t active = 0;         ///< Nonzero when the projectile is active.
    };

    /**
     * @struct NetworkAsteroid
     * @brief Serialized gameplay state for one asteroid.
     */
    struct NetworkAsteroid {
        std::array<float, 3> position{}; ///< World-space position.
        std::array<float, 3> rotation{}; ///< Euler rotation in degrees.
        float radius = 0.0f;             ///< Collision radius.
        std::uint8_t slot = 0;           ///< Source asteroid slot.
        std::uint8_t active = 0;         ///< Nonzero when the asteroid is active.
    };

    /**
     * @struct NetworkState
     * @brief Complete replicated gameplay state published by one player.
     */
    struct NetworkState {
        std::array<float, 3> position{};                                           ///< Ship world-space position.
        std::array<float, 3> rotation{};                                           ///< Ship Euler rotation in degrees.
        float current_speed = 1.0f;                                                ///< Ship forward speed.
        std::array<NetworkProjectile, NETWORK_PROJECTILE_COUNT> projectiles{};     ///< Locally owned projectiles.
        std::array<NetworkAsteroid, NETWORK_ASTEROID_COUNT> asteroids{};           ///< Host-authoritative asteroids.
        std::array<std::uint32_t, NETWORK_PLAYER_COUNT> kills{};                   ///< Kill counters by player slot.
        std::array<std::uint32_t, NETWORK_PLAYER_COUNT> death_serials{};           ///< Monotonic death events by player slot.
        std::array<std::uint32_t, NETWORK_PLAYER_COUNT> consumed_projectile_ids{}; ///< Last consumed projectile per player.
        std::uint8_t exploding = 0;                                                ///< Nonzero while the ship is exploding.
        std::uint8_t match_started = 0;                                            ///< Nonzero after the host starts the match.
        std::uint8_t winner = 0;                                                   ///< Winning player identifier, or zero if unset.
    };

    /**
     * @struct NetworkPlayerUpdate
     * @brief State received for a specific player slot.
     */
    struct NetworkPlayerUpdate {
        std::uint8_t player_id = 0; ///< Player slot owning the state.
        NetworkState state{};       ///< Most recently received state.
    };

    /**
     * @struct NetworkExchange
     * @brief Collection of remote player updates produced by one exchange tick.
     */
    struct NetworkExchange {
        std::vector<NetworkPlayerUpdate> players{}; ///< Updates received during the tick.
    };

    /**
     * @class MultiplayerSession
     * @brief Owns a host or client UDP session and exchanges replicated game state.
     */
    class MultiplayerSession {
      public:
        /** @brief Initializes the networking subsystem. */
        MultiplayerSession();
        /** @brief Stops the session and releases networking resources. */
        ~MultiplayerSession();
        MultiplayerSession(const MultiplayerSession &) = delete;
        MultiplayerSession &operator=(const MultiplayerSession &) = delete;

        /**
         * @brief Starts hosting a session.
         * @param port Local UDP port.
         * @param player_name Host display name.
         */
        void host(const std::string &port, const std::string &player_name);
        /**
         * @brief Joins a hosted session.
         * @param address Host address.
         * @param port Host UDP port.
         * @param player_name Local display name.
         * @param join_code Host-provided session code.
         */
        void join(const std::string &address, const std::string &port, const std::string &player_name, const std::string &join_code);
        /** @brief Stops the active host or client session. */
        void stop();
        /**
         * @brief Sends local state and receives remote updates.
         * @param local_state Current local gameplay state.
         * @param delta_time Frame duration in seconds.
         * @return Remote states received during this tick.
         */
        NetworkExchange exchange(const NetworkState &local_state, float delta_time);

        /** @brief Returns whether a host or client session is active. */
        [[nodiscard]] bool active() const;
        /** @brief Returns whether the local player has an established peer connection. */
        [[nodiscard]] bool connected() const;
        /** @brief Returns whether this session is the host. */
        [[nodiscard]] bool is_host() const;
        /** @brief Returns the assigned local player slot. */
        [[nodiscard]] std::uint8_t local_player_id() const;
        /** @brief Returns the number of connected players. */
        [[nodiscard]] std::size_t player_count() const;
        /** @brief Returns display names indexed by player slot. */
        [[nodiscard]] const std::array<std::string, NETWORK_PLAYER_COUNT> &player_names() const;
        /** @brief Returns connection flags indexed by player slot. */
        [[nodiscard]] const std::array<bool, NETWORK_PLAYER_COUNT> &player_connected() const;
        /** @brief Returns the session join code. */
        [[nodiscard]] const std::string &join_code() const;
        /** @brief Returns the automatic router port-mapping status. */
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
