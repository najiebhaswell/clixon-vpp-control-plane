# Development Guide

This document provides detailed information for developing and extending the Clixon VPP Control Plane.

## Architecture Overview

```
                                  ┌───────────────────┐
                                  │   Management      │
                                  │   Applications    │
                                  └────────┬──────────┘
                                           │
              ┌────────────────────────────┼────────────────────────────┐
              │                            │                            │
    ┌─────────▼─────────┐      ┌──────────▼──────────┐      ┌─────────▼─────────┐
    │   Clixon CLI      │      │  RESTCONF Server    │      │  NETCONF Server   │
    │   (clixon_cli)    │      │ (clixon_restconf)   │      │  (clixon_netconf) │
    └─────────┬─────────┘      └──────────┬──────────┘      └─────────┬─────────┘
              │                            │                            │
              └────────────────────────────┼────────────────────────────┘
                                           │
                              ┌────────────▼────────────┐
                              │    Clixon Backend       │
                              │   (clixon_backend)      │
                              │                         │
                              │  ┌───────────────────┐  │
                              │  │  YANG Datastore   │  │
                              │  │  (XML/JSON)       │  │
                              │  └───────────────────┘  │
                              └────────────┬────────────┘
                                           │
                              ┌────────────▼────────────┐
                              │    VPP Plugin           │
                              │   (vpp_plugin.so)       │
                              │                         │
                              │  • Transaction handlers │
                              │  • State data callbacks │
                              │  • RPC handlers         │
                              └────────────┬────────────┘
                                           │
                                   VAPI (Shared Memory)
                                           │
                              ┌────────────▼────────────┐
                              │         VPP             │
                              │   (Vector Packet        │
                              │    Processing)          │
                              └─────────────────────────┘
```

## Plugin Lifecycle

### Initialization Flow

1. **clixon_plugin_init()**: Called when plugin is loaded
   - Return plugin API structure
   - Register RPC callbacks
   - Initialize global state

2. **start callback**: Called when backend starts
   - Connect to VPP
   - Initialize data structures

3. **exit callback**: Called on shutdown
   - Disconnect from VPP
   - Clean up resources

### Transaction Flow

```
User commits config
        │
        ▼
┌───────────────────┐
│  trans_begin()    │  ─── Ensure VPP connection
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ trans_validate()  │  ─── Validate config against VPP capabilities
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  trans_commit()   │  ─── Apply config to VPP via VAPI
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  trans_end()      │  ─── Finalize transaction (optional)
└───────────────────┘
```

## VAPI Usage

### Connection Management

```c
#include <vapi/vapi.h>

vapi_ctx_t ctx;
vapi_error_e rv;

// Allocate context
rv = vapi_ctx_alloc(&ctx);

// Connect to VPP
rv = vapi_connect(ctx, 
                  "my-client",      // client name
                  NULL,             // chroot prefix
                  32,               // request queue size
                  32,               // response queue size
                  VAPI_MODE_BLOCKING,
                  true);            // handle keepalives

// Do work...

// Disconnect
vapi_disconnect(ctx);
vapi_ctx_free(ctx);
```

### Sending Requests

```c
#include <vapi/interface.api.vapi.h>

// Allocate message
vapi_msg_sw_interface_set_flags *msg;
msg = vapi_alloc_sw_interface_set_flags(ctx);

// Fill in payload
msg->payload.sw_if_index = 1;
msg->payload.flags = IF_STATUS_API_FLAG_ADMIN_UP;

// Send (blocking mode)
rv = vapi_sw_interface_set_flags(ctx, msg, NULL, NULL);
```

### Handling Dumps (Multi-response)

