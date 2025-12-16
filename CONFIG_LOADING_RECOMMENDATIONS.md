# Rekomendasi: Load Config dari File XML di Clixon VPP

## Status Saat Ini

Clixon VPP sudah memiliki infrastructure untuk XML config loading:

```
Datastore Location: /var/lib/clixon/vpp/
├── running_db       (Running config - format binary XMLDB)
├── candidate_db     (Candidate config - format binary XMLDB)
├── vpp_config.xml   (Exported XML config)
└── tmp_db           (Temporary)
```

Current XML format dari `/var/lib/clixon/vpp/vpp_config.xml`:
```xml
<config>
  <interfaces xmlns="http://example.com/vpp/interfaces">
    <interface>
      <name>HundredGigabitEthernet8a/0/1</name>
    </interface>
  </interfaces>
  <bonds xmlns="http://example.com/vpp/bonds">
    <bond>
      <name>BondEthernet10</name>
      <id>10</id>
      <mode>lacp</mode>
      <load-balance>l34</load-balance>
    </bond>
  </bonds>
</config>
```

---

## Opsi Loading Config (Recommended Priority)

### **OPSI 1: CLI-Based Load (RECOMMENDED - Safest & Easiest)**

**Cara:** Konversi XML config ke CLI commands, lalu feed ke `clixon_cli`

**Kelebihan:**
- ✅ Type-safe validation (YANG)
- ✅ Datastore sync otomatis
- ✅ Atomic commit (all-or-nothing)
- ✅ Can rollback if error
- ✅ Audit trail lengkap

**Implementasi:**
```bash
#!/bin/bash
# load-config-from-xml.sh
CONFIG_FILE="$1"

# Extract bonds dari XML dan convert ke CLI commands
bonds=$(xmllint --xpath "//*[local-name()='bond']" "$CONFIG_FILE")
interfaces=$(xmllint --xpath "//*[local-name()='interface']" "$CONFIG_FILE")

# Generate CLI script
{
    echo "configure terminal"
    
    # Create bonds
    while IFS= read -r bond_line; do
        name=$(echo "$bond_line" | xmllint --xpath "//*[local-name()='name']/text()" -)
        mode=$(echo "$bond_line" | xmllint --xpath "//*[local-name()='mode']/text()" -)
        lb=$(echo "$bond_line" | xmllint --xpath "//*[local-name()='load-balance']/text()" -)
        echo "interface bonding $name mode $mode load-balance $lb"
        echo "no shutdown"
        echo "exit"
    done <<< "$bonds"
    
    # Configure interfaces
    while IFS= read -r if_line; do
        name=$(echo "$if_line" | xmllint --xpath "//*[local-name()='name']/text()" -)
        enabled=$(echo "$if_line" | xmllint --xpath "//*[local-name()='enabled']/text()" -)
        mtu=$(echo "$if_line" | xmllint --xpath "//*[local-name()='mtu']/text()" -)
        
        echo "interface ethernet $name"
        [ "$enabled" = "true" ] && echo "no shutdown"
        [ -n "$mtu" ] && echo "mtu $mtu"
        echo "exit"
    done <<< "$interfaces"
    
    echo "commit"
    echo "end"
} | clixon_cli
```

**Contoh XML config untuk di-load:**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<config>
  <bonds xmlns="http://example.com/vpp/bonds">
    <bond>
      <name>BondEthernet0</name>
      <id>0</id>
      <mode>lacp</mode>
      <load-balance>l23</load-balance>
    </bond>
  </bonds>
  
  <interfaces xmlns="http://example.com/vpp/interfaces">
    <interface>
      <name>GigabitEthernet0/0/0</name>
      <enabled>true</enabled>
      <mtu>9000</mtu>
      <ipv4-address>
        <address>192.168.1.1</address>
        <prefix-length>24</prefix-length>
      </ipv4-address>
      <channel-group>0</channel-group>
    </interface>
    
    <interface>
      <name>GigabitEthernet0/0/1</name>
      <enabled>true</enabled>
      <channel-group>0</channel-group>
    </interface>
    
    <interface>
      <name>BondEthernet0</name>
      <enabled>true</enabled>
      <ipv4-address>
        <address>10.0.0.1</address>
        <prefix-length>24</prefix-length>
      </ipv4-address>
    </interface>
  </interfaces>
