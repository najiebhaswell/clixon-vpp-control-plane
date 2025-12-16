# Clixon VPP Control Plane

A YANG-based control plane for VPP (Vector Packet Processing) using [Clixon](https://github.com/clicon/clixon) framework.

> **Inspired by Netgate TNSR** - This project follows the same architectural approach as [TNSR](https://www.tnsr.com/), using Clixon as the management framework for VPP.

## Overview

This project implements a management plane for **VPP 25.06** using Clixon, providing:
- **NETCONF** interface for network management (RFC 6241)
- **RESTCONF** interface for REST API access (RFC 8040)
- **CLI** for interactive configuration
- **YANG** data models for:
  - Interface management
  - **LCP** (Linux Control Plane) interface pairs
  - **Bonding/LACP** interfaces

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    Management Interfaces                      │
│   ┌─────────┐      ┌──────────┐      ┌─────────┐            │
│   │   CLI   │      │ RESTCONF │      │ NETCONF │            │
│   └────┬────┘      └────┬─────┘      └────┬────┘            │
│        └────────────────┼─────────────────┘                  │
│                         ▼                                     │
│              ┌─────────────────────┐                         │
│              │   Clixon Backend    │                         │
│              │   clixon_backend    │                         │
│              └──────────┬──────────┘                         │
│                         │                                     │
│              ┌──────────▼──────────┐                         │
│              │   VPP Plugin        │                         │
│              │   vpp_plugin.so     │                         │
│              └──────────┬──────────┘                         │
└─────────────────────────┼────────────────────────────────────┘
                          │ VAPI (Shared Memory)
┌─────────────────────────▼────────────────────────────────────┐
│                       Data Plane                              │
│              ┌─────────────────────┐                         │
│              │     VPP 25.06       │                         │
│              └─────────────────────┘                         │
└──────────────────────────────────────────────────────────────┘
```

## Project Structure

```
clixon-vpp-control-plane/
├── README.md                    # This file
├── Makefile                     # Build configuration
├── config/
│   └── clixon-vpp.xml          # Clixon configuration
├── yang/
│   ├── vpp-interfaces.yang     # Interface YANG model
│   ├── vpp-lcp.yang            # LCP (Linux Control Plane) model
│   └── vpp-bonding.yang        # Bonding/LACP model
├── src/
│   ├── vpp_plugin.c            # Main Clixon backend plugin
│   ├── vpp_interface.c         # VPP interface operations
│   ├── vpp_interface.h         # Interface operations header
│   ├── vpp_connection.c        # VPP connection management
│   └── vpp_connection.h        # Connection header
├── scripts/
│   ├── install-deps.sh         # Install CLIgen, Clixon, VPP
│   └── start-services.sh       # Start/stop services
├── doc/
│   └── DEVELOPMENT.md          # Development guide
└── reference/
    └── tnsr-yang-models/       # Netgate TNSR YANG reference
```

## Prerequisites

- **VPP 25.06** with development packages (`vpp-dev`)
- **CLIgen** (CLI generator library)
- **Clixon 6.x** or later
- GCC/Clang compiler
- Make, pkg-config

## Quick Start

1. **Install dependencies:**
   ```bash
   sudo ./scripts/install-deps.sh
   ```
   This will install:
   - CLIgen from https://github.com/clicon/cligen.git
   - Clixon from https://github.com/clicon/clixon.git
   - VPP development packages (optional)

2. **Build the plugin:**
   ```bash
   make
   ```

3. **Install:**
   ```bash
   sudo make install
   ```

4. **Start services:**
   ```bash
   sudo ./scripts/start-services.sh start
   ```

## Supported Features

### Interface Management (`vpp-interfaces.yang`)
- [x] List interfaces
- [x] Enable/disable interface (admin status)
- [x] Set MTU
- [x] Configure IPv4/IPv6 addresses
- [x] View interface statistics
- [x] Create/delete loopback interfaces
- [ ] Create sub-interfaces
- [ ] VLAN configuration

### Linux Control Plane (`vpp-lcp.yang`)
- [x] Create LCP interface pairs (VPP ↔ Linux TAP)
- [x] Configure host interface names
- [x] Network namespace support
- [x] Auto-create option
- [ ] Sync from Linux (Netlink)

### Bonding/LACP (`vpp-bonding.yang`)
- [x] Create bond interfaces
- [x] Multiple bonding modes (round-robin, active-backup, XOR, broadcast, LACP)
- [x] Load balancing algorithms (L2, L34, L23)
- [x] Slave interface management
- [x] LACP configuration (rate, system-priority)
- [ ] LACP state monitoring

## Usage Examples

### CLI
```bash
clixon_cli -f /etc/clixon/clixon-vpp.xml

> show interfaces
> configure
# set interfaces interface GigabitEthernet0/8/0 enabled true
# set interfaces interface GigabitEthernet0/8/0 mtu 1500
# set interfaces interface GigabitEthernet0/8/0 ipv4 address 192.168.1.1/24
# commit
```

### RESTCONF
```bash
# Get all interfaces
curl http://localhost:8080/restconf/data/vpp-interfaces:interfaces

# Enable an interface  
curl -X PATCH http://localhost:8080/restconf/data/vpp-interfaces:interfaces/interface=loop0 \
  -H "Content-Type: application/yang-data+json" \
  -d '{"enabled": true}'

# Create LCP pair
curl -X POST http://localhost:8080/restconf/data/vpp-lcp:lcp/interface-pairs \
  -H "Content-Type: application/yang-data+json" \
  -d '{"pair": {"phy-interface": "GigabitEthernet0/8/0", "host-interface": "eth0"}}'

# Create bond interface
curl -X POST http://localhost:8080/restconf/data/vpp-bonding:bonding/bond-interface \
  -H "Content-Type: application/yang-data+json" \
  -d '{"name": "BondEthernet0", "mode": "lacp", "load-balance": "l34"}'
```

## Service Management

```bash
# Start all services
sudo ./scripts/start-services.sh start

# Check status
sudo ./scripts/start-services.sh status

# Stop services
sudo ./scripts/start-services.sh stop

# Start CLI
sudo ./scripts/start-services.sh cli
```

## Development

See [doc/DEVELOPMENT.md](doc/DEVELOPMENT.md) for detailed development guide including:
- Plugin architecture
- VAPI usage examples
- YANG model development
- Debugging tips

## License

Apache License 2.0

## References

- [Clixon GitHub](https://github.com/clicon/clixon)
- [CLIgen GitHub](https://github.com/clicon/cligen)
- [Clixon Documentation](https://clixon-docs.readthedocs.io/)
- [VPP Documentation](https://fd.io/documentation/)
- [Netgate TNSR YANG Models](https://github.com/Netgate/tnsr-yang-models)
- [IETF YANG Interfaces Model (RFC 8343)](https://datatracker.ietf.org/doc/html/rfc8343)

