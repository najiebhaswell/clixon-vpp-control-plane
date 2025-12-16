/*
 * vpp_interface.c - VPP interface operations via CLI socket
 *
 * Uses VPP CLI commands to manage interfaces:
 * - show interface
 * - set interface state
 * - set interface mtu
 * - set interface ip address
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vpp_connection.h"
#include "vpp_interface.h"

/*
 * Parse "show interface" output to extract interface information
 *
 * Example output (fixed-width columns):
 *               Name               Idx    State  MTU (L3/IP4/IP6/MPLS) Counter
 * Count HundredGigabitEthernet8a/0/0      1     down         9000/0/0/0 local0
 * 0     down          0/0/0/0
 */
static int parse_show_interface(const char *output,
                                vpp_interface_info_t **interfaces) {
  vpp_interface_info_t *head = NULL;
  vpp_interface_info_t *tail = NULL;
  const char *line;
  const char *next;
  char linebuf[512];
  int line_num = 0;

  if (!output || !interfaces) {
    return -1;
  }
  *interfaces = NULL;

  line = output;
  while (line && *line) {
    /* Find end of line */
    next = strchr(line, '\n');
    size_t len = next ? (size_t)(next - line) : strlen(line);

    if (len >= sizeof(linebuf)) {
      len = sizeof(linebuf) - 1;
    }
    memcpy(linebuf, line, len);
    linebuf[len] = '\0';

    line_num++;

    /* Skip header line (contains "Name" and "Idx") */
    if (strstr(linebuf, "Name") && strstr(linebuf, "Idx")) {
      line = next ? next + 1 : NULL;
      continue;
    }

    /* Skip empty or short lines */
    if (len < 20) {
      line = next ? next + 1 : NULL;
      continue;
    }

    /* Parse fixed-width format:
     * Columns: Name (30 chars), Idx, State, MTU/MTU/MTU/MTU, Counter, Count
     * Name starts at column 0 and goes until we find multiple spaces before a
     * digit
     */
    char name[64] = {0};
    int idx = -1;
    char state[16] = {0};
    int mtu1 = 0;

    /* The format seems to be:
     * Name is left-aligned, followed by spaces, then index right-aligned around
     * column 30
     */

    /* Find where name ends - look for pattern of spaces followed by digit
     * (index) */
    int name_end = 0;
    int i;
    for (i = 0; i < (int)len - 2; i++) {
      if (!isspace(linebuf[i])) {
        name_end = i + 1;
      }
      /* Look for transition from spaces to digit */
      if (isspace(linebuf[i]) && isspace(linebuf[i + 1]) &&
          isdigit(linebuf[i + 2])) {
        break;
      }
    }

    if (name_end > 0 && name_end < 64) {
      memcpy(name, linebuf, name_end);
      name[name_end] = '\0';
    }

    /* Parse rest: idx state mtu/mtu/mtu/mtu */
    if (name[0] && i < (int)len) {
      int mtu2, mtu3, mtu4;
      char *rest = linebuf + i;

      /* Skip leading spaces and parse */
      while (*rest && isspace(*rest))
        rest++;

      if (sscanf(rest, "%d %15s %d/%d/%d/%d", &idx, state, &mtu1, &mtu2, &mtu3,
                 &mtu4) >= 2) {

        /* Create interface info */
        vpp_interface_info_t *info = calloc(1, sizeof(vpp_interface_info_t));
        if (info) {
          info->sw_if_index = (uint32_t)idx;
          strncpy(info->name, name, sizeof(info->name) - 1);
          info->mtu = (uint32_t)mtu1;
          info->admin_up = (strcmp(state, "up") == 0);
          info->link_up = info->admin_up;

          /* Determine type from name */
          if (strncmp(name, "local", 5) == 0) {
            strcpy(info->type, "local");
          } else if (strncmp(name, "loop", 4) == 0) {
            strcpy(info->type, "loopback");
          } else if (strncmp(name, "tap", 3) == 0) {
            strcpy(info->type, "tap");
          } else if (strncmp(name, "vxlan", 5) == 0) {
            strcpy(info->type, "vxlan");
          } else if (strncmp(name, "memif", 5) == 0) {
            strcpy(info->type, "memif");
          } else if (strncmp(name, "host-", 5) == 0) {
            strcpy(info->type, "af-packet");
          } else if (strncmp(name, "BondEthernet", 12) == 0) {
            strcpy(info->type, "bond");
          } else if (strchr(name, '.') != NULL) {
            strcpy(info->type, "sub-interface");
          } else {
            strcpy(info->type, "ethernet");
          }

          /* Add to list */
          info->next = NULL;
          if (tail == NULL) {
            head = tail = info;
          } else {
            tail->next = info;
            tail = info;
          }
        }
      }
    }

    line = next ? next + 1 : NULL;
  }

  *interfaces = head;
  return 0;
}

