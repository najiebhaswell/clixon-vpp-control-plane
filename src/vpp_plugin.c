/*
 * vpp_plugin.c - Main Clixon backend plugin for VPP
 */

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/* Clixon includes */
#include <cligen/cligen.h>
#include <clixon/clixon.h>
#include <clixon/clixon_backend_transaction.h>

#include "vpp_connection.h"
#include "vpp_interface.h"

#define PLUGIN_NAME "vpp-control-plane"
#define VPP_NS "http://example.com/vpp/interfaces"

/*
 * RPC callback: create-loopback
 * Creates a new VPP loopback interface
 */
static int rpc_create_loopback(clixon_handle h, cxobj *xn, cbuf *cbret,
                               void *arg, void *regarg) {
  (void)arg;
  (void)regarg;
  char ifname[64] = {0};
  const char *mac_str = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC create-loopback called", PLUGIN_NAME);

  /* Get optional MAC address from input */
  cxobj *x_mac = xpath_first(xn, NULL, "mac-address");
  if (x_mac != NULL) {
    mac_str = xml_body(x_mac);
  }

  /* Ensure VPP connection */
  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  /* Create loopback */
  if (vpp_interface_create_loopback_mac(mac_str, ifname, sizeof(ifname)) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to create loopback interface</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }

  /* Get the sw-if-index of the created interface */
  uint32_t sw_if_index = vpp_interface_name_to_index(ifname);

  clixon_log(h, LOG_NOTICE, "%s: Created loopback %s (index %u)", PLUGIN_NAME,
             ifname, sw_if_index);

  /* Return success response */
  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<interface-name xmlns=\"%s\">%s</interface-name>"
          "<sw-if-index xmlns=\"%s\">%u</sw-if-index>"
          "</rpc-reply>",
          VPP_NS, ifname, VPP_NS, sw_if_index);

  return ret;
}

/*
 * RPC callback: delete-loopback
 * Deletes a VPP loopback interface
 */
