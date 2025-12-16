/*
 * VPP API Integration for Clixon CLI
 * Uses VAPI (VPP API) for direct communication with VPP
 *
 * For now, this is a simplified version focusing on bonds and interfaces.
 * LCP requires more complex handling with the v2 API.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vapi/bond.api.vapi.h>
#include <vapi/interface.api.vapi.h>
#include <vapi/lcp.api.vapi.h>
#include <vapi/vapi.h>

#include "vpp_api.h"

/* VAPI context */
static vapi_ctx_t vapi_ctx = NULL;
static bool api_connected = false;

/* Define message IDs */
DEFINE_VAPI_MSG_IDS_BOND_API_JSON;
DEFINE_VAPI_MSG_IDS_INTERFACE_API_JSON;
DEFINE_VAPI_MSG_IDS_LCP_API_JSON;

/* Bond mode strings - indexed by VPP API enum */
static const char *bond_modes[] = {
    "unknown",       /* 0 - not used */
    "round-robin",   /* 1 - BOND_API_MODE_ROUND_ROBIN */
    "active-backup", /* 2 - BOND_API_MODE_ACTIVE_BACKUP */
    "xor",           /* 3 - BOND_API_MODE_XOR */
    "broadcast",     /* 4 - BOND_API_MODE_BROADCAST */
    "lacp",          /* 5 - BOND_API_MODE_LACP */
};

