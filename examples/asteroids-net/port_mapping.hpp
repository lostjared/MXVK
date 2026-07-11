#ifndef ASTEROIDS_NET_PORT_MAPPING_HPP
#define ASTEROIDS_NET_PORT_MAPPING_HPP

#include <cstdint>
#include <string>

namespace space {

    class PortMapping {
      public:
        PortMapping() = default;
        ~PortMapping();
        PortMapping(const PortMapping &) = delete;
        PortMapping &operator=(const PortMapping &) = delete;

        void open(const std::string &port);
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