static int rpc_delete_loopback(clixon_handle h, cxobj *xn, cbuf *cbret,
                               void *arg, void *regarg) {
  (void)arg;
  (void)regarg;
  const char *ifname = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC delete-loopback called", PLUGIN_NAME);

  /* Get interface name from input */
  cxobj *x_name = xpath_first(xn, NULL, "interface-name");
  if (x_name == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>interface-name is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  ifname = xml_body(x_name);
  if (ifname == NULL || *ifname == '\0') {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>invalid-value</error-tag>"
            "<error-message>interface-name cannot be empty</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }

  /* Ensure VPP connection */
  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  /* Delete loopback */
  if (vpp_interface_delete_loopback(ifname) != 0) {
    cprintf(
        cbret,
        "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
        "<rpc-error><error-type>application</error-type>"
        "<error-tag>operation-failed</error-tag>"
        "<error-message>Failed to delete loopback interface %s</error-message>"
        "</rpc-error></rpc-reply>",
        ifname);
    return 0;
  }

  clixon_log(h, LOG_NOTICE, "%s: Deleted loopback %s", PLUGIN_NAME, ifname);

  /* Return success response */
  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<result xmlns=\"%s\">true</result>"
          "</rpc-reply>",
          VPP_NS);

  return ret;
}

/*
 * RPC callback: create-sub-interface
 * Creates a VLAN sub-interface
 */
static int rpc_create_subif(clixon_handle h, cxobj *xn, cbuf *cbret, void *arg,
                            void *regarg) {
  (void)arg;
  (void)regarg;
  char ifname[128] = {0};
  const char *parent = NULL;
  uint16_t vlan_id = 0;
  uint32_t sub_id = 0;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC create-sub-interface called", PLUGIN_NAME);

  /* Get parent interface */
  cxobj *x_parent = xpath_first(xn, NULL, "parent-interface");
  if (x_parent == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>parent-interface is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  parent = xml_body(x_parent);

  /* Get VLAN ID */
  cxobj *x_vlan = xpath_first(xn, NULL, "vlan-id");
  if (x_vlan == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>vlan-id is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  vlan_id = (uint16_t)atoi(xml_body(x_vlan));

  /* Get optional sub-id */
  cxobj *x_subid = xpath_first(xn, NULL, "sub-id");
  if (x_subid != NULL) {
    sub_id = (uint32_t)atoi(xml_body(x_subid));
  }

  /* Ensure VPP connection */
  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  /* Create sub-interface */
  if (vpp_interface_create_subif(parent, vlan_id, sub_id, ifname,
                                 sizeof(ifname)) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to create sub-interface</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }

  /* Get the sw-if-index */
  uint32_t sw_if_index = vpp_interface_name_to_index(ifname);

  clixon_log(h, LOG_NOTICE, "%s: Created sub-interface %s (VLAN %u, index %u)",
             PLUGIN_NAME, ifname, vlan_id, sw_if_index);

  /* Return success response */
  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<interface-name xmlns=\"%s\">%s</interface-name>"
          "<sw-if-index xmlns=\"%s\">%u</sw-if-index>"
          "</rpc-reply>",
          VPP_NS, ifname, VPP_NS, sw_if_index);

  return ret;
}

/*
 * RPC callback: delete-sub-interface
 * Deletes a sub-interface
 */
static int rpc_delete_subif(clixon_handle h, cxobj *xn, cbuf *cbret, void *arg,
                            void *regarg) {
  (void)arg;
  (void)regarg;
  const char *ifname = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC delete-sub-interface called", PLUGIN_NAME);

  /* Get interface name */
  cxobj *x_name = xpath_first(xn, NULL, "interface-name");
  if (x_name == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>interface-name is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  ifname = xml_body(x_name);

  /* Ensure VPP connection */
  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  /* Delete sub-interface */
  if (vpp_interface_delete_subif(ifname) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to delete sub-interface %s</error-message>"
            "</rpc-error></rpc-reply>",
            ifname);
    return 0;
  }

  clixon_log(h, LOG_NOTICE, "%s: Deleted sub-interface %s", PLUGIN_NAME,
             ifname);

  /* Return success response */
  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<result xmlns=\"%s\">true</result>"
          "</rpc-reply>",
          VPP_NS);

  return ret;
}

/*
 * RPC callback: create-bond
 * Creates a bonding interface
 */
static int rpc_create_bond(clixon_handle h, cxobj *xn, cbuf *cbret, void *arg,
                           void *regarg) {
  (void)arg;
  (void)regarg;
  char ifname[64] = {0};
  const char *mode = NULL;
  const char *lb = NULL;
  const char *mac_str = NULL;
  uint32_t bond_id = 0;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC create-bond called", PLUGIN_NAME);

  /* Get mode */
  cxobj *x_mode = xpath_first(xn, NULL, "mode");
  if (x_mode == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>mode is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  mode = xml_body(x_mode);

  /* Get optional load-balance */
  cxobj *x_lb = xpath_first(xn, NULL, "load-balance");
  if (x_lb != NULL) {
    lb = xml_body(x_lb);
  }

  /* Get optional MAC */
  cxobj *x_mac = xpath_first(xn, NULL, "mac-address");
  if (x_mac != NULL) {
    mac_str = xml_body(x_mac);
  }

  /* Get optional bond-id */
  cxobj *x_id = xpath_first(xn, NULL, "bond-id");
  if (x_id != NULL) {
    bond_id = (uint32_t)atoi(xml_body(x_id));
  }

  /* Ensure VPP connection */
  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  /* Create bond */
  if (vpp_interface_create_bond(mode, lb, mac_str, bond_id, ifname,
                                sizeof(ifname)) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to create bond interface</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }

  uint32_t sw_if_index = vpp_interface_name_to_index(ifname);

  clixon_log(h, LOG_NOTICE, "%s: Created bond %s (mode %s, index %u)",
             PLUGIN_NAME, ifname, mode, sw_if_index);

  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<interface-name xmlns=\"%s\">%s</interface-name>"
          "<sw-if-index xmlns=\"%s\">%u</sw-if-index>"
          "</rpc-reply>",
          VPP_NS, ifname, VPP_NS, sw_if_index);

  return ret;
}

/*
 * RPC callback: delete-bond
 */
static int rpc_delete_bond(clixon_handle h, cxobj *xn, cbuf *cbret, void *arg,
                           void *regarg) {
  (void)arg;
  (void)regarg;
  const char *ifname = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC delete-bond called", PLUGIN_NAME);

  cxobj *x_name = xpath_first(xn, NULL, "interface-name");
  if (x_name == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>interface-name is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  ifname = xml_body(x_name);

  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  if (vpp_interface_delete_bond(ifname) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to delete bond %s</error-message>"
            "</rpc-error></rpc-reply>",
            ifname);
    return 0;
  }

  clixon_log(h, LOG_NOTICE, "%s: Deleted bond %s", PLUGIN_NAME, ifname);

  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<result xmlns=\"%s\">true</result>"
          "</rpc-reply>",
          VPP_NS);

  return ret;
}