/* Load balance strings - indexed by VPP API enum */
static const char *lb_modes[] = {
    "l2",  /* 0 - BOND_API_LB_ALGO_L2 */
    "l34", /* 1 - BOND_API_LB_ALGO_L34 */
    "l23", /* 2 - BOND_API_LB_ALGO_L23 */
    "rr",  /* 3 - BOND_API_LB_ALGO_RR */
    "bc",  /* 4 - BOND_API_LB_ALGO_BC */
    "ab",  /* 5 - BOND_API_LB_ALGO_AB */
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

int vpp_api_connect(const char *client_name) {
  if (api_connected)
    return 0;

  vapi_error_e rv = vapi_ctx_alloc(&vapi_ctx);
  if (rv != VAPI_OK) {
    fprintf(stderr, "VPP API: Failed to allocate context: %d\n", rv);
    return -1;
  }

  rv = vapi_connect(vapi_ctx, client_name, NULL, 256, 128, VAPI_MODE_BLOCKING,
                    true);
  if (rv != VAPI_OK) {
    fprintf(stderr, "VPP API: Failed to connect: %d\n", rv);
    vapi_ctx_free(vapi_ctx);
    vapi_ctx = NULL;
    return -1;
  }

  api_connected = true;
  return 0;
}

void vpp_api_disconnect(void) {
  if (!api_connected || !vapi_ctx)
    return;

  vapi_disconnect(vapi_ctx);
  vapi_ctx_free(vapi_ctx);
  vapi_ctx = NULL;
  api_connected = false;
}

bool vpp_api_is_connected(void) { return api_connected; }

/* Callback for bond dump */
typedef struct {
  vpp_bond_info_t *bonds;
  int max_bonds;
  int count;
} bond_dump_ctx_t;

static vapi_error_e
bond_dump_cb(struct vapi_ctx_s *ctx, void *callback_ctx, vapi_error_e rv,
             bool is_last, vapi_payload_sw_bond_interface_details *reply) {
  (void)ctx;
  (void)rv;

  if (!reply || is_last)
    return VAPI_OK;

  bond_dump_ctx_t *dctx = (bond_dump_ctx_t *)callback_ctx;
  if (dctx->count >= dctx->max_bonds)
    return VAPI_OK;

  vpp_bond_info_t *bond = &dctx->bonds[dctx->count];

  /* Use interface_name from reply if available */
  if (reply->interface_name[0]) {
    snprintf(bond->name, sizeof(bond->name), "%s",
             (char *)reply->interface_name);
  } else {
    snprintf(bond->name, sizeof(bond->name), "BondEthernet%u", reply->id);
  }

  bond->sw_if_index = reply->sw_if_index;
  bond->id = reply->id;
  bond->mode = reply->mode;
  bond->lb = reply->lb;
  bond->active_members = reply->active_members;
  bond->members = reply->members;

  dctx->count++;

  return VAPI_OK;
}

int vpp_api_get_bonds(vpp_bond_info_t *bonds, int max_bonds) {
  if (!api_connected || !vapi_ctx)
    return -1;

  bond_dump_ctx_t ctx = {.bonds = bonds, .max_bonds = max_bonds, .count = 0};

  vapi_msg_sw_bond_interface_dump *msg =
      vapi_alloc_sw_bond_interface_dump(vapi_ctx);
  if (!msg)
    return -1;

  msg->payload.sw_if_index = ~0; /* All bonds */

  vapi_error_e rv =
      vapi_sw_bond_interface_dump(vapi_ctx, msg, bond_dump_cb, &ctx);
  if (rv != VAPI_OK)
    return -1;

  return ctx.count;
}

/* LCP dump context */
typedef struct {
  vpp_lcp_info_t *lcps;
  int max_lcps;
  int count;
  /* Interface names cache - need to resolve sw_if_index to name */
  vpp_interface_info_t *ifs;
  int if_count;
} lcp_dump_ctx_t;

/* Helper to find interface name by sw_if_index */
static const char *find_if_name_by_index(vpp_interface_info_t *ifs, int count,
                                         uint32_t sw_if_index) {
  for (int i = 0; i < count; i++) {
    if (ifs[i].sw_if_index == sw_if_index)
      return ifs[i].name;
  }
  return NULL;
}

/* LCP details callback */
static vapi_error_e lcp_details_cb(struct vapi_ctx_s *ctx, void *callback_ctx,
                                   vapi_error_e rv, bool is_last,
                                   vapi_payload_lcp_itf_pair_details *reply) {
  (void)ctx;
  (void)rv;

  if (!reply || is_last)
    return VAPI_OK;

  lcp_dump_ctx_t *dctx = (lcp_dump_ctx_t *)callback_ctx;
  if (dctx->count >= dctx->max_lcps)
    return VAPI_OK;

  vpp_lcp_info_t *lcp = &dctx->lcps[dctx->count];

  /* Get VPP interface name from sw_if_index */
  const char *vpp_if_name =
      find_if_name_by_index(dctx->ifs, dctx->if_count, reply->phy_sw_if_index);
  if (vpp_if_name) {
    snprintf(lcp->vpp_if, sizeof(lcp->vpp_if), "%s", vpp_if_name);
  } else {
    snprintf(lcp->vpp_if, sizeof(lcp->vpp_if), "sw_if_index:%u",
             reply->phy_sw_if_index);
  }

  snprintf(lcp->host_if, sizeof(lcp->host_if), "%s",
           (char *)reply->host_if_name);
  lcp->phy_sw_if_index = reply->phy_sw_if_index;
  lcp->host_sw_if_index = reply->host_sw_if_index;

  if (reply->netns[0]) {
    snprintf(lcp->netns, sizeof(lcp->netns), "%s", (char *)reply->netns);
  }

  dctx->count++;
  return VAPI_OK;
}

/* LCP reply callback */
static vapi_error_e
lcp_reply_cb(struct vapi_ctx_s *ctx, void *callback_ctx, vapi_error_e rv,
             bool is_last, vapi_payload_lcp_itf_pair_get_v2_reply *reply) {
  (void)ctx;
  (void)callback_ctx;
  (void)rv;
  (void)is_last;
  (void)reply;
  return VAPI_OK;
}

int vpp_api_get_lcps(vpp_lcp_info_t *lcps, int max_lcps) {
  if (!api_connected || !vapi_ctx)
    return -1;

  /* First get all interfaces to resolve sw_if_index to names */
  vpp_interface_info_t ifs[128];
  int if_count = vpp_api_get_interfaces(ifs, 128);
  if (if_count < 0)
    if_count = 0;

  lcp_dump_ctx_t ctx = {
      .lcps = lcps,
      .max_lcps = max_lcps,
      .count = 0,
      .ifs = ifs,
      .if_count = if_count,
  };

  vapi_msg_lcp_itf_pair_get_v2 *msg = vapi_alloc_lcp_itf_pair_get_v2(vapi_ctx);
  if (!msg)
    return -1;

  msg->payload.cursor = 0;
  msg->payload.sw_if_index = ~0; /* All LCPs */

  vapi_error_e rv = vapi_lcp_itf_pair_get_v2(vapi_ctx, msg, lcp_reply_cb, &ctx,
                                             lcp_details_cb, &ctx);
  if (rv != VAPI_OK)
    return -1;

  return ctx.count;
}

/* Callback for interface dump */
typedef struct {
  vpp_interface_info_t *ifs;
  int max_ifs;
  int count;
} if_dump_ctx_t;

static vapi_error_e if_dump_cb(struct vapi_ctx_s *ctx, void *callback_ctx,
                               vapi_error_e rv, bool is_last,
                               vapi_payload_sw_interface_details *reply) {
  (void)ctx;
  (void)rv;

  if (!reply || is_last)
    return VAPI_OK;

  if_dump_ctx_t *dctx = (if_dump_ctx_t *)callback_ctx;
  if (dctx->count >= dctx->max_ifs)
    return VAPI_OK;

  vpp_interface_info_t *iface = &dctx->ifs[dctx->count];

  snprintf(iface->name, sizeof(iface->name), "%s",
           (char *)reply->interface_name);
  iface->sw_if_index = reply->sw_if_index;
  iface->admin_up = (reply->flags & IF_STATUS_API_FLAG_ADMIN_UP) != 0;
  iface->link_up = (reply->flags & IF_STATUS_API_FLAG_LINK_UP) != 0;
  iface->mtu = reply->mtu[0]; /* L3 MTU */

  dctx->count++;
  return VAPI_OK;
}

int vpp_api_get_interfaces(vpp_interface_info_t *ifs, int max_ifs) {
  if (!api_connected || !vapi_ctx)
    return -1;

  if_dump_ctx_t ctx = {.ifs = ifs, .max_ifs = max_ifs, .count = 0};

  vapi_msg_sw_interface_dump *msg = vapi_alloc_sw_interface_dump(vapi_ctx, 0);
  if (!msg)
    return -1;

  msg->payload.sw_if_index = ~0;
  msg->payload.name_filter_valid = false;

  vapi_error_e rv = vapi_sw_interface_dump(vapi_ctx, msg, if_dump_cb, &ctx);
  if (rv != VAPI_OK) {
    fprintf(stderr, "VPP API: Interface dump failed: %d\n", rv);
    return -1;
  }

  return ctx.count;
}
