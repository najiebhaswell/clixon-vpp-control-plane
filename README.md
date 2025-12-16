# Clixon VPP Control Plane

A YANG-based control plane for VPP (Vector Packet Processing) using [Clixon](https://github.com/clicon/clixon) framework.

> **Inspired by Netgate TNSR** - This project follows the same architectural approach as [TNSR](https://www.tnsr.com/), using Clixon as the management framework for VPP.

## Overview

This project implements a management plane for **VPP 25.06** using Clixon, providing:
- **CLI** for interactive configuration (Cisco-like syntax)
- **RESTCONF** interface for REST API access (RFC 8040)
- **NETCONF** interface for network management (RFC 6241)
- **YANG** data models for:
  - Interface management
  - **LCP** (Linux Control Plane) interface pairs
  - **Bonding/LACP** interfaces
- **Configuration Persistence** - automatic restore after VPP restart

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
                          │ VPP CLI Socket
┌─────────────────────────▼────────────────────────────────────┐
│                       Data Plane                              │
│              ┌─────────────────────┐                         │
│              │     VPP 25.06       │                         │
│              └─────────────────────┘                         │
└──────────────────────────────────────────────────────────────┘
```

## Quick Start

### Installation via .deb Package

```bash
# Install the package
sudo dpkg -i clixon-vpp_1.1.0_amd64.deb
sudo apt-get install -f  # Fix dependencies if needed

# Enable and start services
sudo systemctl enable vpp clixon-vpp-backend
sudo systemctl start vpp clixon-vpp-backend

# Start CLI
sudo cli
```

### Building from Source

```bash
# Install dependencies
sudo ./scripts/install-deps.sh

# Build
make

# Install
sudo make install

# Build .deb package
./build-deb-from-source.sh
```

## CLI Usage

```bash
sudo cli

# Show commands
debian# show running-config
debian# show interface brief
debian# show bond
debian# show lcp

# Enter configuration mode
debian# configure terminal

# Interface configuration
debian(config)# interface ethernet HundredGigabitEthernet8a/0/0
debian(config-if)# no shutdown
debian(config-if)# ip address 192.168.1.1 24
debian(config-if)# exit

# Create loopback with instance
debian(config)# interface loopback 10
debian(config-if)# no shutdown
debian(config-if)# ip address 10.0.0.1 32
debian(config-if)# exit

# Create Bond interface
debian(config)# interface bonding 10 mode lacp load-balance l34
debian(config-if)# member HundredGigabitEthernet8a/0/0
debian(config-if)# no shutdown
debian(config-if)# exit

# Create VLAN sub-interface
debian(config)# interface vlan BondEthernet10 100
debian(config-if)# no shutdown
debian(config-if)# ip address 192.168.100.1 24
debian(config-if)# exit

# Create LCP pair
debian(config-if)# lcp host-if bond10

# Commit and save
debian(config)# commit
debian(config)# end
```

## Features

### Interface Management
- ✅ Physical interfaces (enable/disable, MTU, IP addresses)
- ✅ Loopback interfaces (with instance number)
- ✅ VLAN sub-interfaces (dot1q)
- ✅ IPv4 and IPv6 addresses
- ✅ Interface completion (tab)

### Bonding/LACP
- ✅ Create bond interfaces with ID
- ✅ Multiple modes (lacp, round-robin, active-backup, xor, broadcast)
- ✅ Load balancing (l2, l23, l34)
- ✅ Member interface management
- ✅ Bond completion (tab)

### Linux Control Plane (LCP)
- ✅ Create LCP pairs (VPP ↔ Linux TAP)
- ✅ Network namespace support
- ✅ Auto-subinterface LCP

### Configuration Persistence
- ✅ Configuration saved to `/var/lib/clixon/vpp/vpp_config.xml`
- ✅ Auto-restore after VPP restart
- ✅ Auto-restore after VPP crash
- ✅ Systemd integration

### CLI Features
- ✅ Cisco-like command syntax
- ✅ Tab completion for interfaces, bonds, loopbacks
- ✅ `show running-config` (includes interfaces, bonds, LCPs)
- ✅ Uncommitted changes warning on `end`
- ✅ Configuration validation

## Configuration Persistence

Configuration is automatically restored when:
1. **VPP restarts** - Clixon backend re-applies config
2. **VPP crashes** - Systemd restarts Clixon after VPP recovery

### Systemd Services

```bash
# Backend service
sudo systemctl status clixon-vpp-backend

# RESTCONF service (optional)
sudo systemctl status clixon-vpp-restconf

# VPP drop-in for crash recovery
cat /etc/systemd/system/vpp.service.d/restart-clixon.conf
```

## RESTCONF API

```bash
# Get all interfaces (STUB mode - returns sample data)
curl http://localhost:8080/restconf/data/vpp-interfaces:interfaces

# Enable an interface
curl -X PATCH http://localhost:8080/restconf/data/vpp-interfaces:interfaces/interface=loop0 \
  -H "Content-Type: application/yang-data+json" \
  -d '{"enabled": true}'

# Create bond interface
curl -X POST http://localhost:8080/restconf/data/vpp-bonding:bonding/bond-interface \
  -H "Content-Type: application/yang-data+json" \
  -d '{"name": "BondEthernet0", "mode": "lacp", "load-balance": "l34"}'
```

## Project Structure

```
clixon-vpp-control-plane/
├── README.md                    # This file
├── Makefile                     # Build configuration
├── build-deb-from-source.sh     # Build .deb package
├── config/
│   └── clixon-vpp.xml          # Clixon configuration
├── yang/
│   ├── vpp-interfaces.yang     # Interface YANG model
│   ├── vpp-lcp.yang            # LCP model
│   └── vpp-bonding.yang        # Bonding/LACP model
├── src/
│   ├── vpp_plugin.c            # Backend plugin (config persistence)
│   ├── vpp_cli_plugin.c        # CLI plugin (commands)
│   ├── vpp_api.c               # VPP API functions
│   ├── vpp_interface.c         # Interface operations
│   └── vpp_connection.c        # VPP connection management
├── cli/
│   ├── base_mode.cli           # Exec mode commands
│   ├── configure_mode.cli      # Config mode commands
│   └── configure_if_mode.cli   # Interface config commands
├── systemd/
│   ├── clixon-vpp-backend.service
│   ├── clixon-vpp-restconf.service
│   └── vpp.service.d/restart-clixon.conf
└── scripts/
    └── vpp-config-loader.sh    # Config loader script
```

## Prerequisites

- **VPP 25.06** with development packages (`vpp-dev`)
- **CLIgen** (CLI generator library)
- **Clixon 6.x** or later
- **libxml2-dev**, **libnghttp2-dev**
- GCC/Clang compiler

## License

Apache License 2.0

## References

- [Clixon GitHub](https://github.com/clicon/clixon)
- [CLIgen GitHub](https://github.com/clicon/cligen)
- [Clixon Documentation](https://clixon-docs.readthedocs.io/)
- [VPP Documentation](https://fd.io/documentation/)
- [Netgate TNSR YANG Models](https://github.com/Netgate/tnsr-yang-models)
