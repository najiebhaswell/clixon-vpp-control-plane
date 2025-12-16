/*
 * VPP API Integration for Clixon CLI
 * Uses VAPI (VPP API) for direct communication with VPP
 */

#ifndef VPP_API_H
#define VPP_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bond information structure */
typedef struct {
  char name[64];
  uint32_t sw_if_index;
  uint32_t id;
  uint8_t mode;
  uint8_t lb;
  uint32_t active_members;
  uint32_t members;
} vpp_bond_info_t;

/* LCP pair information */
typedef struct {
  char vpp_if[128];
  char host_if[64];
  char netns[64];
  uint32_t phy_sw_if_index;
  uint32_t host_sw_if_index;
} vpp_lcp_info_t;

/* Interface information */
typedef struct {
  char name[128];
  uint32_t sw_if_index;
  bool admin_up;
  bool link_up;
  uint32_t mtu;
  char ipv4_addr[64];
  int ipv4_prefix;
  char ipv6_addr[128];
  int ipv6_prefix;
} vpp_interface_info_t;

/* Initialize VPP API connection */
int vpp_api_connect(const char *client_name);

/* Disconnect from VPP API */
void vpp_api_disconnect(void);

/* Check if connected */
bool vpp_api_is_connected(void);

/* Get all bond interfaces - returns count, fills array */
int vpp_api_get_bonds(vpp_bond_info_t *bonds, int max_bonds);

/* Get all LCP pairs - returns count, fills array */
int vpp_api_get_lcps(vpp_lcp_info_t *lcps, int max_lcps);

/* Get all interfaces - returns count, fills array */
int vpp_api_get_interfaces(vpp_interface_info_t *ifs, int max_ifs);

/* Convert bond mode number to string */
const char *vpp_bond_mode_str(uint8_t mode);

/* Convert load balance number to string */
const char *vpp_lb_mode_str(uint8_t lb);

/* Get interface index by name */
int vpp_api_get_interface_index(const char *ifname);

/* ====== CLI WRAPPER FUNCTIONS ====== */

/* Create bond interface */
int vpp_cli_create_bond(const char *mode, const char *lb, uint32_t id,
                        char *bondname_out, size_t bondname_len);

/* Set interface state */
int vpp_cli_set_interface_state(const char *ifname, int is_up);

/* Set interface MTU */
int vpp_cli_set_interface_mtu(const char *ifname, uint32_t mtu);

/* Add IP address */
int vpp_cli_add_ip_address(const char *ifname, const char *address);

/* Delete IP address */
int vpp_cli_del_ip_address(const char *ifname, const char *address);

/* Create sub-interface (VLAN) */
int vpp_cli_create_subif(const char *parent, uint32_t vlan_id, char *subif_out,
                         size_t subif_len);

/* Delete sub-interface */
int vpp_cli_delete_subif(const char *subif);

/* Create loopback interface */
int vpp_cli_create_loopback(char *loopback_out, size_t loopback_len);

/* Create loopback with MAC */
int vpp_cli_create_loopback_mac(const char *mac_addr, char *loopback_out,
                                size_t loopback_len);

/* Delete loopback interface */
int vpp_cli_delete_loopback(const char *loopback);

/* Add member to bond */
int vpp_cli_bond_add_member(const char *bond, const char *member);

/* Remove member from bond */
int vpp_cli_bond_remove_member(const char *bond, const char *member);

/* Create LCP interface pair */
int vpp_cli_create_lcp(const char *vpp_if, const char *host_if,
                       const char *netns);

/* Delete LCP interface pair */
int vpp_cli_delete_lcp(const char *vpp_if);

#endif /* VPP_API_H */
