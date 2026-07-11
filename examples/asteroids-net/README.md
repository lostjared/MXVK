# Asteroids Net

`asteroids-net` is the network multiplayer version of the MXVK 3D Asteroids example. Up to four players fly in the same match over UDP, with the host maintaining the authoritative session and relaying player state. The game includes the model-rendered ships and asteroids, projectiles, particle effects, starfield, audio, debug console, alternate camera/control modes, and optional CRT presentation from `asteroids3d`.

## Build and Run

Build from the repository root:

```bash
cmake -S . -B build
cmake --build build -j --target asteroids-net
./run.pl asteroids-net
```

Use `cmake --fresh -S . -B build` after installing optional networking libraries so CMake searches for them again.

Supported common arguments include:

- `-p <path>` or `--path <path>` - asset root, normally supplied by `run.pl`
- `-r <WxH>` or `--resolution <WxH>` - window resolution
- `-f` or `--fullscreen` - fullscreen mode
- `--enable-crt` - start with CRT post-processing enabled

## Hosting a Match

From the multiplayer lobby:

1. Select **Host**.
2. Enter a pilot name and UDP listen port. The default is `48120`.
3. Start hosting and give the displayed eight-character join code to the other players.
4. Also give remote players the public IP address and mapped port reported by the lobby. For LAN play, give them the host's LAN address instead.
5. After at least one player connects, start the match from the host lobby.

The join code identifies the session but is not encryption or strong authentication. Share it only with the intended players.

## Joining a Match

Select **Join**, then enter:

- your pilot name;
- the host's public IP address or DNS name for internet play, or LAN address for local play;
- the host's UDP port; and
- the eight-character join code supplied by the host.

The client sends UDP traffic to the host. Client-side router port forwarding is not normally required.

## Automatic Router Mapping

Internet hosts behind a typical home NAT router need an inbound UDP mapping. `asteroids-net` handles this automatically when either supported library is available:

1. UPnP IGD through `miniupnpc` is attempted first.
2. NAT-PMP through `libnatpmp` is used as the fallback.
3. The selected UDP mapping is removed when hosting stops or the application exits normally.

UPnP and NAT-PMP are two different router-control protocols. UPnP support on another device, such as a television or media server, does not mean the router provides the Internet Gateway Device service required for port mapping. NAT-PMP is normally answered by the default gateway and must be supported and enabled by that router.

### Required Optional Packages

Automatic mapping requires at least one of the following library packages. Installing both gives the host both available methods:

| Arch package | Build feature | Diagnostic command |
| --- | --- | --- |
| `miniupnpc` | UPnP IGD discovery, mapping, and cleanup | `upnpc` |
| `libnatpmp` | NAT-PMP mapping and cleanup | `natpmpc` |

On Arch Linux, install both optional dependencies with:

```bash
sudo pacman -S miniupnpc libnatpmp
cmake --fresh -S . -B build
cmake --build build -j --target asteroids-net
```

During configuration, CMake reports which paths are enabled:

```text
asteroids-net: UPnP automatic port mapping enabled
asteroids-net: NAT-PMP automatic port mapping enabled
```

Verify the installed Arch packages with:

```bash
pacman -Q miniupnpc libnatpmp
```

The CMake checks are independent and compile-time gated:

- UPnP is enabled only when `miniupnpc/miniupnpc.h` and the `miniupnpc` library are both found. Only then is `ASTEROIDS_NET_HAS_MINIUPNPC` defined.
- NAT-PMP is enabled only when `natpmp.h` and the `natpmp` library are both found. Only then is `ASTEROIDS_NET_HAS_NATPMP` defined.
- If one library is missing, its code is not compiled or linked and the other method remains available.
- If both are missing, the game still builds and supports LAN or manually forwarded connections.

The dependencies are optional. Without them, `asteroids-net` still builds and LAN multiplayer works, but internet hosts may need to forward the selected UDP port manually.

UPnP or NAT-PMP must also be enabled on the router. Automatic mapping creates a temporary forwarding rule; it does not bypass the router firewall or the internet provider's network.

### Checking Router Support

The installed command-line clients can test the router independently of the game:

```bash
upnpc -s
natpmpc
```

`upnpc -s` must find a valid UPnP Internet Gateway Device. Finding a generic UPnP root device is insufficient. `natpmpc` must receive a public-address response from the default gateway. These probes do not create a port mapping.

For the network used during development, the probes found a generic UPnP device at `192.168.1.250`, but it was not an Internet Gateway Device. The actual gateway at `192.168.1.1` returned that NAT-PMP was unsupported. On a network with those results, enable UPnP or NAT-PMP in the gateway configuration if available, or use one of the alternatives below. PCP is a newer mapping protocol, but it is not currently implemented by `asteroids-net`.

### Alternatives When Mapping Is Unavailable

- Manually forward UDP port `48120`, or the port selected in the host lobby, to the host computer.
- Use globally routable IPv6 and allow the selected UDP port through the router and host firewalls. IPv6 support also needs to be implemented/enabled in the game socket path; the current host path uses IPv4.
- Use an overlay network such as Tailscale or ZeroTier for a private game. Players connect using the overlay address.
- Use a public UDP relay for seamless operation through CGNAT or restrictive NAT. A normal request/response PHP web script can provide lobby discovery, but gameplay relay traffic requires a persistent UDP service.

## Troubleshooting Internet Hosting

- **The lobby says automatic mapping is unavailable:** Confirm `miniupnpc` and `libnatpmp` were detected during a fresh CMake configure and that UPnP or NAT-PMP is enabled in the router settings.
- **Mapping succeeds but players cannot connect:** Allow the game and selected UDP port through the host operating-system firewall. Confirm players are using the displayed public address and port rather than a private address such as `192.168.x.x`.
- **The router assigns a different public port:** Give players the mapped public port shown in the lobby, not only the local listen port.
- **There are two routers:** The upstream router also needs a mapping, or the downstream router should be placed in access-point/bridge mode.
- **The public address is in `100.64.0.0/10`, or it differs from the router's WAN address:** The connection may use carrier-grade NAT. UPnP and NAT-PMP cannot create a mapping through the provider's NAT. Use a public relay, request a public IP from the provider, or use an overlay network such as Tailscale or ZeroTier.
- **The application exited abnormally:** The router lease expires automatically, but its lifetime may vary by router. The application explicitly deletes it during a normal shutdown.

## Controls

- `Escape` - return to the intro/lobby from play, or leave the current screen
- `Space` or `Enter` - advance intro/menu actions and fire while playing
- `F1` - toggle the debug HUD
- `F2` - toggle arcade versus inverted pitch controls
- `F3` - open or close the in-game console
- `F5` - toggle classic keyboard versus keyboard/mouse controls
- `F7` - toggle chase versus first-person camera
- `F8` - toggle CRT post-processing
- `Left` / `Right` - yaw in classic keyboard mode
- `W` / `S` - pitch in classic keyboard mode; adjust speed in keyboard/mouse mode
- `A` / `D` - roll
- `Up` / `Down` - adjust speed in classic keyboard mode
- Mouse - look around in keyboard/mouse mode; left click fires
- Gamepad left stick - yaw
- Gamepad right stick - roll and pitch
- Gamepad shoulders or D-pad up/down - adjust speed

The host simulates the authoritative match state. Network packets are kept below a conservative UDP MTU payload budget, state is exchanged without blocking the render loop, and inactive peers time out automatically.
