# RESTCONF Configuration Loader for Clixon VPP

Complete implementation untuk load/save configuration via RESTCONF API dengan automatic backup, validation, dan error handling.

## Fitur

✅ **Multiple Format Support**
- XML dan JSON format
- Automatic format detection
- XML schema validation

✅ **Safety Features**
- Automatic backup before load
- Validate before commit
- Rollback on error
- Dry-run mode

✅ **Error Handling**
- Comprehensive error messages
- HTTP status code handling
- Authentication support
- SSL verification control

✅ **Logging & Audit**
- Detailed log file
- Timestamped operations
- Config statistics

## Quick Start

### Opsi 1: Bash Script

```bash
# Make executable
chmod +x scripts/restconf-config-loader.sh

# Load config with automatic commit
./scripts/restconf-config-loader.sh examples/config-lacp-bond.xml

# Load config but don't commit (review first)
./scripts/restconf-config-loader.sh examples/config-lacp-bond.xml --no-commit

# Validate only
./scripts/restconf-config-loader.sh examples/config-lacp-bond.xml --validate-only

# Dry run (preview what would be sent)
./scripts/restconf-config-loader.sh examples/config-lacp-bond.xml --dry-run
```

### Opsi 2: Python Script (Recommended)

```bash
# Install dependencies (one-time)
pip3 install requests

# Make executable
chmod +x scripts/restconf-loader.py

# Load config
./scripts/restconf-loader.py examples/config-lacp-bond.xml

# Load with custom RESTCONF URL
./scripts/restconf-loader.py examples/config-lacp-bond.xml \
    --restconf-url http://192.168.1.1:8080/restconf \
    --username admin \
    --password mypassword

# Validate only
./scripts/restconf-loader.py examples/config-lacp-bond.xml --validate-only

# No commit
./scripts/restconf-loader.py examples/config-lacp-bond.xml --no-commit

# Dry run
./scripts/restconf-loader.py examples/config-lacp-bond.xml --dry-run
```

## Configuration File Format

### XML Format (Recommended)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:bond="http://example.com/vpp/bonds"
        xmlns:if="http://example.com/vpp/interfaces"
        xmlns:lcp="http://example.com/vpp/lcp">
  
  <!-- Create bonds first -->
  <bond:bonds>
    <bond:bond>
      <bond:name>BondEthernet0</bond:name>
      <bond:id>0</bond:id>
      <bond:mode>lacp</bond:mode>
      <bond:load-balance>l23</bond:load-balance>
    </bond:bond>
  </bond:bonds>
  
  <!-- Configure interfaces -->
  <if:interfaces>
    <if:interface>
      <if:name>GigabitEthernet0/0/0</if:name>
      <if:enabled>true</if:enabled>
      <if:mtu>9000</if:mtu>
      <if:channel-group>0</if:channel-group>
      <if:description>Bond Member</if:description>
    </if:interface>
    
    <if:interface>
      <if:name>BondEthernet0</if:name>
      <if:enabled>true</if:enabled>
      <if:ipv4-address>
        <if:address>192.168.1.1</if:address>
        <if:prefix-length>24</if:prefix-length>
      </if:ipv4-address>
    </if:interface>
  </if:interfaces>
  
  <!-- LCP pairs for Linux integration -->
  <lcp:lcp-pairs>
    <lcp:lcp-pair>
      <lcp:vpp-interface>BondEthernet0</lcp:vpp-interface>
      <lcp:host-interface>bond0</lcp:host-interface>
      <lcp:netns>default</lcp:netns>
    </lcp:lcp-pair>
  </lcp:lcp-pairs>
  
</config>
```

### JSON Format

```json
{
  "config": {
    "bonds": {
      "bond": [
        {
          "name": "BondEthernet0",
          "id": 0,
          "mode": "lacp",
          "load-balance": "l23"
        }
      ]
    },
    "interfaces": {
      "interface": [
        {
          "name": "GigabitEthernet0/0/0",
          "enabled": true,
          "mtu": 9000,
          "channel-group": 0
        }
      ]
    }
  }
}
```

## Configuration Elements

### Bonds
```xml
<bond:bonds>
  <bond:bond>
    <bond:name>BondEthernet0</bond:name>        <!-- Interface name -->
    <bond:id>0</bond:id>                        <!-- Bond ID -->
    <bond:mode>lacp</bond:mode>                 <!-- lacp, xor, active-backup, round-robin, broadcast -->
    <bond:load-balance>l23</bond:load-balance>  <!-- l2, l23, l34 -->
  </bond:bond>
