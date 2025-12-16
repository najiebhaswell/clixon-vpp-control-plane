/*
 * VPP API Integration for Clixon CLI
 * Stub mode - uses VPP CLI socket for communication
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vpp_api.h"
#include "vpp_connection.h"

/* Stub mode - always use CLI fallback */
static bool api_connected = false;

/* Bond mode strings */
static const char *bond_modes[] = {
    "unknown",       /* 0 */
    "round-robin",   /* 1 */
    "active-backup", /* 2 */
    "xor",           /* 3 */
    "broadcast",     /* 4 */
    "lacp",          /* 5 */
};

/* Load balance strings */
static const char *lb_modes[] = {
    "l2",  /* 0 */
    "l34", /* 1 */
    "l23", /* 2 */
    "rr",  /* 3 */
    "bc",  /* 4 */
    "ab",  /* 5 */
};

const char *vpp_bond_mode_str(uint8_t mode) {
  if (mode < sizeof(bond_modes) / sizeof(bond_modes[0]))
    return bond_modes[mode];
  return "unknown";
}

const char *vpp_lb_mode_str(uint8_t lb) {
  if (lb < sizeof(lb_modes) / sizeof(lb_modes[0]))
    return lb_modes[lb];
  return "l2";
}

/* Stub implementations - use CLI fallback */

int vpp_api_connect(const char *client_name) {
  (void)client_name;
  /* In stub mode, we rely on vppctl socket */
  api_connected = true;
  return 0;
}

void vpp_api_disconnect(void) { api_connected = false; }

bool vpp_api_is_connected(void) { return api_connected; }

int vpp_api_get_bonds(vpp_bond_info_t *bonds, int max_bonds) {
  if (!bonds || max_bonds <= 0)
    return -1;

  /* Use CLI to get bond info */
  char *output = vpp_cli_exec("show bond details");
  if (!output)
    return 0;

  int count = 0;
  char *saveptr = NULL;
  char *line = strtok_r(output, "\n", &saveptr);

  /* Current bond being parsed */
  vpp_bond_info_t *current = NULL;

  while (line && count < max_bonds) {
    /* Check for bond name line (starts with "BondEthernet") */
    if (strncmp(line, "BondEthernet", 12) == 0) {
      current = &bonds[count];
      memset(current, 0, sizeof(vpp_bond_info_t));

      /* Extract name */
      char name[64];
      if (sscanf(line, "%63s", name) == 1) {
        strncpy(current->name, name, sizeof(current->name) - 1);
        current->id = atoi(name + 12);
        current->mode = 5; /* lacp default */
        current->lb = 0;   /* l2 default */
        count++;
      }
    }
    /* Parse mode line */
    else if (current && strstr(line, "mode:")) {
      char mode[32];
      if (sscanf(line, "  mode: %31s", mode) == 1) {
        if (strcmp(mode, "round-robin") == 0)
          current->mode = 1;
        else if (strcmp(mode, "active-backup") == 0)
          current->mode = 2;
        else if (strcmp(mode, "xor") == 0)
          current->mode = 3;
        else if (strcmp(mode, "broadcast") == 0)
          current->mode = 4;
        else if (strcmp(mode, "lacp") == 0)
          current->mode = 5;
      }
    }
    /* Parse load balance line */
    else if (current && strstr(line, "load balance:")) {
      char lb[16];
      if (sscanf(line, "  load balance: %15s", lb) == 1) {
        if (strcmp(lb, "l2") == 0)
          current->lb = 0;
        else if (strcmp(lb, "l34") == 0)
          current->lb = 1;
        else if (strcmp(lb, "l23") == 0)
          current->lb = 2;
        else if (strcmp(lb, "rr") == 0)
          current->lb = 3;
        else if (strcmp(lb, "bc") == 0)
          current->lb = 4;
        else if (strcmp(lb, "ab") == 0)
          current->lb = 5;
      }
    }
    /* Parse number of members */
    else if (current && strstr(line, "number of members:")) {
      sscanf(line, "  number of members: %d", (int *)&current->members);
    }
    /* Parse active members */
    else if (current && strstr(line, "number of active members:")) {
      sscanf(line, "  number of active members: %d",
             (int *)&current->active_members);
    }
    /* Parse sw_if_index */
    else if (current && strstr(line, "sw_if_index:")) {
      sscanf(line, "  sw_if_index: %u", &current->sw_if_index);
    }

    line = strtok_r(NULL, "\n", &saveptr);
  }

  free(output);
  return count;
}

