#include "port_mapping.hpp"

#include <charconv>
#include <chrono>
#include <thread>

#if defined(ASTEROIDS_NET_HAS_MINIUPNPC)
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

#if defined(ASTEROIDS_NET_HAS_NATPMP)
#include <natpmp.h>
#endif

namespace space {
    namespace {
        constexpr int MAPPING_LIFETIME_SECONDS = 7200;

        bool parse_port(const std::string &text, std::uint16_t &port) {
            unsigned int value = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0 || value > 65535)
                return false;
            port = static_cast<std::uint16_t>(value);
            return true;
        }

#if defined(ASTEROIDS_NET_HAS_NATPMP)
        int wait_for_natpmp(natpmp_t &natpmp, natpmpresp_t &response) {
            for (int attempt = 0; attempt < 12; ++attempt) {
                const int result = readnatpmpresponseorretry(&natpmp, &response);
                if (result >= 0)
                    return result;
                if (result != NATPMP_TRYAGAIN)
                    return result;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return NATPMP_TRYAGAIN;
        }

        std::string natpmp_error(const int error) {
            switch (error) {
            case NATPMP_ERR_NOGATEWAYSUPPORT:
                return "gateway does not support NAT-PMP";
            case NATPMP_ERR_NOTAUTHORIZED:
                return "NAT-PMP request was not authorized";
            case NATPMP_ERR_NETWORKFAILURE:
                return "NAT-PMP gateway reported a network failure";
            case NATPMP_ERR_OUTOFRESOURCES:
                return "NAT-PMP gateway has no mapping resources";
            case NATPMP_TRYAGAIN:
                return "NAT-PMP gateway did not respond";
            default:
                return "NAT-PMP request failed (" + std::to_string(error) + ")";
            }
        }
#endif

#if defined(ASTEROIDS_NET_HAS_MINIUPNPC)
        int get_valid_igd(UPNPDev *devices, UPNPUrls &urls, IGDdatas &data, char *local_address,
                          std::size_t local_address_size, char *external_address, std::size_t external_address_size) {
#if MINIUPNPC_API_VERSION >= 21
            return UPNP_GetValidIGD(devices, &urls, &data, local_address, static_cast<int>(local_address_size), external_address,
                                    static_cast<int>(external_address_size));
#else
            (void)external_address;
            (void)external_address_size;
            return UPNP_GetValidIGD(devices, &urls, &data, local_address, static_cast<int>(local_address_size));
#endif
        }
#endif
    } // namespace

    PortMapping::~PortMapping() {
        if (private_port == 0 || public_port == 0)
            return;
        const std::string port = std::to_string(public_port);
#if defined(ASTEROIDS_NET_HAS_MINIUPNPC)
        if (method == Method::Upnp) {
            int error = 0;
            UPNPDev *devices = upnpDiscover(1000, nullptr, nullptr, 0, 0, 2, &error);
            UPNPUrls urls{};
            IGDdatas data{};
            char local_address[64]{};
            char external_address[64]{};
            if (devices != nullptr &&
                get_valid_igd(devices, urls, data, local_address, sizeof(local_address), external_address, sizeof(external_address)) > 0) {
                UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "UDP", nullptr);
                FreeUPNPUrls(&urls);
            }
            if (devices != nullptr)
                freeUPNPDevlist(devices);
            return;
        }
#endif
#if defined(ASTEROIDS_NET_HAS_NATPMP)
        if (method == Method::NatPmp) {
            natpmp_t natpmp{};
            if (initnatpmp(&natpmp, 0, 0) >= 0) {
                sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_UDP, private_port, public_port, 0);
                natpmpresp_t response{};
                wait_for_natpmp(natpmp, response);
                closenatpmp(&natpmp);
            }
        }
#endif
    }

    void PortMapping::open(const std::string &port_text) {
        if (!parse_port(port_text, private_port)) {
            private_port = 0;
            status_message = "Invalid port; automatic router mapping skipped.";
            return;
        }
        public_port = private_port;
        const std::string port = std::to_string(private_port);
        std::string upnp_failure = "UPnP support was not built";
        std::string natpmp_failure = "NAT-PMP support was not built";

#if defined(ASTEROIDS_NET_HAS_MINIUPNPC)
        int error = 0;
        UPNPDev *devices = upnpDiscover(1500, nullptr, nullptr, 0, 0, 2, &error);
        UPNPUrls urls{};
        IGDdatas data{};
        char local_address[64]{};
        char external_address[64]{};
        const int igd_result = devices == nullptr
                                   ? 0
                                   : get_valid_igd(devices, urls, data, local_address, sizeof(local_address), external_address,
                                                   sizeof(external_address));
        if (igd_result > 0) {
            const int result = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, port.c_str(), port.c_str(),
                                                   local_address, "asteroids-net", "UDP", nullptr,
                                                   std::to_string(MAPPING_LIFETIME_SECONDS).c_str());
            if (result == UPNPCOMMAND_SUCCESS) {
                if (external_address[0] == '\0')
                    UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, external_address);
                method = Method::Upnp;
                status_message = std::string("UPnP mapped UDP ") + port;
                if (external_address[0] != '\0')
                    status_message += std::string(" at ") + external_address + ":" + port;
                status_message += "; waiting for players...";
                FreeUPNPUrls(&urls);
                freeUPNPDevlist(devices);
                return;
            }
            upnp_failure = std::string("UPnP mapping failed: ") + strupnperror(result);
            FreeUPNPUrls(&urls);
        } else if (devices == nullptr) {
            upnp_failure = "no UPnP devices responded";
        } else {
            upnp_failure = "no valid UPnP internet gateway";
        }
        if (devices != nullptr)
            freeUPNPDevlist(devices);
#endif

#if defined(ASTEROIDS_NET_HAS_NATPMP)
        natpmp_t natpmp{};
        const int init_result = initnatpmp(&natpmp, 0, 0);
        if (init_result >= 0) {
            const int sent = sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_UDP, private_port, public_port, MAPPING_LIFETIME_SECONDS);
            natpmpresp_t response{};
            const int response_result = sent < 0 ? sent : wait_for_natpmp(natpmp, response);
            if (response_result >= 0) {
                public_port = response.pnu.newportmapping.mappedpublicport;
                method = Method::NatPmp;
                status_message = "NAT-PMP mapped UDP " + std::to_string(public_port) + "; waiting for players...";
                closenatpmp(&natpmp);
                return;
            }
            natpmp_failure = natpmp_error(response_result);
            closenatpmp(&natpmp);
        } else {
            natpmp_failure = natpmp_error(init_result);
        }
#endif

        private_port = 0;
        public_port = 0;
        status_message = upnp_failure + "; " + natpmp_failure + ". Forward UDP " + port + " manually.";
    }

    const std::string &PortMapping::status() const { return status_message; }

} // namespace space