</config>
```

---

### **OPSI 2: Direct XMLDB Load (Advanced - Fast)**

**Cara:** Copy XML langsung ke running_db datastore

**Kelebihan:**
- ✅ Instant load (no CLI parsing)
- ✅ Efficient untuk large configs

**Kekurangan:**
- ❌ Bypass YANG validation
- ❌ No commit ceremony (direct apply)
- ❌ Risk data corruption

**Command:**
```bash
# Backup running config
cp /var/lib/clixon/vpp/running_db /var/lib/clixon/vpp/running_db.bak

# Load new config (perlu restart backend untuk apply)
clixon_restconf -i <<EOF
POST /restconf/yang-library:modules-state/module-set-id
$(cat config.xml)
EOF

# Atau via NETCONF
netconf-console --rpc <<'EOF'
<rpc xmlns="urn:ietf:params:xml:ns:netconf:base:1.0" message-id="1">
  <edit-config>
    <target><running/></target>
    <default-operation>replace</default-operation>
    <config>
      <!-- paste config XML here -->
    </config>
  </edit-config>
</rpc>
EOF
```

---

### **OPSI 3: RESTCONF API Load (Modern - Recommended for Integration)**

**Cara:** HTTP POST config ke RESTCONF endpoint

**Kelebihan:**
- ✅ Programmatic (JSON/XML)
- ✅ Standard IETF format
- ✅ Validation + commit
- ✅ Easy integration dengan automation tools

**Implementation:**
```bash
#!/bin/bash
# load-config-restconf.sh

CONFIG_FILE="$1"
RESTCONF_URL="http://localhost:8080/restconf"

# Load config via RESTCONF
curl -X POST \
  -H "Content-Type: application/yang-data+xml" \
  -H "Accept: application/yang-data+xml" \
  -d @"$CONFIG_FILE" \
  "$RESTCONF_URL/data"

# Validate
curl -X POST \
  "$RESTCONF_URL/operations/ietf-netconf:validate" \
  -H "Content-Type: application/yang-data+xml"

# Commit
curl -X POST \
  "$RESTCONF_URL/operations/ietf-netconf:commit" \
  -H "Content-Type: application/yang-data+xml"
```

**Example RESTCONF config XML:**
```xml
<data xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-datastores">
  <bonds xmlns="http://example.com/vpp/bonds">
    <bond>
      <name>BondEthernet0</name>
      <id>0</id>
      <mode>lacp</mode>
      <load-balance>l23</load-balance>
    </bond>
  </bonds>
</data>
```

---

### **OPSI 4: Python Clixon Library (Best for Complex Logic)**

**Cara:** Use pyclixon atau direct NETCONF dengan paramiko

**Implementation:**
```python
#!/usr/bin/env python3
import xml.etree.ElementTree as ET
from clixon import Clixon
import json

def load_config_from_xml(xml_file):
    """Load config from XML file using Clixon library"""
    
    # Parse XML
    tree = ET.parse(xml_file)
    root = tree.getroot()
    
    # Connect to clixon
    cli = Clixon()
    
    # Generate and execute CLI commands
    cli.cmd("configure terminal")
    
    # Process bonds
    for bond in root.findall(".//{http://example.com/vpp/bonds}bond"):
        name = bond.find("{http://example.com/vpp/bonds}name").text
        mode = bond.find("{http://example.com/vpp/bonds}mode").text
        lb = bond.find("{http://example.com/vpp/bonds}load-balance").text
        
        cli.cmd(f"interface bonding {name} mode {mode} load-balance {lb}")
        cli.cmd("no shutdown")
        cli.cmd("exit")
    
    # Process interfaces
    for iface in root.findall(".//{http://example.com/vpp/interfaces}interface"):
        name = iface.find("{http://example.com/vpp/interfaces}name").text
        enabled = iface.find("{http://example.com/vpp/interfaces}enabled")
        
        cli.cmd(f"interface ethernet {name}")
        if enabled is not None and enabled.text == "true":
            cli.cmd("no shutdown")
        cli.cmd("exit")
    
    # Commit
    cli.cmd("commit")
    cli.cmd("end")