int vpp_api_get_lcps(vpp_lcp_info_t *lcps, int max_lcps) {
  if (!lcps || max_lcps <= 0)
    return -1;

  char *output = vpp_cli_exec("show lcp");
  if (!output)
    return 0;

  int count = 0;
  char *saveptr = NULL;
  char *line = strtok_r(output, "\n", &saveptr);

  while (line && count < max_lcps) {
    /* Format: itf-pair: [N] vpp_if tap_if host_if idx type tap netns name */
    if (strncmp(line, "itf-pair:", 9) == 0) {
      int idx, host_sw;
      char vpp_if[128], tap_if[64], host_if[64], type[16], netns[64];

      if (sscanf(line, "itf-pair: [%d] %127s %63s %63s %d type %15s netns %63s",
                 &idx, vpp_if, tap_if, host_if, &host_sw, type, netns) >= 6) {
        memset(&lcps[count], 0, sizeof(vpp_lcp_info_t));
        strncpy(lcps[count].vpp_if, vpp_if, sizeof(lcps[count].vpp_if) - 1);
        strncpy(lcps[count].host_if, host_if, sizeof(lcps[count].host_if) - 1);
        strncpy(lcps[count].netns, netns, sizeof(lcps[count].netns) - 1);
        lcps[count].phy_sw_if_index = idx;
        lcps[count].host_sw_if_index = host_sw;
        count++;
      }
    }
    line = strtok_r(NULL, "\n", &saveptr);
  }

  free(output);
  return count;
}

int vpp_api_get_interfaces(vpp_interface_info_t *ifs, int max_ifs) {
  if (!ifs || max_ifs <= 0)
    return -1;

  char *output = vpp_cli_exec("show interface");
  if (!output)
    return 0;

  int count = 0;
  char *line = strtok(output, "\n");

  /* Skip header */
  if (line)
    line = strtok(NULL, "\n");

  while (line && count < max_ifs) {
    char name[64], state[16];
    int idx;

    if (sscanf(line, "%63s %d %15s", name, &idx, state) >= 2) {
      if (name[0] >= 'A' && name[0] <= 'z' && strcmp(name, "Name") != 0) {
        strncpy(ifs[count].name, name, sizeof(ifs[count].name) - 1);
        ifs[count].sw_if_index = idx;
        ifs[count].admin_up = (strcmp(state, "up") == 0);
        ifs[count].link_up = (strcmp(state, "up") == 0);
        ifs[count].mtu = 1500;
        count++;
      }
    }
    line = strtok(NULL, "\n");
  }

  free(output);
  return count;
}

/* CLI helper functions - these use vpp_cli_exec from vpp_connection.h */

int vpp_cli_create_bond(const char *mode, const char *lb, uint32_t id,
                        char *bondname_out, size_t bondname_len) {
  if (!bondname_out || bondname_len == 0)
    return -1;

  char cmd[256];
  if (lb && strlen(lb) > 0 && strcasecmp(lb, "l2") != 0) {
    snprintf(cmd, sizeof(cmd), "create bond mode %s id %u load-balance %s",
             mode, id, lb);
  } else {
    snprintf(cmd, sizeof(cmd), "create bond mode %s id %u", mode, id);
  }

  char *result = vpp_cli_exec(cmd);
  if (!result)
    return -1;

  char *newline = strchr(result, '\n');
  if (newline)
    *newline = '\0';

  if (strlen(result) > 0) {
    strncpy(bondname_out, result, bondname_len - 1);
    bondname_out[bondname_len - 1] = '\0';
    free(result);
    return 0;
  }

  snprintf(bondname_out, bondname_len, "BondEthernet%u", id);
  free(result);
  return 0;
}

