#!/bin/bash
#
# VPP Config Loader - Direct VPP CLI Mode
# Reads saved config from XML file and applies to VPP via vppctl
#

CONFIG_FILE="${VPP_CONFIG_FILE:-/var/lib/clixon/vpp/vpp_config.xml}"
LOG_FILE="/var/log/vpp/config-loader.log"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

vpp_exec() {
    local cmd="$1"
    log "Executing: vppctl $cmd"
    local result
    result=$(sudo vppctl -s /run/vpp/cli.sock "$cmd" 2>&1)
    local rc=$?
    if [ $rc -ne 0 ]; then
        log "Command failed (exit $rc): $result"
    fi
    return $rc
}

log "=== VPP Config Loader Started (Direct Mode) ==="

# Wait for VPP
log "Waiting for VPP to be ready..."
for i in {1..30}; do
    if sudo vppctl -s /run/vpp/cli.sock show version >/dev/null 2>&1; then
        log "VPP is ready"
        break
    fi
    sleep 1
done

if [ ! -f "$CONFIG_FILE" ]; then
    log "No config file found at $CONFIG_FILE"
    exit 0
fi

log "Loading configuration from $CONFIG_FILE"

# Apply Bonds first (using xmllint)
log "Processing bonds..."
bond_names=$(xmllint --xpath "//*[local-name()='bond-interface']/*[local-name()='name']/text()" "$CONFIG_FILE" 2>/dev/null)
for bond_name in $bond_names; do
    log "Processing bond: $bond_name"
    
    mode=$(xmllint --xpath "//*[local-name()='bond-interface'][*[local-name()='name' and text()='$bond_name']]/*[local-name()='mode']/text()" "$CONFIG_FILE" 2>/dev/null)
    lb=$(xmllint --xpath "//*[local-name()='bond-interface'][*[local-name()='name' and text()='$bond_name']]/*[local-name()='load-balance']/text()" "$CONFIG_FILE" 2>/dev/null)
    
    # Extract ID from name (BondEthernetXX -> XX)
    bond_id=$(echo "$bond_name" | sed 's/BondEthernet//')
    
    mode=${mode:-lacp}
    lb=${lb:-l2}
    
    log "Creating bond: mode=$mode id=$bond_id load-balance=$lb"
    vpp_exec "create bond mode $mode id $bond_id load-balance $lb"
    vpp_exec "set interface state $bond_name up"
    
    # Add members
    members=$(xmllint --xpath "//*[local-name()='bond-interface'][*[local-name()='name' and text()='$bond_name']]/*[local-name()='members']/*[local-name()='member']/text()" "$CONFIG_FILE" 2>/dev/null)
    for member in $members; do
        log "Adding member $member to $bond_name"
        vpp_exec "bond add $bond_name $member"
    done
done

# Apply Interfaces
log "Processing interfaces..."
if_names=$(xmllint --xpath "//*[local-name()='interface']/*[local-name()='name']/text()" "$CONFIG_FILE" 2>/dev/null)
for ifname in $if_names; do
    log "Processing interface: $ifname"
    
    # Set state
    enabled=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]/*[local-name()='enabled']/text()" "$CONFIG_FILE" 2>/dev/null)
    if [ "$enabled" = "true" ]; then
        log "Enabling interface: $ifname"
        vpp_exec "set interface state $ifname up"
    fi
    
    # Set MTU
    mtu=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]/*[local-name()='mtu']/text()" "$CONFIG_FILE" 2>/dev/null)
    if [ -n "$mtu" ]; then
        log "Setting MTU for $ifname: $mtu"
        vpp_exec "set interface mtu $mtu $ifname"
    fi
    
    # Set IPv4 address - try both old and new YANG structure
    ipaddr=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]//*[local-name()='ipv4']//*[local-name()='ip']/text()" "$CONFIG_FILE" 2>/dev/null)
    prefix=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]//*[local-name()='ipv4']//*[local-name()='prefix-length']/text()" "$CONFIG_FILE" 2>/dev/null)
    
    # Fallback to old structure
    if [ -z "$ipaddr" ]; then
        ipaddr=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]/*[local-name()='ipv4-address']/*[local-name()='address']/text()" "$CONFIG_FILE" 2>/dev/null)
        prefix=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]/*[local-name()='ipv4-address']/*[local-name()='prefix-length']/text()" "$CONFIG_FILE" 2>/dev/null)
    fi
    
    if [ -n "$ipaddr" ] && [ -n "$prefix" ]; then
        log "Setting IPv4 for $ifname: $ipaddr/$prefix"
        vpp_exec "set interface ip address $ifname $ipaddr/$prefix"
    fi
    
    # Set IPv6 address
    ipv6addr=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]//*[local-name()='ipv6']//*[local-name()='ip']/text()" "$CONFIG_FILE" 2>/dev/null)
    ipv6prefix=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]//*[local-name()='ipv6']//*[local-name()='prefix-length']/text()" "$CONFIG_FILE" 2>/dev/null)
    
    if [ -n "$ipv6addr" ] && [ -n "$ipv6prefix" ]; then
        log "Setting IPv6 for $ifname: $ipv6addr/$ipv6prefix"
        vpp_exec "set interface ip address $ifname $ipv6addr/$ipv6prefix"
    fi
done

# Apply LCPs
log "Processing LCPs..."
lcp_vpp_ifs=$(xmllint --xpath "//*[local-name()='lcp']/*[local-name()='vpp-interface']/text()" "$CONFIG_FILE" 2>/dev/null)
for vpp_if in $lcp_vpp_ifs; do
    host_if=$(xmllint --xpath "//*[local-name()='lcp'][*[local-name()='vpp-interface' and text()='$vpp_if']]/*[local-name()='host-interface']/text()" "$CONFIG_FILE" 2>/dev/null)
    netns=$(xmllint --xpath "//*[local-name()='lcp'][*[local-name()='vpp-interface' and text()='$vpp_if']]/*[local-name()='netns']/text()" "$CONFIG_FILE" 2>/dev/null)
    
    if [ -n "$host_if" ]; then
        if [ -n "$netns" ]; then
            log "Creating LCP: $vpp_if -> $host_if (netns: $netns)"
            vpp_exec "lcp create $vpp_if host-if $host_if netns $netns"
        else
            log "Creating LCP: $vpp_if -> $host_if"
            vpp_exec "lcp create $vpp_if host-if $host_if"
        fi
    fi
done

# Apply Sub-interfaces (VLAN)
log "Processing sub-interfaces..."
subif_names=$(xmllint --xpath "//*[local-name()='subinterface']/*[local-name()='name']/text()" "$CONFIG_FILE" 2>/dev/null)
for subif_name in $subif_names; do
    parent=$(xmllint --xpath "//*[local-name()='subinterface'][*[local-name()='name' and text()='$subif_name']]/*[local-name()='parent']/text()" "$CONFIG_FILE" 2>/dev/null)
    vlanid=$(xmllint --xpath "//*[local-name()='subinterface'][*[local-name()='name' and text()='$subif_name']]/*[local-name()='vlan-id']/text()" "$CONFIG_FILE" 2>/dev/null)
    
    if [ -n "$parent" ] && [ -n "$vlanid" ]; then
        log "Creating sub-interface: $subif_name (parent: $parent, vlan: $vlanid)"
        vpp_exec "create sub-interfaces $parent $vlanid dot1q $vlanid exact-match"
    fi
done

log "=== VPP Config Loader Complete ==="