/*
 * RPC callback: bond-add-member
 */
static int rpc_bond_add_member(clixon_handle h, cxobj *xn, cbuf *cbret,
                               void *arg, void *regarg) {
  (void)arg;
  (void)regarg;
  const char *bond_if = NULL;
  const char *member_if = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC bond-add-member called", PLUGIN_NAME);

  cxobj *x_bond = xpath_first(xn, NULL, "bond-interface");
  if (x_bond == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>bond-interface is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  bond_if = xml_body(x_bond);

  cxobj *x_member = xpath_first(xn, NULL, "member-interface");
  if (x_member == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>member-interface is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  member_if = xml_body(x_member);

  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  if (vpp_interface_bond_add_member(bond_if, member_if) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to add %s to %s</error-message>"
            "</rpc-error></rpc-reply>",
            member_if, bond_if);
    return 0;
  }

  clixon_log(h, LOG_NOTICE, "%s: Added %s to bond %s", PLUGIN_NAME, member_if,
             bond_if);

  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<result xmlns=\"%s\">true</result>"
          "</rpc-reply>",
          VPP_NS);

  return ret;
}

/*
 * RPC callback: bond-del-member
 */
static int rpc_bond_del_member(clixon_handle h, cxobj *xn, cbuf *cbret,
                               void *arg, void *regarg) {
  (void)arg;
  (void)regarg;
  const char *member_if = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC bond-del-member called", PLUGIN_NAME);

  cxobj *x_member = xpath_first(xn, NULL, "member-interface");
  if (x_member == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>member-interface is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  member_if = xml_body(x_member);

  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  if (vpp_interface_bond_del_member(member_if) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to remove %s from bond</error-message>"
            "</rpc-error></rpc-reply>",
            member_if);
    return 0;
  }

  clixon_log(h, LOG_NOTICE, "%s: Removed %s from bond", PLUGIN_NAME, member_if);

  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<result xmlns=\"%s\">true</result>"
          "</rpc-reply>",
          VPP_NS);

  return ret;
}

/*
 * RPC callback: lcp-create
 * Creates an LCP pair to mirror VPP interface to Linux
 */
static int rpc_lcp_create(clixon_handle h, cxobj *xn, cbuf *cbret, void *arg,
                          void *regarg) {
  (void)arg;
  (void)regarg;
  const char *ifname = NULL;
  const char *host_if = NULL;
  const char *netns = NULL;
  bool is_tun = false;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC lcp-create called", PLUGIN_NAME);

  /* Get VPP interface name */
  cxobj *x_if = xpath_first(xn, NULL, "interface-name");
  if (x_if == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>interface-name is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  ifname = xml_body(x_if);

  /* Get Linux host interface name */
  cxobj *x_host = xpath_first(xn, NULL, "host-interface");
  if (x_host == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>host-interface is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  host_if = xml_body(x_host);

  /* Get optional netns */
  cxobj *x_netns = xpath_first(xn, NULL, "netns");
  if (x_netns != NULL) {
    netns = xml_body(x_netns);
  }

  /* Get optional tun flag */
  cxobj *x_tun = xpath_first(xn, NULL, "tun");
  if (x_tun != NULL) {
    const char *tun_str = xml_body(x_tun);
    is_tun = (tun_str && strcmp(tun_str, "true") == 0);
  }

  /* Ensure VPP connection */
  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  /* Create LCP pair */
  if (vpp_lcp_create(ifname, host_if, netns, is_tun) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to create LCP pair for %s</error-message>"
            "</rpc-error></rpc-reply>",
            ifname);
    return 0;
  }

  clixon_log(h, LOG_NOTICE, "%s: Created LCP pair %s -> %s", PLUGIN_NAME,
             ifname, host_if);

  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<result xmlns=\"%s\">true</result>"
          "</rpc-reply>",
          VPP_NS);

  return ret;
}

/*
 * RPC callback: lcp-delete
 */