int vpp_cli_set_interface_state(const char *ifname, int is_up) {
  if (!ifname)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "set interface state %s %s", ifname,
           is_up ? "up" : "down");
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_set_interface_mtu(const char *ifname, uint32_t mtu) {
  if (!ifname || mtu < 64 || mtu > 65535)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "set interface mtu %u %s", mtu, ifname);
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_add_ip_address(const char *ifname, const char *address) {
  if (!ifname || !address)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "set interface ip address %s %s", ifname, address);
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_del_ip_address(const char *ifname, const char *address) {
  if (!ifname || !address)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "set interface ip address %s %s del", ifname,
           address);
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_create_subif(const char *parent, uint32_t vlan_id, char *subif_out,
                         size_t subif_len) {
  if (!parent || !subif_out || subif_len == 0 || vlan_id < 1 || vlan_id > 4094)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "create sub-interfaces %s %u dot1q %u exact-match",
           parent, vlan_id, vlan_id);

  char *result = vpp_cli_exec(cmd);
  if (!result)
    return -1;

  snprintf(subif_out, subif_len, "%s.%u", parent, vlan_id);
  free(result);
  return 0;
}

int vpp_cli_delete_subif(const char *subif) {
  if (!subif)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "delete sub-interfaces %s", subif);
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_create_loopback(char *loopback_out, size_t loopback_len) {
  if (!loopback_out || loopback_len == 0)
    return -1;

  char *result = vpp_cli_exec("create loopback interface");
  if (!result)
    return -1;

  char *newline = strchr(result, '\n');
  if (newline)
    *newline = '\0';

  if (strlen(result) > 0) {
    strncpy(loopback_out, result, loopback_len - 1);
    loopback_out[loopback_len - 1] = '\0';
    free(result);
    return 0;
  }

  free(result);
  return -1;
}

int vpp_cli_create_loopback_mac(const char *mac_addr, char *loopback_out,
                                size_t loopback_len) {
  if (!loopback_out || loopback_len == 0)
    return -1;

  char cmd[256];
  if (mac_addr && strlen(mac_addr) > 0) {
    snprintf(cmd, sizeof(cmd), "create loopback interface mac %s", mac_addr);
  } else {
    snprintf(cmd, sizeof(cmd), "create loopback interface");
  }

  char *result = vpp_cli_exec(cmd);
  if (!result)
    return -1;

  char *newline = strchr(result, '\n');
  if (newline)
    *newline = '\0';

  if (strlen(result) > 0) {
    strncpy(loopback_out, result, loopback_len - 1);
    loopback_out[loopback_len - 1] = '\0';
    free(result);
    return 0;
  }

  free(result);
  return -1;
}

int vpp_cli_delete_loopback(const char *loopback) {
  if (!loopback)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "delete loopback interface intfc %s", loopback);
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_bond_add_member(const char *bond, const char *member) {
  if (!bond || !member)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "bond add %s %s", bond, member);
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_bond_remove_member(const char *bond, const char *member) {
  if (!bond || !member)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "bond del %s %s", bond, member);
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_create_lcp(const char *vpp_if, const char *host_if,
                       const char *netns) {
  if (!vpp_if || !host_if)
    return -1;

  char cmd[512];
  if (netns && strlen(netns) > 0) {
    snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s netns %s", vpp_if,
             host_if, netns);
  } else {
    snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s", vpp_if, host_if);
  }
  return vpp_cli_exec_check(cmd);
}

int vpp_cli_delete_lcp(const char *vpp_if) {
  if (!vpp_if)
    return -1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "lcp delete %s", vpp_if);
  return vpp_cli_exec_check(cmd);
}

/* Get interface index by name */
int vpp_api_get_interface_index(const char *ifname) {
  if (!ifname)
    return -1;

  vpp_interface_info_t ifaces[256];
  int count = vpp_api_get_interfaces(ifaces, 256);

  for (int i = 0; i < count; i++) {
    if (strcmp(ifaces[i].name, ifname) == 0) {
      return ifaces[i].sw_if_index;
    }
  }

  return -1;
}