/*
 * Parse MAC address from "show hardware-interfaces" output
 */
static int get_interface_mac(const char *ifname, uint8_t *mac) {
  char cmd[128];
  char *response;

  snprintf(cmd, sizeof(cmd), "show hardware-interfaces %s", ifname);
  response = vpp_cli_exec(cmd);

  if (response) {
    /* Look for Ethernet address: xx:xx:xx:xx:xx:xx */
    char *p = strstr(response, "Ethernet address ");
    if (p) {
      p += strlen("Ethernet address ");
      if (sscanf(p, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &mac[0],
                 &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
        free(response);
        return 0;
      }
    }
    free(response);
  }

  memset(mac, 0, 6);
  return -1;
}

int vpp_interface_dump(vpp_interface_info_t **interfaces) {
  char *response;
  int ret;

  if (!interfaces) {
    return -1;
  }
  *interfaces = NULL;

  /* Execute "show interface" command */
  response = vpp_cli_exec("show interface");
  if (!response) {
    fprintf(stderr, "[vpp] Failed to execute 'show interface'\n");
    return -1;
  }

  /* Parse response */
  ret = parse_show_interface(response, interfaces);
  free(response);

  if (ret != 0) {
    return ret;
  }

  /* Optionally get MAC addresses for each interface */
  vpp_interface_info_t *iface;
  for (iface = *interfaces; iface; iface = iface->next) {
    if (strcmp(iface->type, "local") != 0) {
      get_interface_mac(iface->name, iface->mac);
    }
  }

  return 0;
}

void vpp_interface_list_free(vpp_interface_info_t *list) {
  vpp_interface_info_t *curr, *next;
  for (curr = list; curr; curr = next) {
    next = curr->next;
    free(curr);
  }
}

uint32_t vpp_interface_name_to_index(const char *name) {
  vpp_interface_info_t *list, *curr;
  uint32_t idx = (uint32_t)-1;

  if (vpp_interface_dump(&list) == 0) {
    for (curr = list; curr; curr = curr->next) {
      if (strcmp(curr->name, name) == 0) {
        idx = curr->sw_if_index;
        break;
      }
    }
    vpp_interface_list_free(list);
  }
  return idx;
}

int vpp_interface_set_flags(uint32_t sw_if_index, bool admin_up) {
  char cmd[256];
  vpp_interface_info_t *list, *curr;
  const char *ifname = NULL;

  /* Find interface name by index */
  if (vpp_interface_dump(&list) == 0) {
    for (curr = list; curr; curr = curr->next) {
      if (curr->sw_if_index == sw_if_index) {
        ifname = curr->name;
        break;
      }
    }
  }

  if (!ifname) {
    fprintf(stderr, "[vpp] Interface with index %u not found\n", sw_if_index);
    vpp_interface_list_free(list);
    return -1;
  }

  /* Build command: set interface state <name> up|down */
  snprintf(cmd, sizeof(cmd), "set interface state %s %s", ifname,
           admin_up ? "up" : "down");

  vpp_interface_list_free(list);

  return vpp_cli_exec_check(cmd);
}

int vpp_interface_set_mtu(uint32_t sw_if_index, uint16_t mtu) {
  char cmd[256];
  vpp_interface_info_t *list, *curr;
  const char *ifname = NULL;

  /* Find interface name by index */
  if (vpp_interface_dump(&list) == 0) {
    for (curr = list; curr; curr = curr->next) {
      if (curr->sw_if_index == sw_if_index) {
        ifname = curr->name;
        break;
      }
    }
  }

  if (!ifname) {
    fprintf(stderr, "[vpp] Interface with index %u not found\n", sw_if_index);
    vpp_interface_list_free(list);
    return -1;
  }

  /* Build command: set interface mtu <mtu> <name> */
  snprintf(cmd, sizeof(cmd), "set interface mtu %u %s", mtu, ifname);

  vpp_interface_list_free(list);

  return vpp_cli_exec_check(cmd);
}

int vpp_interface_add_ip4_address(uint32_t sw_if_index, uint32_t address,
                                  uint8_t prefix_len) {
  char cmd[256];
  char ip_str[INET_ADDRSTRLEN];
  vpp_interface_info_t *list, *curr;
  const char *ifname = NULL;

  /* Find interface name by index */
  if (vpp_interface_dump(&list) == 0) {
    for (curr = list; curr; curr = curr->next) {
      if (curr->sw_if_index == sw_if_index) {
        ifname = curr->name;
        break;
      }
    }
  }

  if (!ifname) {
    fprintf(stderr, "[vpp] Interface with index %u not found\n", sw_if_index);
    vpp_interface_list_free(list);
    return -1;
  }

  /* Convert address to string */
  inet_ntop(AF_INET, &address, ip_str, sizeof(ip_str));

  /* Build command: set interface ip address <name> <ip>/<prefix> */
  snprintf(cmd, sizeof(cmd), "set interface ip address %s %s/%u", ifname,
           ip_str, prefix_len);

  vpp_interface_list_free(list);

  return vpp_cli_exec_check(cmd);
}

int vpp_mac_string_to_bytes(const char *mac_str, uint8_t *mac) {
  unsigned int b[6];
  if (sscanf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x", &b[0], &b[1], &b[2],
             &b[3], &b[4], &b[5]) != 6) {
    return -1;
  }
  for (int i = 0; i < 6; i++) {
    mac[i] = (uint8_t)b[i];
  }
  return 0;
}