if __name__ == "__main__":
    load_config_from_xml("config.xml")
```

---

## Rekomendasi Praktis

### **Untuk Development/Testing → OPSI 1 (CLI-based)**
- Paling aman dan transparent
- Bisa debug command by command
- Datastore sync otomatis

### **Untuk Automation/Integration → OPSI 3 (RESTCONF)**
- Fit dengan modern infrastructure
- JSON support untuk non-XML tools
- REST standard (mudah di-integrate)

### **Untuk High-Performance → OPSI 2 (Direct XMLDB)**
- Gunakan jika config sangat besar
- HATI-HATI: test di environment non-production dulu

### **Untuk Complex Migration Logic → OPSI 4 (Python Library)**
- Jika ada dependency checks
- Rollback logic
- Error recovery

---

## Struktur Config XML yang Recommended

```xml
<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:if="http://example.com/vpp/interfaces"
        xmlns:bond="http://example.com/vpp/bonds"
        xmlns:lcp="http://example.com/vpp/lcp">
  
  <!-- Bonds harus di-create terlebih dahulu (before interfaces) -->
  <bond:bonds>
    <bond:bond>
      <bond:name>BondEthernet0</bond:name>
      <bond:id>0</bond:id>
      <bond:mode>lacp</bond:mode>
      <bond:load-balance>l23</bond:load-balance>
    </bond:bond>
  </bond:bonds>
  
  <!-- Interfaces -->
  <if:interfaces>
    <!-- Physical interfaces yang akan di-add ke bond -->
    <if:interface>
      <if:name>GigabitEthernet0/0/0</if:name>
      <if:enabled>true</if:enabled>
      <if:mtu>9000</if:mtu>
      <if:channel-group>0</if:channel-group>
    </if:interface>
    
    <!-- Bond interface dengan IP -->
    <if:interface>
      <if:name>BondEthernet0</if:name>
      <if:enabled>true</if:enabled>
      <if:ipv4-address>
        <if:address>10.0.0.1</if:address>
        <if:prefix-length>24</if:prefix-length>
      </if:ipv4-address>
    </if:interface>
    
    <!-- VLAN sub-interface dengan LCP -->
    <if:interface>
      <if:name>BondEthernet0.100</if:name>
      <if:enabled>true</if:enabled>
      <if:vlan>100</if:vlan>
      <if:ipv4-address>
        <if:address>172.16.0.1</if:address>
        <if:prefix-length>24</if:prefix-length>
      </if:ipv4-address>
    </if:interface>
  </if:interfaces>
  
  <!-- LCP pairs -->
  <lcp:lcp-pairs>
    <lcp:lcp-pair>
      <lcp:vpp-interface>BondEthernet0.100</lcp:vpp-interface>
      <lcp:host-interface>vlan100</lcp:host-interface>
      <lcp:netns>ns-dataplane</lcp:netns>
    </lcp:lcp-pair>
  </lcp:lcp-pairs>
</config>
```

---

## Load Order yang Penting ⚠️

**URUTAN EXECUTION HARUS:**
1. ✅ Create bonds (sebelum add members)
2. ✅ Add physical interfaces ke bonds (channel-group)
3. ✅ Configure IP addresses
4. ✅ Create VLAN sub-interfaces
5. ✅ Create LCP pairs
6. ✅ Commit config

**JANGAN:**
- ❌ Add interface ke bond sebelum bond exist
- ❌ Configure IP pada loopback sebelum enable interface
- ❌ Create VLAN sub-interface dengan parent yang shutdown

---

## Next Steps

1. Pilih opsi mana yang paling sesuai dengan workflow Anda
2. Saya bisa implement complete solution untuk opsi yang dipilih
3. Test dengan config sample terlebih dahulu
4. Setup automation/scheduler jika diperlukan

**Mana yang Anda prefer?**

