# Quick Reference - Clixon VPP CLI

## Build & Test

```bash
# Build
cd /home/well/clixon-vpp-control-plane
make clean && make

# Run tests
bash test_cli_api.sh              # VPP command tests
bash test_cli_comprehensive.sh    # Full integration tests

# Check compilation
ls -la src/*.o *.so
```

## Using Clixon VPP CLI

```bash
# Launch CLI
sudo clixon_cli -f /etc/clixon/clixon-vpp.xml

# Or with custom config
sudo clixon_cli -f /path/to/clixon-vpp.xml
```

### Example Session

```
debian# configure terminal
debian(config)# interface ethernet HundredGigabitEthernet8a/0/0
debian(config-if)# no shutdown
[HundredGigabitEthernet8a/0/0] Enabled
debian(config-if)# exit
debian(config)# commit
Configuration committed to /var/lib/clixon/vpp/vpp_config.xml
debian(config)# exit
debian#
```

## CLI Commands

### Interface Management

```bash
# Configure interface
configure terminal
interface ethernet <name>
interface loopback
interface vlan <parent> <vlan-id>
interface bonding <name> mode lacp load-balance l2

# Interface commands (in configure-if mode)
no shutdown          # Enable interface
shutdown             # Disable interface
mtu <size>          # Set MTU (e.g., 9000)
description <text>  # Set description
ip address <addr>   # Add IPv4 address
exit                # Exit interface mode
commit              # Save configuration
```

### Bond Management

```bash
configure terminal
interface bonding BondEthernet0 mode lacp load-balance l2
# (In configure-if mode)
channel-group 0     # Add member to bond
exit
commit
```

### Show Commands

```bash
show running-config     # Show active configuration
show interface brief    # List interfaces
show interface <name>   # Interface details
show bond              # Bond status
show lcp               # LCP pairs
```

## Configuration File

- **Main Config**: `/etc/clixon/clixon-vpp.xml`
- **Startup Config**: `/var/lib/clixon/vpp/vpp_config.xml`
- **Plugins Dir**: `/usr/local/lib/clixon/plugins/`

## VPP Commands (Direct)

```bash
# Show interfaces
sudo vppctl show interface

# Create bond
sudo vppctl create bond mode lacp id 0 load-balance l2

# Create loopback
sudo vppctl create loopback interface

# Configure interface
sudo vppctl set interface state <ifname> up
sudo vppctl set interface mtu 9000 <ifname>
sudo vppctl set interface ip address <ifname> 192.168.1.1/24

# Show bonds
sudo vppctl show bond

# Show LCP pairs
sudo vppctl show lcp
```

## API Layer Functions (For Development)

Located in `src/vpp_api.c`:

```c
// Bond operations
int vpp_cli_create_bond(const char *mode, const char *lb, uint32_t id, 
                        char *bondname_out, size_t bondname_len);
int vpp_cli_bond_add_member(const char *bond, const char *member);
int vpp_cli_bond_remove_member(const char *bond, const char *member);

// Interface operations
int vpp_cli_set_interface_state(const char *ifname, int is_up);
int vpp_cli_set_interface_mtu(const char *ifname, uint32_t mtu);
int vpp_cli_add_ip_address(const char *ifname, const char *address);
int vpp_cli_del_ip_address(const char *ifname, const char *address);

// Sub-interface (VLAN)
int vpp_cli_create_subif(const char *parent, uint32_t vlan_id,
                        char *subif_out, size_t subif_len);
int vpp_cli_delete_subif(const char *subif);

// Loopback
int vpp_cli_create_loopback(char *loopback_out, size_t loopback_len);
int vpp_cli_create_loopback_mac(const char *mac_addr, 
                                 char *loopback_out, size_t loopback_len);
int vpp_cli_delete_loopback(const char *loopback);

// LCP
int vpp_cli_create_lcp(const char *vpp_if, const char *host_if,
                       const char *netns);
int vpp_cli_delete_lcp(const char *vpp_if);
```

## Troubleshooting

### CLI won't start
```bash
# Check if config file exists
ls /etc/clixon/clixon-vpp.xml

# Check VPP is running
sudo vppctl show version

# Check permissions
sudo vppctl show interface  # Should work
```

### Commands fail
```bash
# Check error messages in CLI
# Enable debug logging
export CLIXON_DEBUG=1
sudo clixon_cli -f /etc/clixon/clixon-vpp.xml

# Test direct VPP command
sudo vppctl set interface mtu 9000 <ifname>
```

### Configuration not saved
```bash
# Check config directory
ls -la /var/lib/clixon/vpp/

# Create if missing
sudo mkdir -p /var/lib/clixon/vpp
sudo chown $(whoami):$(whoami) /var/lib/clixon/vpp
```

## Documentation

- [CLI API Integration](CLI_API_INTEGRATION.md)
- [Development Guide](DEVELOPMENT.md)
- [Test Results](TEST_RESULTS.md)

## Support

For issues or questions:
1. Check TEST_RESULTS.md for test output
2. Review CLI_API_INTEGRATION.md for architecture
3. Check DEVELOPMENT.md for detailed development info
4. Run test scripts to verify setup
