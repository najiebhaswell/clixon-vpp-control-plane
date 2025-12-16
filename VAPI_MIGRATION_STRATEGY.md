# VAPI Migration Strategy for Configure Mode

## Objective
Migrate configure mode from CLI-based wrappers (`vpp_cli_*`) to pure VPP API (VAPI) for consistency and performance.

## Architecture Current State

### Current Flow (CLI Wrapper Based)
```
Configure Handler → vpp_cli_* function → vpp_cli_exec() → vppctl → VPP
```

### Target Flow (Pure VAPI)
```
Configure Handler → vpp_api_* function → VAPI → VPP
```

## Migration Phases

### Phase 1: Simple Stateless Operations (VAPI Direct)
These operations are simple and return quickly:
- ✅ Set interface state (up/down) - `vpp_api_set_interface_state()`
- ✅ Set interface MTU - `vpp_api_set_interface_mtu()`
- 🔄 TBD: Add IP address - needs ip.api.vapi.h
- 🔄 TBD: Create VLAN - needs interface_utils functions

### Phase 2: Complex Operations (Hybrid Approach)
These require response parsing and complex logic:
- Keep using CLI wrappers for now:
  - `vpp_cli_create_bond()` - complex bond creation with mode/lb parameters
  - `vpp_cli_bond_add_member()` - bond membership operations
  - `vpp_cli_create_lcp()` - LCP pair creation (v2 API is complex)

### Phase 3: Full Migration (Future)
- Implement VAPI callbacks for all operations
- Use VAPI context patterns for synchronous responses
- Remove all CLI wrapper functions

## Current Implementation

### VAPI Functions Available
1. **Interface Configuration**
   - `sw_interface_set_flags` - set admin up/down
   - `hw_interface_set_mtu` - set MTU

2. **Bond Operations**
   - `bond_create` / `bond_create2` - create bonds
   - `bond_add_member` - add bond members
   - `bond_del_member` - remove bond members

3. **LCP Operations**
   - `lcp_itf_pair_add_del` - create/delete LCP pairs

4. **IP Operations**
   - `ip_address_dump` - query IP addresses
   - Need to find add/del functions

### New Wrapper Functions Created
```c
int vpp_api_set_interface_state(uint32_t sw_if_index, int is_up);
int vpp_api_set_interface_mtu(uint32_t sw_if_index, uint16_t mtu);
int vpp_api_get_interface_index(const char *ifname);
```

## Implementation Plan

### Step 1: Create VAPI Helper for IP Operations
- Check if `ip_interface_address_add_del` exists
- Implement `vpp_api_add_ip_address(sw_if_index, address)`
- Implement `vpp_api_del_ip_address(sw_if_index, address)`

### Step 2: Create VAPI Helper for VLAN (sub-interfaces)
- Check if `create_subif` VAPI exists
- Implement `vpp_api_create_subif(sw_if_index, vlan_id)`

### Step 3: Create VAPI Helper for Bond Operations
- Implement `vpp_api_create_bond(mode, lb, id)`
- Implement `vpp_api_bond_add_member(bond_index, member_index)`
- Implement `vpp_api_bond_del_member(bond_index, member_index)`

### Step 4: Update Configure Handlers
Replace:
```c
vpp_cli_set_interface_mtu(ifname, mtu)
```
With:
```c
int sw_if_index = vpp_api_get_interface_index(ifname);
if (sw_if_index >= 0) {
  vpp_api_set_interface_mtu(sw_if_index, mtu);
}
```

### Step 5: Create VAPI Helper for LCP Operations
- Check `lcp_itf_pair_add_del` VAPI structure
- Implement `vpp_api_create_lcp(vpp_if_index, linux_if_name)`
- Implement `vpp_api_delete_lcp(vpp_if_index)`

## Challenges & Solutions

### Challenge 1: VAPI requires sw_if_index, not interface names
**Solution:** Use `vpp_api_get_interface_index(ifname)` helper

### Challenge 2: VAPI is asynchronous/callback-based
**Solution:** Start with synchronous operations, add callback pattern as needed

### Challenge 3: IP address operations in VAPI
**Solution:** Need to investigate if ip.api.vapi.h has add/del functions

### Challenge 4: LCP API is complex (v2 schema)
**Solution:** Keep CLI wrapper for now, migrate in phase 3

## Handlers to Update (Phase 1)

1. `cli_if_mtu()` - already uses abstraction, update to VAPI
2. `cli_if_no_shutdown()` - already uses abstraction, update to VAPI
3. `cli_if_shutdown()` - already uses abstraction, update to VAPI
4. `cli_vlan_create()` - needs VAPI subif creation

## Testing Strategy

1. Build with new VAPI functions
2. Test interface state changes (up/down)
3. Test MTU changes
4. Test IP address operations
5. Test bond operations (if migrated)
6. Verify all operations work end-to-end

## Timeline

- **Phase 1 (Now):** Simple interface operations (state, MTU)
- **Phase 2 (Later):** IP address and VLAN operations
- **Phase 3 (Future):** Complex operations (bonds, LCP)

## Benefits

✅ **Performance:** Direct API calls vs CLI parsing (vppctl)
✅ **Reliability:** Type-safe VAPI interfaces vs string commands
✅ **Consistency:** Same API layer for queries (get_*) and changes (api_*)
✅ **Maintainability:** Less code, fewer edge cases
