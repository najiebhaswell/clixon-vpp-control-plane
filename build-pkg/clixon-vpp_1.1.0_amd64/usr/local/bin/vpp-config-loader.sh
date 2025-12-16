#!/bin/bash
#
# VPP Config Loader - Restores VPP configuration after restart
# Reads saved config from XML file and applies to VPP via socket
#

CONFIG_FILE="${VPP_CONFIG_FILE:-/var/lib/clixon/vpp/vpp_config.xml}"
LOG_FILE="/var/log/vpp/config-loader.log"
VPP_SOCK="/run/vpp/cli.sock"

mkdir -p "$(dirname "$LOG_FILE")" 2>/dev/null

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

vpp_exec() {
    local cmd="$1"
    log "  -> vppctl: $cmd"
    sudo vppctl -s "$VPP_SOCK" "$cmd" 2>&1
}

wait_for_vpp() {
    log "Waiting for VPP to be ready..."
    for i in {1..120}; do
        if sudo vppctl -s "$VPP_SOCK" show version >/dev/null 2>&1; then
            local ver=$(sudo vppctl -s "$VPP_SOCK" show version 2>/dev/null | head -1)
            log "VPP is ready: $ver"
            return 0
        fi
        sleep 1
    done
    log "ERROR: VPP not ready after 120 seconds"
    return 1
}

log "=========================================="
log "VPP Config Loader Started"
log "Config file: $CONFIG_FILE"
log "=========================================="

# Wait for VPP
wait_for_vpp || exit 1

if [ ! -f "$CONFIG_FILE" ]; then
    log "No config file found at $CONFIG_FILE - nothing to restore"
    exit 0
fi

log "Loading configuration..."

# ================================================================
# 1. Create Bonds
# ================================================================
log "=== Step 1: Creating bonds ==="
bond_count=0

# Use xmllint to extract bond info
for bond_name in $(xmllint --xpath '//*[local-name()="bond"]/*[local-name()="name"]/text()' "$CONFIG_FILE" 2>/dev/null); do
    [ -z "$bond_name" ] && continue
    
    mode=$(xmllint --xpath "//*[local-name()='bond'][*[local-name()='name' and text()='$bond_name']]/*[local-name()='mode']/text()" "$CONFIG_FILE" 2>/dev/null)
    lb=$(xmllint --xpath "//*[local-name()='bond'][*[local-name()='name' and text()='$bond_name']]/*[local-name()='load-balance']/text()" "$CONFIG_FILE" 2>/dev/null)
    members=$(xmllint --xpath "//*[local-name()='bond'][*[local-name()='name' and text()='$bond_name']]/*[local-name()='members']/text()" "$CONFIG_FILE" 2>/dev/null)
    
    # Extract ID from name (BondEthernetXX -> XX)
    bond_id=$(echo "$bond_name" | sed 's/BondEthernet//')
    mode=${mode:-lacp}
    lb=${lb:-l2}
    
    log "Creating bond: $bond_name (mode=$mode, lb=$lb, id=$bond_id)"
    
    # Check if bond already exists
    if sudo vppctl -s "$VPP_SOCK" show interface "$bond_name" 2>/dev/null | grep -q "$bond_name"; then
        log "  Bond $bond_name already exists"
    else
        vpp_exec "create bond mode $mode id $bond_id load-balance $lb"
    fi
    
    # Enable bond
    vpp_exec "set interface state $bond_name up"
    
    # Add members (comma-separated)
    if [ -n "$members" ]; then
        IFS=',' read -ra member_arr <<< "$members"
        for member in "${member_arr[@]}"; do
            member=$(echo "$member" | xargs)  # trim whitespace
            [ -z "$member" ] && continue
            log "  Adding member $member to $bond_name"
            vpp_exec "set interface state $member up"
            vpp_exec "bond add $bond_name $member"
        done
    fi
    
    ((bond_count++))
done

log "Bonds created: $bond_count"