static int rpc_lcp_delete(clixon_handle h, cxobj *xn, cbuf *cbret, void *arg,
                          void *regarg) {
  (void)arg;
  (void)regarg;
  const char *ifname = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC lcp-delete called", PLUGIN_NAME);

  cxobj *x_if = xpath_first(xn, NULL, "interface-name");
  if (x_if == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>interface-name is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  ifname = xml_body(x_if);

  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  if (vpp_lcp_delete(ifname) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to delete LCP pair for %s</error-message>"
            "</rpc-error></rpc-reply>",
            ifname);
    return 0;
  }

  clixon_log(h, LOG_NOTICE, "%s: Deleted LCP pair for %s", PLUGIN_NAME, ifname);

  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<result xmlns=\"%s\">true</result>"
          "</rpc-reply>",
          VPP_NS);

  return ret;
}

/*
 * RPC callback: lcp-set-netns
 */
static int rpc_lcp_set_netns(clixon_handle h, cxobj *xn, cbuf *cbret, void *arg,
                             void *regarg) {
  (void)arg;
  (void)regarg;
  const char *netns = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: RPC lcp-set-netns called", PLUGIN_NAME);

  cxobj *x_netns = xpath_first(xn, NULL, "netns");
  if (x_netns == NULL) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>missing-element</error-tag>"
            "<error-message>netns is required</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }
  netns = xml_body(x_netns);

  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      cprintf(cbret,
              "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
              "<rpc-error><error-type>application</error-type>"
              "<error-tag>operation-failed</error-tag>"
              "<error-message>Cannot connect to VPP</error-message>"
              "</rpc-error></rpc-reply>");
      return 0;
    }
  }

  if (vpp_lcp_set_default_netns(netns) != 0) {
    cprintf(cbret,
            "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
            "<rpc-error><error-type>application</error-type>"
            "<error-tag>operation-failed</error-tag>"
            "<error-message>Failed to set LCP default netns</error-message>"
            "</rpc-error></rpc-reply>");
    return 0;
  }

  clixon_log(h, LOG_NOTICE, "%s: Set LCP default netns to %s", PLUGIN_NAME,
             netns);

  cprintf(cbret,
          "<rpc-reply xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">"
          "<result xmlns=\"%s\">true</result>"
          "</rpc-reply>",
          VPP_NS);

  return ret;
}

/*
 * Plugin daemon start callback
 */
static int vpp_plugin_start(clixon_handle h) {
  clixon_log(h, LOG_NOTICE, "%s: Starting VPP plugin", PLUGIN_NAME);

  /* Register RPC callbacks */
  if (rpc_callback_register(h, rpc_create_loopback, NULL, VPP_NS,
                            "create-loopback") < 0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register create-loopback RPC");
  }
  if (rpc_callback_register(h, rpc_delete_loopback, NULL, VPP_NS,
                            "delete-loopback") < 0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register delete-loopback RPC");
  }
  if (rpc_callback_register(h, rpc_create_subif, NULL, VPP_NS,
                            "create-sub-interface") < 0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register create-sub-interface RPC");
  }
  if (rpc_callback_register(h, rpc_delete_subif, NULL, VPP_NS,
                            "delete-sub-interface") < 0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register delete-sub-interface RPC");
  }
  /* Bonding RPCs */
  if (rpc_callback_register(h, rpc_create_bond, NULL, VPP_NS, "create-bond") <
      0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register create-bond RPC");
  }
  if (rpc_callback_register(h, rpc_delete_bond, NULL, VPP_NS, "delete-bond") <
      0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register delete-bond RPC");
  }
  if (rpc_callback_register(h, rpc_bond_add_member, NULL, VPP_NS,
                            "bond-add-member") < 0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register bond-add-member RPC");
  }
  if (rpc_callback_register(h, rpc_bond_del_member, NULL, VPP_NS,
                            "bond-del-member") < 0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register bond-del-member RPC");
  }
  /* LCP RPCs */
  if (rpc_callback_register(h, rpc_lcp_create, NULL, VPP_NS, "lcp-create") <
      0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register lcp-create RPC");
  }
  if (rpc_callback_register(h, rpc_lcp_delete, NULL, VPP_NS, "lcp-delete") <
      0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register lcp-delete RPC");
  }
  if (rpc_callback_register(h, rpc_lcp_set_netns, NULL, VPP_NS,
                            "lcp-set-netns") < 0) {
    clixon_err(OE_PLUGIN, 0, "Failed to register lcp-set-netns RPC");
  }
  clixon_log(h, LOG_NOTICE,
             "%s: Registered all RPCs (loopback, sub-if, bond, lcp)",
             PLUGIN_NAME);

  if (vpp_connect() != 0) {
    clixon_err(OE_PLUGIN, 0, "Failed to connect to VPP - is VPP running?");
    clixon_log(h, LOG_WARNING,
               "%s: Will retry VPP connection on first operation", PLUGIN_NAME);
  }

  return 0;
}

