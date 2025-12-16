#!/bin/bash
#
# RESTCONF Config Loader - Quick Reference & Testing
#

cat << 'EOF'
╔═══════════════════════════════════════════════════════════════════════════╗
║           CLIXON VPP - RESTCONF Configuration Loader                    ║
║                                                                           ║
║  Load VPP configuration from XML/JSON files via RESTCONF API            ║
╚═══════════════════════════════════════════════════════════════════════════╝

┌─────────────────────────────────────────────────────────────────────────┐
│ QUICK START                                                             │
└─────────────────────────────────────────────────────────────────────────┘

📄 BASH VERSION (Simplest):
  $ ./scripts/restconf-config-loader.sh examples/config-lacp-bond.xml

🐍 PYTHON VERSION (Recommended):
  $ ./scripts/restconf-loader.py examples/config-lacp-bond.xml

┌─────────────────────────────────────────────────────────────────────────┐
│ COMMON USAGE PATTERNS                                                   │
└─────────────────────────────────────────────────────────────────────────┘

1️⃣  LOAD & AUTO-COMMIT (Production)
  $ ./scripts/restconf-loader.py config.xml
  ✅ Loads config and automatically commits

2️⃣  LOAD WITHOUT COMMIT (Review First)
  $ ./scripts/restconf-loader.py config.xml --no-commit
  ✅ Loads to candidate, you commit manually later

3️⃣  VALIDATE ONLY (Dry Run)
  $ ./scripts/restconf-loader.py config.xml --validate-only
  ✅ Tests validation without making changes

4️⃣  DRY RUN (Preview)
  $ ./scripts/restconf-loader.py config.xml --dry-run
  ✅ Shows what would be sent (no RESTCONF connection needed)

5️⃣  CUSTOM RESTCONF URL & CREDENTIALS
  $ ./scripts/restconf-loader.py config.xml \
      --restconf-url http://192.168.1.1:8080/restconf \
      --username myuser \
      --password mypass

6️⃣  HTTPS WITH SELF-SIGNED CERT
  $ ./scripts/restconf-loader.py config.xml \
      --restconf-url https://192.168.1.1:8443/restconf \
      --skip-ssl

7️⃣  LONGER TIMEOUT (Large Config)
  $ ./scripts/restconf-loader.py large-config.xml --timeout 120

┌─────────────────────────────────────────────────────────────────────────┐
│ AVAILABLE EXAMPLE CONFIGS                                               │
└─────────────────────────────────────────────────────────────────────────┘

📋 LACP Bond (XML):
  examples/config-lacp-bond.xml
  ├─ 2x physical interfaces (GigabitEthernet0/0/0, GigabitEthernet0/0/1)
  ├─ LACP Bond (BondEthernet0) with load-balance l23
  ├─ VLAN 100 sub-interface (with LCP pair in default namespace)
  └─ VLAN 200 sub-interface (with LCP pair in ns-dataplane)

📋 LACP Bond (JSON):
  examples/config-lacp-bond.json
  └─ Same config as XML but in JSON format

📋 Simple Interfaces (XML):
  examples/config-simple.xml
  ├─ Physical interfaces without bonding
  ├─ IPv4 and IPv6 configuration
  └─ Various MTU and description examples

┌─────────────────────────────────────────────────────────────────────────┐
│ TESTING WORKFLOW                                                        │
└─────────────────────────────────────────────────────────────────────────┘

Step 1: DRY RUN (No Impact)
  $ ./scripts/restconf-loader.py examples/config-lacp-bond.xml --dry-run
  ✓ See what config looks like
  ✓ No connection to RESTCONF needed

Step 2: VALIDATE ONLY (Safe Check)
  $ ./scripts/restconf-loader.py examples/config-lacp-bond.xml --validate-only
  ✓ Validates against YANG schema
  ✓ Shows any schema errors
  ✓ No persistent changes

Step 3: LOAD WITHOUT COMMIT (Review)
  $ ./scripts/restconf-loader.py examples/config-lacp-bond.xml --no-commit
  ✓ Loads to candidate datastore
  ✓ You can review with: clixon_cli
  ✓ Manual commit when ready

Step 4: FULL LOAD WITH COMMIT (Production)
  $ ./scripts/restconf-loader.py examples/config-lacp-bond.xml
  ✓ Loads and automatically commits
  ✓ Config is now running

┌─────────────────────────────────────────────────────────────────────────┐
│ LOGGING & DEBUGGING                                                     │
└─────────────────────────────────────────────────────────────────────────┘

Log files created automatically:
  /tmp/restconf-loader-<timestamp>.log

📊 Check last operation:
  $ tail -50 /tmp/restconf-loader-*.log | tail -100

🔍 Enable custom log location:
  $ ./scripts/restconf-loader.py config.xml \
      --log-file my-debug.log
  $ cat my-debug.log

┌─────────────────────────────────────────────────────────────────────────┐
│ BACKUP & RECOVERY                                                       │
└─────────────────────────────────────────────────────────────────────────┘

📁 Automatic backups location:
  /tmp/restconf-backups/

🔄 List all backups:
  $ ls -lh /tmp/restconf-backups/

💾 Restore from backup:
  $ ./scripts/restconf-loader.py /tmp/restconf-backups/running_config_20251216_154230.xml