# ================================================================
# 2. Configure Interfaces
# ================================================================
log "=== Step 2: Configuring interfaces ==="
if_count=0

for ifname in $(xmllint --xpath '//*[local-name()="interface"]/*[local-name()="name"]/text()' "$CONFIG_FILE" 2>/dev/null); do
    [ -z "$ifname" ] && continue
    [[ "$ifname" == tap* ]] && continue  # Skip tap interfaces
    
    log "Configuring interface: $ifname"
    
    # Set state
    enabled=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]/*[local-name()='enabled']/text()" "$CONFIG_FILE" 2>/dev/null)
    if [ "$enabled" = "true" ]; then
        vpp_exec "set interface state $ifname up"
    fi
    
    # Set MTU
    mtu=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]/*[local-name()='mtu']/text()" "$CONFIG_FILE" 2>/dev/null)
    if [ -n "$mtu" ] && [ "$mtu" != "" ]; then
        vpp_exec "set interface mtu $mtu $ifname"
    fi
    
    # Set IPv4 address
    ipv4=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]//*[local-name()='ipv4-address']/*[local-name()='address']/text()" "$CONFIG_FILE" 2>/dev/null)
    ipv4_prefix=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]//*[local-name()='ipv4-address']/*[local-name()='prefix-length']/text()" "$CONFIG_FILE" 2>/dev/null)
    if [ -n "$ipv4" ] && [ -n "$ipv4_prefix" ]; then
        vpp_exec "set interface ip address $ifname $ipv4/$ipv4_prefix"
    fi
    
    # Set IPv6 address
    ipv6=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]//*[local-name()='ipv6-address']/*[local-name()='address']/text()" "$CONFIG_FILE" 2>/dev/null)
    ipv6_prefix=$(xmllint --xpath "//*[local-name()='interface'][*[local-name()='name' and text()='$ifname']]//*[local-name()='ipv6-address']/*[local-name()='prefix-length']/text()" "$CONFIG_FILE" 2>/dev/null)
    if [ -n "$ipv6" ] && [ -n "$ipv6_prefix" ]; then
        vpp_exec "set interface ip address $ifname $ipv6/$ipv6_prefix"
    fi
    
    ((if_count++))
done

log "Interfaces configured: $if_count"

# ================================================================
# 3. Create LCPs
# ================================================================
log "=== Step 3: Creating LCPs ==="
lcp_count=0

for vpp_if in $(xmllint --xpath '//*[local-name()="lcp"]/*[local-name()="vpp-interface"]/text()' "$CONFIG_FILE" 2>/dev/null); do
    [ -z "$vpp_if" ] && continue
    
    host_if=$(xmllint --xpath "//*[local-name()='lcp'][*[local-name()='vpp-interface' and text()='$vpp_if']]/*[local-name()='host-interface']/text()" "$CONFIG_FILE" 2>/dev/null)
    netns=$(xmllint --xpath "//*[local-name()='lcp'][*[local-name()='vpp-interface' and text()='$vpp_if']]/*[local-name()='netns']/text()" "$CONFIG_FILE" 2>/dev/null)
    
    [ -z "$host_if" ] && continue
    
    # Check if LCP already exists
    if sudo vppctl -s "$VPP_SOCK" show lcp 2>/dev/null | grep -q "$vpp_if"; then
        log "  LCP for $vpp_if already exists"
    else
        if [ -n "$netns" ]; then
            log "  Creating LCP: $vpp_if -> $host_if (netns: $netns)"
            vpp_exec "lcp create $vpp_if host-if $host_if netns $netns"
        else
            log "  Creating LCP: $vpp_if -> $host_if"
            vpp_exec "lcp create $vpp_if host-if $host_if"
        fi
    fi
    ((lcp_count++))
done

log "LCPs created: $lcp_count"

log "=========================================="
log "VPP Config Loader Complete"
log "  Bonds: $bond_count"
log "  Interfaces: $if_count"
log "  LCPs: $lcp_count"
log "=========================================="
