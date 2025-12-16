/*
 * vpp_interface.h - VPP interface operations
 */

#ifndef _VPP_INTERFACE_H_
#define _VPP_INTERFACE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Interface information structure */
typedef struct vpp_interface_info {
  uint32_t sw_if_index;
  uint32_t sup_sw_if_index;
  char name[64];
  char type[32];
  uint8_t mac[6];
  uint32_t mtu;
  uint32_t link_speed;
  bool admin_up;
  bool link_up;
  struct vpp_interface_info *next;
} vpp_interface_info_t;

/* Interface operations */
int vpp_interface_dump(vpp_interface_info_t **interfaces);
void vpp_interface_list_free(vpp_interface_info_t *list);
uint32_t vpp_interface_name_to_index(const char *name);

int vpp_interface_set_flags(uint32_t sw_if_index, bool admin_up);
int vpp_interface_set_mtu(uint32_t sw_if_index, uint16_t mtu);
int vpp_interface_add_ip4_address(uint32_t sw_if_index, uint32_t address,
                                  uint8_t prefix_len);

/* MAC address utilities */
int vpp_mac_string_to_bytes(const char *mac_str, uint8_t *mac);
void vpp_mac_bytes_to_string(const uint8_t *mac, char *str, size_t len);

/* Loopback management */
int vpp_interface_create_loopback(uint32_t *sw_if_index);
int vpp_interface_delete_loopback(const char *ifname);
int vpp_interface_create_loopback_mac(const char *mac_str, char *ifname_out,
                                      size_t ifname_len);

/* IP address management (by interface name) */
int vpp_interface_add_ip_address(const char *ifname, const char *address_str);
int vpp_interface_del_ip_address(const char *ifname, const char *address_str);

/* Sub-interface management */
int vpp_interface_create_subif(const char *parent_ifname, uint16_t vlan_id,
                               uint32_t sub_id, char *ifname_out,
                               size_t ifname_len);
int vpp_interface_delete_subif(const char *ifname);
int vpp_interface_create_qinq_subif(const char *parent_ifname,
                                    uint16_t outer_vlan, uint16_t inner_vlan,
                                    uint32_t sub_id, char *ifname_out,
                                    size_t ifname_len);

/* Bonding interface management */
int vpp_interface_create_bond(const char *mode, const char *lb,
                              const char *mac_str, uint32_t bond_id,
                              char *ifname_out, size_t ifname_len);
int vpp_interface_delete_bond(const char *ifname);
int vpp_interface_bond_add_member(const char *bond_ifname,
                                  const char *member_ifname);
int vpp_interface_bond_del_member(const char *member_ifname);
char *vpp_interface_show_bond(const char *bond_ifname);

/* LCP (Linux Control Plane) management */
int vpp_lcp_create(const char *ifname, const char *host_ifname,
                   const char *netns, bool is_tun);
int vpp_lcp_delete(const char *ifname);
int vpp_lcp_set_default_netns(const char *netns);
int vpp_lcp_set_sync(bool enable);
int vpp_lcp_set_auto_subint(bool enable);
char *vpp_lcp_show(void);

#endif /* _VPP_INTERFACE_H_ */
