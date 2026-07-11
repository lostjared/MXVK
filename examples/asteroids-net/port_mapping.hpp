#ifndef ASTEROIDS_NET_PORT_MAPPING_HPP
#define ASTEROIDS_NET_PORT_MAPPING_HPP

/**
 * @file port_mapping.hpp
 * @brief Automatic UDP port mapping for hosted multiplayer sessions.
 */

#include <cstdint>
#include <string>

namespace space {

    /**
     * @class PortMapping
     * @brief Owns an automatically created UPnP or NAT-PMP port mapping.
     *
     * Destroying the object removes any mapping successfully created by open().
     */
    class PortMapping {
      public:
        /** @brief Creates an inactive port mapping. */
        PortMapping() = default;
        /** @brief Removes the active mapping, if one was created. */
        ~PortMapping();
        PortMapping(const PortMapping &) = delete;
        PortMapping &operator=(const PortMapping &) = delete;

        /**
         * @brief Attempts to expose a local UDP port through the router.
         * @param port Decimal UDP port number to map.
         */
        void open(const std::string &port);
        /** @brief Returns a human-readable description of the mapping result. */
        [[nodiscard]] const std::string &status() const;

      private:
        enum class Method {
            None,
            Upnp,
            NatPmp
        };

        Method method = Method::None;
        std::uint16_t private_port = 0;
        std::uint16_t public_port = 0;
        std::string status_message = "Automatic router mapping unavailable; forward the UDP port manually.";
    };

} // namespace space

#endif
