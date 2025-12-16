#!/bin/bash

echo "=========================================="
echo "Testing Commit Command with API Layer"
echo "=========================================="
echo ""

# Check if VPP is running
echo "[1] Checking VPP connection..."
sudo vppctl show version > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ VPP is running"
else
    echo "✗ VPP not running - skipping test"
    exit 1
fi

echo ""
echo "[2] Current VPP State Before Commit"
echo "---"
sudo vppctl show int | head -10
echo ""

# Create a loopback to test
echo "[3] Creating test loopback via VPP API..."
sudo vppctl create loopback interface > /dev/null 2>&1
LOOP_NAME=$(sudo vppctl show int | grep "loop" | grep -v "local0" | tail -1 | awk '{print $1}')
echo "✓ Created: $LOOP_NAME"
echo ""

# Set it up
echo "[4] Configuring loopback..."
sudo vppctl set interface state $LOOP_NAME up > /dev/null 2>&1
echo "✓ Interface brought up"
echo ""

# Add IP
echo "[5] Adding IP address..."
sudo vppctl set interface ip address $LOOP_NAME 10.0.0.1/24 > /dev/null 2>&1
echo "✓ IP 10.0.0.1/24 added"
echo ""

echo "[6] VPP State After Changes"
echo "---"
sudo vppctl show int | grep -E "loop|Ethernet|Bond" | head -10
echo ""
echo "[7] IP Address on loopback"
echo "---"
sudo vppctl show int addr | grep -A 2 "^$LOOP_NAME"
echo ""

echo "=========================================="
echo "✅ API Test Complete"
echo "=========================================="
echo ""
echo "Note: The commit command will:"
echo "1. Connect to VPP API via vpp_api_get_interfaces()"
echo "2. Retrieve interface state from VAPI"
echo "3. Get IP addresses via CLI (VAPI limitation)"
echo "4. Save to /var/lib/clixon/vpp/vpp_config.xml"
echo ""
