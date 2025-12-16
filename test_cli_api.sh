#!/bin/bash
# Test script untuk VPP CLI Integration

echo "=================================================="
echo "VPP CLI API Integration Test"
echo "=================================================="
echo ""

# Test 1: Show interfaces
echo "[TEST 1] Show existing VPP interfaces:"
sudo vppctl show interface | head -20
echo ""

# Test 2: Show bonds
echo "[TEST 2] Show existing bonds:"
sudo vppctl show bond 2>&1 | head -20
echo ""

# Test 3: Test CLI command parsing with bond creation
echo "[TEST 3] Test bond creation via API:"
echo "Creating bond with LACP mode and L2 load balance..."
sudo vppctl create bond mode lacp id 0 load-balance l2 2>&1 && echo "✓ Bond created successfully"
echo ""

# Test 4: Verify bond was created
echo "[TEST 4] Verify bond creation:"
sudo vppctl show interface BondEthernet0 2>&1 | head -20
echo ""

# Test 5: Create loopback
echo "[TEST 5] Create loopback interface:"
sudo vppctl create loopback interface 2>&1 && echo "✓ Loopback created"
echo ""

# Test 6: Set MTU on interface
echo "[TEST 6] Set MTU on loopback:"
sudo vppctl set interface mtu 9000 loop0 2>&1 && echo "✓ MTU set to 9000"
echo ""

# Test 7: Set interface state
echo "[TEST 7] Set interface state (up):"
sudo vppctl set interface state loop0 up 2>&1 && echo "✓ Interface up"
echo ""

# Test 8: Add IP address
echo "[TEST 8] Add IP address to loopback:"
sudo vppctl set interface ip address loop0 192.168.1.1/24 2>&1 && echo "✓ IP address added"
echo ""

# Test 9: Show interface details
echo "[TEST 9] Show loop0 details:"
sudo vppctl show interface loop0 2>&1
echo ""

# Test 10: Show running configuration
echo "[TEST 10] Verify configuration:"
sudo vppctl show interface loop0 addr 2>&1
echo ""

echo "=================================================="
echo "All tests completed!"
echo "=================================================="