```c
typedef struct {
    vpp_interface_info_t *head;
} dump_ctx_t;

static vapi_error_e
dump_callback(struct vapi_ctx_s *ctx,
              void *callback_ctx,
              vapi_error_e rv,
              bool is_last,
              vapi_payload_sw_interface_details *reply)
{
    if (is_last) return VAPI_OK;
    
    dump_ctx_t *dump = (dump_ctx_t *)callback_ctx;
    // Process reply...
    return VAPI_OK;
}

// Send dump request
vapi_msg_sw_interface_dump *msg;
msg = vapi_alloc_sw_interface_dump(ctx);
msg->payload.sw_if_index = ~0;  // all interfaces

dump_ctx_t dump = { NULL };
vapi_sw_interface_dump(ctx, msg, dump_callback, &dump);
```

## YANG Model Development

### Adding a New Feature

1. **Define YANG model** in `yang/` directory
2. **Add to configuration** in `config/clixon-vpp.xml`
3. **Implement C handlers** in `src/`
4. **Register callbacks** in `vpp_plugin.c`

### YANG Best Practices

- Use IETF standard types where possible (`ietf-inet-types`, `ietf-yang-types`)
- Separate config and operational state
- Add meaningful descriptions
- Use `when` statements for conditional features
- Define RPCs for imperative operations

### Example: Adding VXLAN Support

```yang
// yang/vpp-vxlan.yang
module vpp-vxlan {
    namespace "http://example.com/vpp/vxlan";
    prefix vpp-vxlan;
    
    container vxlan-tunnels {
        list tunnel {
            key "name";
            
            leaf name { type string; }
            leaf vni { type uint32; }
            leaf src-address { type inet:ip-address; }
            leaf dst-address { type inet:ip-address; }
            // ...
        }
    }
}
```

## Debugging

### Enable Debug Logging

```bash
# In clixon config, set:
<CLICON_LOG_DESTINATION>syslog</CLICON_LOG_DESTINATION>

# Or run backend in foreground:
clixon_backend -f /etc/clixon/clixon-vpp.xml -F -l o -D 3
```

### VPP Debug

```bash
# Connect to VPP CLI
vppctl

# Show interface state
vppctl show interface

# Show API clients
vppctl show api clients

# Trace API calls
vppctl api trace on
```

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| "Cannot connect to VPP" | VPP not running | Start VPP first |
| "YANG validation error" | Invalid config | Check YANG constraints |
| "Plugin not found" | Wrong path | Check `CLICON_BACKEND_DIR` |
| "Permission denied" | Socket permissions | Check clixon group membership |

## Testing

### Unit Testing

```bash
# Build with debug
make dev

# Run with test config
clixon_backend -f config/clixon-vpp.xml -F -l o -D 3
```

### Integration Testing with VPP

```bash
# 1. Start VPP
sudo systemctl start vpp

# 2. Create test interface
vppctl create loopback interface

# 3. Start backend and test
./scripts/start-services.sh start
clixon_cli -f /etc/clixon/clixon-vpp.xml

# In CLI:
> show interfaces
> configure
# set interfaces interface loop0 enabled true
# commit
```

### RESTCONF Testing

```bash
# Get all interfaces
curl http://localhost:8080/restconf/data/vpp-interfaces:interfaces

# Set interface state
curl -X PATCH \
  http://localhost:8080/restconf/data/vpp-interfaces:interfaces/interface=loop0 \
  -H "Content-Type: application/yang-data+json" \
  -d '{"enabled": true}'
```

## Code Style

- Follow Linux kernel coding style for C code
- Use `clicon_log()` for logging
- Use `clicon_err()` for errors
- Always check return values from VAPI calls
- Free allocated resources on error paths

## Resources

- [Clixon Documentation](https://clixon-docs.readthedocs.io/)
- [Clixon GitHub](https://github.com/clicon/clixon)
- [CLIgen GitHub](https://github.com/clicon/cligen)
- [VPP Documentation](https://fd.io/vppproject/vppapi/)
- [VPP Wiki](https://wiki.fd.io/view/VPP)
- [YANG RFC 7950](https://datatracker.ietf.org/doc/html/rfc7950)
- [NETGATE TNSR YANG Models](https://github.com/Netgate/tnsr-yang-models)
