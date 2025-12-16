#!/bin/bash
#
# VPP Config Restore via RESTCONF
# Loads saved config file and applies via Clixon RESTCONF API
#

CONFIG_FILE="${1:-/var/lib/clixon/vpp/vpp_config.xml}"
RESTCONF_URL="${RESTCONF_URL:-http://localhost:8080/restconf}"

echo "=== VPP Config Restore via RESTCONF ==="
echo "Config file: $CONFIG_FILE"
echo "RESTCONF URL: $RESTCONF_URL"

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: Config file not found: $CONFIG_FILE"
    exit 1
fi

# Wait for RESTCONF to be ready
echo "Waiting for RESTCONF service..."
for i in {1..30}; do
    if curl -s "$RESTCONF_URL/data" > /dev/null 2>&1; then
        echo "RESTCONF is ready"
        break
    fi
    sleep 1
done

# Load config via RESTCONF
echo "Loading configuration..."

# Read config sections and apply each
config_content=$(cat "$CONFIG_FILE")

# Apply interfaces
echo "Applying interfaces..."
interfaces=$(echo "$config_content" | grep -oP '<interfaces[^>]*>.*?</interfaces>' | head -1)
if [ -n "$interfaces" ]; then
    curl -s -X PUT \
        -H "Content-Type: application/xml" \
        -d "$interfaces" \
        "$RESTCONF_URL/data/vpp-interfaces:interfaces" 2>&1
    echo " -> Interfaces applied"
fi

# Apply bonds
echo "Applying bonds..."
bonds=$(echo "$config_content" | grep -oP '<bonds[^>]*>.*?</bonds>' | head -1)
if [ -n "$bonds" ]; then
    curl -s -X PUT \
        -H "Content-Type: application/xml" \
        -d "$bonds" \
        "$RESTCONF_URL/data/vpp-bonds:bonds" 2>&1
    echo " -> Bonds applied"
fi

# Apply LCPs
echo "Applying LCPs..."
lcps=$(echo "$config_content" | grep -oP '<lcps[^>]*>.*?</lcps>' | head -1)
if [ -n "$lcps" ]; then
    curl -s -X PUT \
        -H "Content-Type: application/xml" \
        -d "$lcps" \
        "$RESTCONF_URL/data/vpp-lcp:lcps" 2>&1
    echo " -> LCPs applied"
fi

echo "=== Config Restore Complete ==="