</bond:bonds>
```

### Interfaces
```xml
<if:interfaces>
  <if:interface>
    <if:name>GigabitEthernet0/0/0</if:name>      <!-- Interface name -->
    <if:enabled>true</if:enabled>               <!-- Admin state: true|false -->
    <if:mtu>9000</if:mtu>                       <!-- MTU (64-9216) -->
    <if:description>...</if:description>        <!-- Optional description -->
    <if:channel-group>0</if:channel-group>      <!-- Bond ID for membership (optional) -->
    <if:vlan>100</if:vlan>                      <!-- VLAN ID for sub-interface (optional) -->
    
    <!-- IPv4 Address (optional) -->
    <if:ipv4-address>
      <if:address>192.168.1.1</if:address>
      <if:prefix-length>24</if:prefix-length>
    </if:ipv4-address>
    
    <!-- IPv6 Address (optional) -->
    <if:ipv6-address>
      <if:address>2001:db8::1</if:address>
      <if:prefix-length>64</if:prefix-length>
    </if:ipv6-address>
  </if:interface>
</if:interfaces>
```

### LCP Pairs
```xml
<lcp:lcp-pairs>
  <lcp:lcp-pair>
    <lcp:vpp-interface>BondEthernet0</lcp:vpp-interface>  <!-- VPP interface -->
    <lcp:host-interface>bond0</lcp:host-interface>        <!-- Linux TAP name -->
    <lcp:netns>default</lcp:netns>                        <!-- Network namespace -->
  </lcp:lcp-pair>
</lcp:lcp-pairs>
```

## Load Order & Dependencies

⚠️ **PENTING: Order matters!**

**Correct Order:**
1. ✅ **Create bonds** (sebelum interfaces)
2. ✅ **Configure physical interfaces** + add to bonds (channel-group)
3. ✅ **Configure bond interface** dengan IP addresses
4. ✅ **Create VLAN sub-interfaces** on bonds/interfaces
5. ✅ **Configure LCP pairs**
6. ✅ **Commit** configuration

**JANGAN:**
- ❌ Add interface ke bond sebelum bond exist
- ❌ Configure VLAN sub-interface tanpa parent interface
- ❌ Create LCP pair untuk interface yang shutdown

**Recommended Structure:**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<config>
  <!-- 1. Bonds first -->
  <bonds>
    <bond>...</bond>
  </bonds>
  
  <!-- 2. All interfaces (physical + bonds + VLANs) -->
  <interfaces>
    <!-- Physical interfaces with channel-group -->
    <interface>...</interface>
    <!-- Bond interfaces -->
    <interface>...</interface>
    <!-- VLAN sub-interfaces -->
    <interface>...</interface>
  </interfaces>
  
  <!-- 3. LCP pairs last -->
  <lcp-pairs>
    <lcp-pair>...</lcp-pair>
  </lcp-pairs>
</config>
```

## Workflow Examples

### Example 1: Load dengan Validation

```bash
# Load dan validate, tapi jangan commit
./scripts/restconf-loader.py config.xml --no-commit

# Review hasilnya
clixon_cli -o set
  router# show running-config

# Jika OK, commit manual
clixon_cli
  router# configure terminal
  router# commit
  router# end
```

### Example 2: Backup & Restore

```bash
# Backup current config
curl -u admin:admin \
  -H "Accept: application/yang-data+xml" \
  http://localhost:8080/restconf/data > backup.xml

# Load dari backup
./scripts/restconf-loader.py backup.xml
```

### Example 3: Automation Script

```bash
#!/bin/bash
CONFIG_DIR="/etc/vpp/configs"

# Load all configs in order
for config in $CONFIG_DIR/*.xml; do
  echo "Loading $config..."
  ./scripts/restconf-loader.py "$config" || {
    echo "Failed to load $config"
    exit 1
  }
done

echo "All configs loaded successfully"
```