void vpp_mac_bytes_to_string(const uint8_t *mac, char *str, size_t len) {
  if (len >= 18) {
    snprintf(str, len, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
  }
}

/*
 * Create a loopback interface
 * Returns sw_if_index on success, -1 on failure
 */
int vpp_interface_create_loopback(uint32_t *sw_if_index) {
  char *response;

  response = vpp_cli_exec("create loopback interface");
  if (!response) {
    return -1;
  }

  /* Parse response: "loop0" or similar */
  if (sw_if_index) {
    /* Get the interface index */
    vpp_interface_info_t *list, *curr;
    if (vpp_interface_dump(&list) == 0) {
      /* Find the newest loop interface */
      for (curr = list; curr; curr = curr->next) {
        if (strncmp(curr->name, "loop", 4) == 0) {
          *sw_if_index = curr->sw_if_index;
        }
      }
      vpp_interface_list_free(list);
    }
  }

  free(response);
  return 0;
}

/*
 * Delete a loopback interface
 */
int vpp_interface_delete_loopback(const char *ifname) {
  char cmd[256];

  snprintf(cmd, sizeof(cmd), "delete loopback interface intfc %s", ifname);
  return vpp_cli_exec_check(cmd);
}

/*
 * Add an IP address to an interface (works for both IPv4 and IPv6)
 * address_str should be in format "x.x.x.x/prefix" or "xxxx::xxxx/prefix"
 */
int vpp_interface_add_ip_address(const char *ifname, const char *address_str) {
  char cmd[256];

  if (!ifname || !address_str) {
    return -1;
  }

  snprintf(cmd, sizeof(cmd), "set interface ip address %s %s", ifname,
           address_str);
  fprintf(stderr, "[vpp] Adding IP: %s\n", cmd);
  return vpp_cli_exec_check(cmd);
}

/*
 * Delete an IP address from an interface
 */
int vpp_interface_del_ip_address(const char *ifname, const char *address_str) {
  char cmd[256];

  if (!ifname || !address_str) {
    return -1;
  }

  snprintf(cmd, sizeof(cmd), "set interface ip address del %s %s", ifname,
           address_str);
  fprintf(stderr, "[vpp] Deleting IP: %s\n", cmd);
  return vpp_cli_exec_check(cmd);
}

/*
 * Create loopback with optional MAC address
 */
int vpp_interface_create_loopback_mac(const char *mac_str, char *ifname_out,
                                      size_t ifname_len) {
  char cmd[256];
  char *response;

  if (mac_str && *mac_str) {
    snprintf(cmd, sizeof(cmd), "create loopback interface mac %s", mac_str);
  } else {
    snprintf(cmd, sizeof(cmd), "create loopback interface");
  }

  response = vpp_cli_exec(cmd);
  if (!response) {
    return -1;
  }

  /* Parse response - VPP returns the interface name like "loop0" */
  if (ifname_out && ifname_len > 0) {
    /* Response is like "loop0\n" */
    char *newline = strchr(response, '\n');
    if (newline)
      *newline = '\0';
    strncpy(ifname_out, response, ifname_len - 1);
    ifname_out[ifname_len - 1] = '\0';
  }

  free(response);
  return 0;
}

/*
 * Create a VLAN sub-interface (dot1q)
 * parent_ifname: parent interface name (e.g., "HundredGigabitEthernet8a/0/0")
 * vlan_id: VLAN ID (1-4094)
 * sub_id: sub-interface ID (usually same as vlan_id)
 * ifname_out: buffer to store created interface name
 */
int vpp_interface_create_subif(const char *parent_ifname, uint16_t vlan_id,
                               uint32_t sub_id, char *ifname_out,
                               size_t ifname_len) {
  char cmd[256];
  char *response;

  if (!parent_ifname || vlan_id == 0 || vlan_id > 4094) {
    return -1;
  }

  /* If sub_id is 0, use vlan_id as sub_id */
  if (sub_id == 0) {
    sub_id = vlan_id;
  }

  /* Command: create sub-interfaces <interface> <subId> dot1q <vlanId>
   * exact-match */
  snprintf(cmd, sizeof(cmd), "create sub-interfaces %s %u dot1q %u exact-match",
           parent_ifname, sub_id, vlan_id);

  fprintf(stderr, "[vpp] Creating sub-interface: %s\n", cmd);

  response = vpp_cli_exec(cmd);
  if (!response) {
    return -1;
  }

  /* Check for error in response */
  if (strstr(response, "error") || strstr(response, "unknown")) {
    fprintf(stderr, "[vpp] Sub-interface creation failed: %s\n", response);
    free(response);
    return -1;
  }

  /* Build the sub-interface name: parent.subId */
  if (ifname_out && ifname_len > 0) {
    snprintf(ifname_out, ifname_len, "%s.%u", parent_ifname, sub_id);
  }

  free(response);
  return 0;
}

/*
 * Delete a sub-interface
 * ifname: sub-interface name (e.g., "HundredGigabitEthernet8a/0/0.100")
 */
int vpp_interface_delete_subif(const char *ifname) {
  char cmd[256];

  if (!ifname) {
    return -1;
  }

  snprintf(cmd, sizeof(cmd), "delete sub-interface %s", ifname);
  fprintf(stderr, "[vpp] Deleting sub-interface: %s\n", cmd);

  return vpp_cli_exec_check(cmd);
}

/*
 * Create a QinQ (dot1ad) sub-interface
 * parent_ifname: parent interface name
 * outer_vlan: outer VLAN ID (S-TAG)
 * inner_vlan: inner VLAN ID (C-TAG)
 * sub_id: sub-interface ID
 */
int vpp_interface_create_qinq_subif(const char *parent_ifname,
                                    uint16_t outer_vlan, uint16_t inner_vlan,
                                    uint32_t sub_id, char *ifname_out,
                                    size_t ifname_len) {
  char cmd[256];
  char *response;

  if (!parent_ifname || outer_vlan == 0 || outer_vlan > 4094 ||
      inner_vlan == 0 || inner_vlan > 4094) {
    return -1;
  }

  if (sub_id == 0) {
    sub_id = outer_vlan;
  }

  /* Command: create sub-interfaces <interface> <subId> dot1ad <outer>
   * inner-dot1q <inner> */
  snprintf(cmd, sizeof(cmd),
           "create sub-interfaces %s %u dot1ad %u inner-dot1q %u exact-match",
           parent_ifname, sub_id, outer_vlan, inner_vlan);

  fprintf(stderr, "[vpp] Creating QinQ sub-interface: %s\n", cmd);

  response = vpp_cli_exec(cmd);
  if (!response) {
    return -1;
  }

  if (strstr(response, "error") || strstr(response, "unknown")) {
    fprintf(stderr, "[vpp] QinQ sub-interface creation failed: %s\n", response);
    free(response);
    return -1;
  }

  if (ifname_out && ifname_len > 0) {
    snprintf(ifname_out, ifname_len, "%s.%u", parent_ifname, sub_id);
  }

  free(response);
  return 0;
}

/*
 * Create a bonding interface
 * mode: "round-robin", "active-backup", "broadcast", "lacp", "xor"
 * lb: load-balance for lacp/xor: "l2", "l23", "l34" (can be NULL)
 * mac_str: optional MAC address (can be NULL)
 * bond_id: optional bond ID (0 = auto)
 * ifname_out: buffer to store created interface name
 */
int vpp_interface_create_bond(const char *mode, const char *lb,
                              const char *mac_str, uint32_t bond_id,
                              char *ifname_out, size_t ifname_len) {
  char cmd[256];
  char *response;

  if (!mode) {
    return -1;
  }

  /* Build command based on mode */
  int pos = snprintf(cmd, sizeof(cmd), "create bond mode %s", mode);

  /* Add load-balance for lacp/xor */
  if (lb && (strcmp(mode, "lacp") == 0 || strcmp(mode, "xor") == 0)) {
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " load-balance %s", lb);
  }

  /* Add optional MAC */
  if (mac_str && *mac_str) {
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " hw-addr %s", mac_str);
  }

  /* Add optional ID */
  if (bond_id > 0) {
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " id %u", bond_id);
  }

  fprintf(stderr, "[vpp] Creating bond: %s\n", cmd);

  response = vpp_cli_exec(cmd);
  if (!response) {
    return -1;
  }

  /* Parse response to get interface name - VPP returns something like
   * "BondEthernet0" */
  if (strstr(response, "error") || strstr(response, "unknown")) {
    fprintf(stderr, "[vpp] Bond creation failed: %s\n", response);
    free(response);
    return -1;
  }

  /* Extract interface name from response */
  if (ifname_out && ifname_len > 0) {
    /* Response is like "BondEthernet0\n" */
    char *newline = strchr(response, '\n');
    if (newline)
      *newline = '\0';

    /* Skip any leading whitespace */
    char *name = response;
    while (*name && (*name == ' ' || *name == '\t'))
      name++;

    strncpy(ifname_out, name, ifname_len - 1);
    ifname_out[ifname_len - 1] = '\0';
  }

  free(response);
  return 0;
}