/*
 * Plugin exit callback
 */
static int vpp_plugin_exit(clixon_handle h) {
  clixon_log(h, LOG_NOTICE, "%s: Stopping VPP plugin", PLUGIN_NAME);
  vpp_disconnect();
  return 0;
}

/*
 * Transaction begin callback
 */
static int vpp_trans_begin(clixon_handle h, transaction_data td) {
  (void)td;
  clixon_log(h, LOG_DEBUG, "%s: Transaction begin", PLUGIN_NAME);

  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      clixon_err(OE_PLUGIN, 0, "Cannot connect to VPP");
      return -1;
    }
  }

  return 0;
}

/*
 * Transaction validate callback
 */
static int vpp_trans_validate(clixon_handle h, transaction_data td) {
  (void)td;
  clixon_log(h, LOG_DEBUG, "%s: Transaction validate", PLUGIN_NAME);
  return 0;
}

/*
 * Transaction commit callback
 */
static int vpp_trans_commit(clixon_handle h, transaction_data td) {
  cxobj *target;
  cxobj *x_ifs;
  cxobj *x_if;
  cxobj **dvec;
  size_t dlen;
  size_t i;

  clixon_log(h, LOG_DEBUG, "%s: Transaction commit", PLUGIN_NAME);

  /* First, process deleted elements from dvec */
  dvec = transaction_dvec(td);
  dlen = transaction_dlen(td);

  for (i = 0; i < dlen; i++) {
    cxobj *del = dvec[i];
    if (del == NULL)
      continue;

    const char *name = xml_name(del);
    if (name == NULL)
      continue;

    /* Check if this is an address deletion */
    if (strcmp(name, "address") == 0) {
      const char *ip = xml_find_body(del, "ip");
      const char *prefix = xml_find_body(del, "prefix-length");

      if (ip && prefix) {
        /* Find parent interface name by walking up the XML tree */
        cxobj *parent = xml_parent(del); /* ipv4 or ipv6 container */
        if (parent) {
          parent = xml_parent(parent); /* interface */
          if (parent) {
            const char *ifname = xml_find_body(parent, "name");
            if (ifname) {
              char addr_str[128];
              snprintf(addr_str, sizeof(addr_str), "%s/%s", ip, prefix);
              clixon_log(h, LOG_DEBUG, "%s: Deleting IP %s from %s",
                         PLUGIN_NAME, addr_str, ifname);
              if (vpp_interface_del_ip_address(ifname, addr_str) != 0) {
                clixon_log(h, LOG_WARNING, "%s: Failed to delete IP %s from %s",
                           PLUGIN_NAME, addr_str, ifname);
              }
            }
          }
        }
      }
    }
  }

  /* Now process target configuration for additions */
  target = transaction_target(td);
  if (target == NULL) {
    return 0;
  }

  /* Find interfaces container */
  x_ifs = xpath_first(target, NULL, "/interfaces");
  if (x_ifs == NULL) {
    return 0;
  }

  /* Process each interface */
  x_if = NULL;
  while ((x_if = xml_child_each(x_ifs, x_if, CX_ELMNT)) != NULL) {
    if (strcmp(xml_name(x_if), "interface") != 0) {
      continue;
    }

    const char *ifname = xml_find_body(x_if, "name");
    if (ifname == NULL) {
      continue;
    }

    uint32_t sw_if_index = vpp_interface_name_to_index(ifname);
    if (sw_if_index == (uint32_t)-1) {
      clixon_log(h, LOG_WARNING, "Interface %s not found in VPP", ifname);
      continue;
    }

    /* Apply description (via vppctl since VAPI doesn't support it) */
    const char *description = xml_find_body(x_if, "description");
    if (description != NULL) {
      char cmd[512];
      snprintf(cmd, sizeof(cmd),
               "sudo vppctl -s /run/vpp/cli.sock set interface description %s "
               "\"%s\"",
               ifname, description);
      int ret_desc = system(cmd);
      if (ret_desc != 0) {
        clixon_log(h, LOG_WARNING, "Failed to set description for %s", ifname);
      } else {
        clixon_log(h, LOG_DEBUG, "%s: Set description for %s: %s", PLUGIN_NAME,
                   ifname, description);
      }
    }

    /* Apply enabled state */
    const char *enabled = xml_find_body(x_if, "enabled");
    if (enabled != NULL) {
      bool admin_up = (strcmp(enabled, "true") == 0);
      if (vpp_interface_set_flags(sw_if_index, admin_up) != 0) {
        clixon_err(OE_PLUGIN, 0, "Failed to set admin state for %s", ifname);
      }
    }

    /* Apply MTU */
    const char *mtu_str = xml_find_body(x_if, "mtu");
    if (mtu_str != NULL) {
      uint16_t mtu = (uint16_t)atoi(mtu_str);
      if (vpp_interface_set_mtu(sw_if_index, mtu) != 0) {
        clixon_err(OE_PLUGIN, 0, "Failed to set MTU for %s", ifname);
      }
    }

    /* Apply IPv4 addresses (only new ones via avec would be ideal,
       but for simplicity we try adding all - duplicates will fail silently) */
    cxobj *x_ipv4 = xml_find(x_if, "ipv4");
    if (x_ipv4 != NULL) {
      cxobj *x_addr = NULL;
      while ((x_addr = xml_child_each(x_ipv4, x_addr, CX_ELMNT)) != NULL) {
        if (strcmp(xml_name(x_addr), "address") != 0) {
          continue;
        }
        const char *ip = xml_find_body(x_addr, "ip");
        const char *prefix = xml_find_body(x_addr, "prefix-length");
        if (ip && prefix) {
          char addr_str[64];
          snprintf(addr_str, sizeof(addr_str), "%s/%s", ip, prefix);
          clixon_log(h, LOG_DEBUG, "%s: Adding IPv4 %s to %s", PLUGIN_NAME,
                     addr_str, ifname);
          /* Ignore errors for already-existing addresses */
          vpp_interface_add_ip_address(ifname, addr_str);
        }
      }
    }

    /* Apply IPv6 addresses */
    cxobj *x_ipv6 = xml_find(x_if, "ipv6");
    if (x_ipv6 != NULL) {
      cxobj *x_addr = NULL;
      while ((x_addr = xml_child_each(x_ipv6, x_addr, CX_ELMNT)) != NULL) {
        if (strcmp(xml_name(x_addr), "address") != 0) {
          continue;
        }
        const char *ip = xml_find_body(x_addr, "ip");
        const char *prefix = xml_find_body(x_addr, "prefix-length");
        if (ip && prefix) {
          char addr_str[128];
          snprintf(addr_str, sizeof(addr_str), "%s/%s", ip, prefix);
          clixon_log(h, LOG_DEBUG, "%s: Adding IPv6 %s to %s", PLUGIN_NAME,
                     addr_str, ifname);
          /* Ignore errors for already-existing addresses */
          vpp_interface_add_ip_address(ifname, addr_str);
        }
      }
    }
  }

  return 0;
}

