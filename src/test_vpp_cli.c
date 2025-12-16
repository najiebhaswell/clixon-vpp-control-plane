/*
 * test_vpp_cli.c - Simple test program for VPP CLI connection
 */

#include "vpp_connection.h"
#include "vpp_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  vpp_interface_info_t *interfaces = NULL;
  vpp_interface_info_t *iface;
  char mac_str[18];

  printf("=== VPP CLI Connection Test ===\n\n");

  printf("Connecting to VPP...\n");
  if (vpp_connect() != 0) {
    fprintf(stderr, "Failed to connect to VPP\n");
    return 1;
  }

  printf("Dumping interfaces...\n\n");
  if (vpp_interface_dump(&interfaces) != 0) {
    fprintf(stderr, "Failed to dump interfaces\n");
    vpp_disconnect();
    return 1;
  }

  printf("%-30s %-6s %-10s %-8s %-8s %-18s\n", "Name", "Index", "Type", "Admin",
         "MTU", "MAC");
  printf("%-30s %-6s %-10s %-8s %-8s %-18s\n", "----", "-----", "----", "-----",
         "---", "---");

  for (iface = interfaces; iface; iface = iface->next) {
    vpp_mac_bytes_to_string(iface->mac, mac_str, sizeof(mac_str));
    printf("%-30s %-6u %-10s %-8s %-8u %s\n", iface->name, iface->sw_if_index,
           iface->type, iface->admin_up ? "up" : "down", iface->mtu, mac_str);
  }

  vpp_interface_list_free(interfaces);

  printf("\n=== Test Complete ===\n");

  return 0;
}