/*
 * Delete a bonding interface
 */
int vpp_interface_delete_bond(const char *ifname) {
  char cmd[256];

  if (!ifname) {
    return -1;
  }

  snprintf(cmd, sizeof(cmd), "delete bond %s", ifname);
  fprintf(stderr, "[vpp] Deleting bond: %s\n", cmd);

  return vpp_cli_exec_check(cmd);
}

/*
 * Add a member interface to a bond
 */
int vpp_interface_bond_add_member(const char *bond_ifname,
                                  const char *member_ifname) {
  char cmd[256];

  if (!bond_ifname || !member_ifname) {
    return -1;
  }

  snprintf(cmd, sizeof(cmd), "bond add %s %s", bond_ifname, member_ifname);
  fprintf(stderr, "[vpp] Adding bond member: %s\n", cmd);

  return vpp_cli_exec_check(cmd);
}

/*
 * Remove a member interface from a bond
 */
int vpp_interface_bond_del_member(const char *member_ifname) {
  char cmd[256];

  if (!member_ifname) {
    return -1;
  }

  snprintf(cmd, sizeof(cmd), "bond del %s", member_ifname);
  fprintf(stderr, "[vpp] Removing bond member: %s\n", cmd);

  return vpp_cli_exec_check(cmd);
}

/*
 * Show bond details
 */