/*
 * Helper: Create an XML element with text body
 * In Clixon, text content must be added as a CX_BODY child node
 */
static cxobj *vpp_xml_element(const char *name, cxobj *parent,
                              const char *value) {
  cxobj *x_elem;
  cxobj *x_body;

  x_elem = xml_new(name, parent, CX_ELMNT);
  if (x_elem == NULL) {
    return NULL;
  }

  if (value && *value) {
    x_body = xml_new("body", x_elem, CX_BODY);
    if (x_body) {
      xml_value_set(x_body, value);
    }
  }

  return x_elem;
}

/*
 * State data callback
 * Called by Clixon to populate operational/state data
 */
#define VPP_INTERFACES_NS "http://example.com/vpp/interfaces"

static int vpp_statedata(clixon_handle h, cvec *nsc, char *xpath,
                         cxobj *xstate) {
  (void)nsc;
  vpp_interface_info_t *interfaces = NULL;
  vpp_interface_info_t *curr;
  cxobj *x_ifs = NULL;
  int ret = 0;

  clixon_log(h, LOG_DEBUG, "%s: State data request for: %s", PLUGIN_NAME,
             xpath ? xpath : "(null)");

  /* Check if this request is for our interfaces module
   * Match both full path and simple "interfaces" queries */
  if (xpath == NULL ||
      (strstr(xpath, "interfaces") == NULL &&
       strstr(xpath, VPP_INTERFACES_NS) == NULL && strcmp(xpath, "/") != 0)) {
    clixon_log(h, LOG_DEBUG, "%s: xpath does not match, skipping", PLUGIN_NAME);
    return 0;
  }

  clixon_log(h, LOG_DEBUG, "%s: Fetching VPP interfaces", PLUGIN_NAME);

  /* Ensure VPP connection */
  if (!vpp_is_connected()) {
    if (vpp_connect() != 0) {
      clixon_log(h, LOG_WARNING, "%s: Cannot connect to VPP", PLUGIN_NAME);
      return 0;
    }
  }

  /* Get interfaces from VPP */
  if (vpp_interface_dump(&interfaces) != 0) {
    clixon_log(h, LOG_WARNING, "%s: Failed to dump VPP interfaces",
               PLUGIN_NAME);
    return 0;
  }

  /* Create interfaces container with proper namespace */
  x_ifs = xml_new("interfaces", xstate, CX_ELMNT);
  if (x_ifs == NULL) {
    clixon_log(h, LOG_ERR, "%s: Failed to create interfaces element",
               PLUGIN_NAME);
    ret = -1;
    goto done;
  }

  /* Set namespace on the container */
  if (xmlns_set(x_ifs, NULL, VPP_INTERFACES_NS) < 0) {
    clixon_log(h, LOG_WARNING, "%s: Failed to set namespace", PLUGIN_NAME);
  }

  /* Iterate through VPP interfaces */
  for (curr = interfaces; curr != NULL; curr = curr->next) {
    cxobj *x_if = xml_new("interface", x_ifs, CX_ELMNT);
    if (x_if == NULL) {
      continue;
    }

    char buf[64];

    /* Name (key) */
    vpp_xml_element("name", x_if, curr->name);

    /* Type */
    vpp_xml_element("type", x_if, curr->type);

    /* sw-if-index */
    snprintf(buf, sizeof(buf), "%u", curr->sw_if_index);
    vpp_xml_element("sw-if-index", x_if, buf);

    /* oper-status */
    vpp_xml_element("oper-status", x_if, curr->link_up ? "up" : "down");

    /* enabled (admin status) */
    vpp_xml_element("enabled", x_if, curr->admin_up ? "true" : "false");

    /* MAC address */
    char mac_str[18];
    vpp_mac_bytes_to_string(curr->mac, mac_str, sizeof(mac_str));
    vpp_xml_element("mac-address", x_if, mac_str);

    /* MTU */
    snprintf(buf, sizeof(buf), "%u", curr->mtu);
    vpp_xml_element("mtu", x_if, buf);

    /* Link speed (if available) */
    if (curr->link_speed > 0) {
      snprintf(buf, sizeof(buf), "%u", curr->link_speed);
      vpp_xml_element("speed", x_if, buf);
    }
  }

  clixon_log(h, LOG_DEBUG, "%s: State data populated successfully",
             PLUGIN_NAME);

done:
  vpp_interface_list_free(interfaces);
  return ret;
}

/*
 * Plugin API structure
 */
static clixon_plugin_api api = {
    .ca_name = PLUGIN_NAME,
    .ca_init = NULL, /* Set below */
    .ca_start = vpp_plugin_start,
    .ca_exit = vpp_plugin_exit,
    .ca_trans_begin = vpp_trans_begin,
    .ca_trans_validate = vpp_trans_validate,
    .ca_trans_commit = vpp_trans_commit,
    .ca_statedata = vpp_statedata,
};

/*
 * Plugin initialization - entry point
 * Note: New Clixon API takes only clixon_handle
 */
clixon_plugin_api *clixon_plugin_init(clixon_handle h) {
  clixon_log(h, LOG_NOTICE, "%s: Plugin init", PLUGIN_NAME);
  api.ca_init = clixon_plugin_init;
  return &api;
}