### Example 4: Dry Run untuk Preview

```bash
# Preview tanpa apply
./scripts/restconf-loader.py config.xml --dry-run

# Output:
# [INFO] DRY RUN MODE - Not sending to RESTCONF
# [INFO] Config that would be sent:
# [INFO]   <?xml version="1.0" encoding="UTF-8"?>
# [INFO]   <config>
# ...
```

## Troubleshooting

### Connection Error
```
[ERROR] RESTCONF unreachable (HTTP 404)
```

**Solutions:**
```bash
# Check jika clixon_backend running
ps aux | grep clixon

# Check jika clixon_restconf running
ps aux | grep restconf

# Verify URL
curl -v http://localhost:8080/restconf/yang-library

# Check firewall
sudo iptables -L | grep 8080
```

### Authentication Failed
```
[ERROR] Authentication failed (HTTP 401)
```

**Solutions:**
```bash
# Use correct credentials
./scripts/restconf-loader.py config.xml \
  --username admin \
  --password correctpassword

# Check clixon config
cat /etc/clixon/clixon-vpp.xml | grep -i auth
```

### Validation Error
```
[ERROR] Validation error (HTTP 422)
```

**Solutions:**
```bash
# Validate XML locally
xmllint config.xml

# Check YANG models
clixon_cli
  router# show yang-modules

# Test validate-only mode
./scripts/restconf-loader.py config.xml --validate-only
```

### Configuration Rejected
```
[ERROR] Bad request (400)
```

**Reasons:**
- XML/JSON malformed
- Required elements missing
- Data type mismatch

**Solutions:**
```bash
# Validate format
xmllint --schema schema.xsd config.xml

# Check example configs
cat examples/config-lacp-bond.xml

# Enable verbose logging
./scripts/restconf-loader.py config.xml \
  --log-file debug.log

# Review log
tail -f debug.log
```

## Advanced Options

### Custom RESTCONF URL
```bash
./scripts/restconf-loader.py config.xml \
  --restconf-url https://192.168.1.1:8443/restconf \
  --skip-ssl
```

### Long Timeout untuk Config Besar
```bash
./scripts/restconf-loader.py large-config.xml \
  --timeout 120
```

### Batch Loading dengan Error Handling
```bash
#!/bin/bash
set -e

configs=(
  "config1.xml"
  "config2.xml"
  "config3.xml"
)

for config in "${configs[@]}"; do
  echo "Loading $config..."
  ./scripts/restconf-loader.py "$config" || {
    echo "Failed on $config, stopping"
    exit 1
  }
  sleep 2
done
```

## Backup & Recovery

Backups otomatis di-save ke `/tmp/restconf-backups/`:

```bash
# List all backups
ls -lh /tmp/restconf-backups/

# Restore from backup
./scripts/restconf-loader.py /tmp/restconf-backups/running_config_20251216_154230.xml
```

## Integration dengan Systemd

```ini
# /etc/systemd/system/vpp-config-loader.service

[Unit]
Description=VPP Configuration Loader (RESTCONF)
After=clixon-backend.service clixon-restconf.service
Requires=clixon-backend.service

[Service]
Type=oneshot
User=vpp
ExecStart=/usr/local/bin/restconf-loader.py \
  /etc/vpp/config.xml \
  --no-commit
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Aktifkan:
```bash
sudo systemctl daemon-reload
sudo systemctl enable vpp-config-loader.service
sudo systemctl start vpp-config-loader.service
```

## Performance Tips

1. **Batch Configuration**
   - Combine related configs dalam 1 file
   - Kurangi jumlah API calls

2. **Large Configs**
   - Gunakan `--timeout 120` untuk configs besar
   - Split menjadi multiple files jika > 10MB

3. **Validate Offline**
   ```bash
   xmllint --noout config.xml
   ```

4. **Use JSON untuk Complex Data**
   - Lebih efficient untuk parsing
   - Native untuk automation tools

## Support & Documentation

- Lihat `/examples/` untuk complete config samples
- Check log files di `/tmp/restconf-loader-*.log`
- Review VPP YANG models di `/usr/local/share/clixon/`