char *vpp_interface_show_bond(const char *bond_ifname) {
  char cmd[256];

  if (bond_ifname) {
    snprintf(cmd, sizeof(cmd), "show bond details %s", bond_ifname);
  } else {
    snprintf(cmd, sizeof(cmd), "show bond");
  }

  return vpp_cli_exec(cmd);
}

/*
 * Create an LCP pair (Linux Control Plane)
 * Creates a Linux interface mirrored to a VPP interface
 * ifname: VPP interface name
 * host_ifname: Linux interface name to create
 * netns: Optional network namespace (NULL for default)
 * is_tun: If true, create a TUN device instead of TAP
 */
int vpp_lcp_create(const char *ifname, const char *host_ifname,
                   const char *netns, bool is_tun) {
  char cmd[256];
  int pos;

  if (!ifname || !host_ifname) {
    return -1;
  }

  pos = snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s", ifname,
                 host_ifname);

  if (netns && *netns) {
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " netns %s", netns);
  }

  if (is_tun) {
    snprintf(cmd + pos, sizeof(cmd) - pos, " tun");
  }

  fprintf(stderr, "[vpp] Creating LCP pair: %s\n", cmd);

  return vpp_cli_exec_check(cmd);
}

/*
 * Delete an LCP pair
 */
int vpp_lcp_delete(const char *ifname) {
  char cmd[256];

  if (!ifname) {
    return -1;
  }

  snprintf(cmd, sizeof(cmd), "lcp delete %s", ifname);
  fprintf(stderr, "[vpp] Deleting LCP pair: %s\n", cmd);

  return vpp_cli_exec_check(cmd);
}

/*
 * Set default LCP network namespace
 */
int vpp_lcp_set_default_netns(const char *netns) {
  char cmd[256];

  if (netns && *netns) {
    snprintf(cmd, sizeof(cmd), "lcp default netns %s", netns);
  } else {
    snprintf(cmd, sizeof(cmd), "lcp default netns");
  }

  fprintf(stderr, "[vpp] Setting LCP default netns: %s\n", cmd);

  return vpp_cli_exec_check(cmd);
}

/*
 * Enable/disable LCP sync (sync Linux state changes to VPP)
 */
int vpp_lcp_set_sync(bool enable) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "lcp lcp-sync %s", enable ? "on" : "off");
  return vpp_cli_exec_check(cmd);
}

/*
 * Enable/disable LCP auto sub-interface creation
 */
int vpp_lcp_set_auto_subint(bool enable) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "lcp lcp-auto-subint %s", enable ? "on" : "off");
  return vpp_cli_exec_check(cmd);
}

/*
 * Get LCP status/info as string
 */
char *vpp_lcp_show(void) { return vpp_cli_exec("show lcp"); }