📥 Manual backup (via curl):
  $ curl -u admin:admin \
      -H "Accept: application/yang-data+xml" \
      http://localhost:8080/restconf/data > my-backup.xml

┌─────────────────────────────────────────────────────────────────────────┐
│ VERIFICATION                                                            │
└─────────────────────────────────────────────────────────────────────────┘

✅ Check if RESTCONF is running:
  $ curl -u admin:admin http://localhost:8080/restconf/yang-library

✅ View running config:
  $ clixon_cli
    router# show running-config

✅ View candidate config (before commit):
  $ clixon_cli
    router# show candidate

✅ Compare candidate vs running:
  $ clixon_cli
    router# show running-config | diff - <(clixon_cli -o cd)

✅ Check applied VPP config:
  $ clixon_cli
    router# show interface brief
    router# show bond
    router# show lcp

┌─────────────────────────────────────────────────────────────────────────┐
│ TROUBLESHOOTING                                                         │
└─────────────────────────────────────────────────────────────────────────┘

❌ "RESTCONF unreachable"
   └─ Check: ps aux | grep -E "clixon_backend|clixon_restconf"
   └─ Check: curl http://localhost:8080/restconf/yang-library
   └─ Wait for services to start

❌ "Authentication failed"
   └─ Try: --username admin --password admin
   └─ Check: /etc/clixon/clixon-vpp.xml for auth config

❌ "XML validation failed"
   └─ Try: xmllint examples/config-lacp-bond.xml
   └─ Check: Log file for detailed error

❌ "Configuration error (400)"
   └─ Try: --validate-only to see YANG schema errors
   └─ Check: examples/ for correct format
   └─ Verify: All required elements present

❌ "Commit failed"
   └─ Try: --no-commit to load without commit
   └─ Then: clixon_cli → configure → validate
   └─ Check: Log file for constraint violations

┌─────────────────────────────────────────────────────────────────────────┐
│ FILE STRUCTURE                                                          │
└─────────────────────────────────────────────────────────────────────────┘

scripts/
  ├─ restconf-config-loader.sh    (Bash version - simple)
  ├─ restconf-loader.py           (Python version - recommended)
  └─ (This file)

examples/
  ├─ config-lacp-bond.xml         (LACP bond with VLAN & LCP)
  ├─ config-simple.xml            (Basic interfaces)
  └─ config-lacp-bond.json        (JSON version of LACP)

docs/
  └─ RESTCONF_CONFIG_LOADER.md    (Full documentation)

┌─────────────────────────────────────────────────────────────────────────┐
│ REAL-WORLD EXAMPLES                                                     │
└─────────────────────────────────────────────────────────────────────────┘

Example 1: Deploy Full Bonding Setup
  $ ./scripts/restconf-loader.py \
      --no-commit \
      examples/config-lacp-bond.xml
  $ clixon_cli
    router# show running-config | grep -A5 "BondEthernet"
    router# commit
    router# end

Example 2: Backup & Restore
  $ # Backup
  $ curl -u admin:admin http://localhost:8080/restconf/data > backup.xml
  $ # Later, restore
  $ ./scripts/restconf-loader.py backup.xml

Example 3: Automation (Ansible, Terraform, etc)
  $ ./scripts/restconf-loader.py $CONFIG_FILE \
      --restconf-url $RESTCONF_URL \
      --username $USER \
      --password $PASS

Example 4: Batch Loading
  $ for config in configs/*.xml; do
  $   ./scripts/restconf-loader.py "$config"
  $   sleep 2
  $ done

┌─────────────────────────────────────────────────────────────────────────┐
│ REQUIREMENTS CHECKLIST                                                  │
└─────────────────────────────────────────────────────────────────────────┘

✅ Prerequisites:
  □ clixon_backend running
  □ clixon_restconf running
  □ XML file with valid structure
  □ Correct RESTCONF URL & credentials

✅ For Python version:
  □ Python 3.6+
  □ requests library: pip3 install requests

✅ For Bash version:
  □ bash 4.0+
  □ curl
  □ xmllint
  □ grep, sed, etc

┌─────────────────────────────────────────────────────────────────────────┐
│ TIPS & BEST PRACTICES                                                   │
└─────────────────────────────────────────────────────────────────────────┘

💡 Always test with --validate-only first
💡 Use --no-commit for development/testing
💡 Check backups automatically created
💡 Review log files for detailed info
💡 Keep configs in version control
💡 Document configuration order/dependencies
💡 Test restore from backup regularly
💡 Use dry-run for previewing changes

┌─────────────────────────────────────────────────────────────────────────┐
│ ADDITIONAL RESOURCES                                                    │
└─────────────────────────────────────────────────────────────────────────┘

📖 Full Documentation:
   → RESTCONF_CONFIG_LOADER.md

🔗 RESTCONF Standard:
   → RFC 8040 - https://tools.ietf.org/html/rfc8040

🔗 NETCONF Standard:
   → RFC 6241 - https://tools.ietf.org/html/rfc6241

📚 Clixon Documentation:
   → https://github.com/clicon/clixon

🐳 VPP Documentation:
   → https://wiki.fd.io/display/vpp/VPP+home

EOF
