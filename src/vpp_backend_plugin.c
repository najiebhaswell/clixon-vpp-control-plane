/*
 * VPP Backend Plugin for Clixon - Simplified
 *
 * This plugin receives commit callbacks and applies config to VPP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/* Cligen first */
#include <cligen/cligen.h>
/* Clixon headers */
#include <clixon/clixon.h>
#include <clixon/clixon_backend.h>
#include <clixon/clixon_datastore.h>
#include <clixon/clixon_xml.h>
#include <clixon/clixon_xpath.h>

/* Execute vppctl command */
static int vpp_exec(const char *cmd) {
  char full_cmd[512];
  /* Explicitly use socket and redirect output to log file for debugging */
  snprintf(full_cmd, sizeof(full_cmd),
           "/usr/bin/vppctl -s /run/vpp/cli.sock %s >> "
           "/var/log/vpp/backend-vppctl.log 2>&1",
           cmd);
  syslog(LOG_INFO, "Executing: %s", full_cmd);
  /* Use direct vppctl without sudo inside container/if running as root */
  return system(full_cmd);
}

static void apply_config(cxobj *root) {
  cxobj *bonds = NULL;
  cxobj *interfaces = NULL;
  cxobj *lcps = NULL;
  cxobj *sub;
  char *name;
  char cmd[256];
  cxobj *c = NULL;

  /* 1. Bonds */
  xpath_first(root, NULL, "bonds", &bonds);
  if (bonds) {
    c = NULL;
    while ((c = xml_child_each(bonds, c, CX_ELMNT)) != NULL) {
      name = xml_find_body(c, "name");
      char *id = xml_find_body(c, "id");
      char *mode = xml_find_body(c, "mode");
      char *lb = xml_find_body(c, "load-balance");

      if (name && id) {
        snprintf(cmd, sizeof(cmd), "create bond mode %s id %s load-balance %s",
                 mode ? mode : "lacp", id, lb ? lb : "l2");
        vpp_exec(cmd);

        /* Members */
        char *members = xml_find_body(c, "members");
        if (members) {
          char *p = strtok(members, ",");
          while (p) {
            /* trim */
            while (*p == ' ')
              p++;
            snprintf(cmd, sizeof(cmd), "bond add %s %s", name, p);
            vpp_exec(cmd);
            p = strtok(NULL, ",");
          }
        }

        /* Enable bond interface */
        snprintf(cmd, sizeof(cmd), "set interface state %s up", name);
        vpp_exec(cmd);
      }
    }
  }

  /* 2. Interfaces */
  xpath_first(root, NULL, "interfaces", &interfaces);
  if (interfaces) {
    c = NULL;
    while ((c = xml_child_each(interfaces, c, CX_ELMNT)) != NULL) {
      name = xml_find_body(c, "name");
      if (!name)
        continue;

      /* Enabled/State */
      char *enabled = xml_find_body(c, "enabled");
      if (enabled && strcmp(enabled, "true") == 0) {
        snprintf(cmd, sizeof(cmd), "set interface state %s up", name);
        vpp_exec(cmd);
      }

      /* MTU */
      char *mtu = xml_find_body(c, "mtu");
      if (mtu) {
        snprintf(cmd, sizeof(cmd), "set interface mtu %s %s", mtu, name);
        vpp_exec(cmd);
      }

      /* IPv4 */
      xpath_first(c, NULL, "ipv4-address", &sub);
      if (sub) {
        char *addr = xml_find_body(sub, "address");
        char *pfx = xml_find_body(sub, "prefix-length");
        if (addr && pfx) {
          snprintf(cmd, sizeof(cmd), "set interface ip address %s %s/%s", name,
                   addr, pfx);
          vpp_exec(cmd);
        }
      }

      /* IPv6 */
      xpath_first(c, NULL, "ipv6-address", &sub);
      if (sub) {
        char *addr = xml_find_body(sub, "address");
        char *pfx = xml_find_body(sub, "prefix-length");
        if (addr && pfx) {
          snprintf(cmd, sizeof(cmd), "set interface ip address %s %s/%s", name,
                   addr, pfx);
          vpp_exec(cmd);
        }
      }
    }
  }

  /* 3. LCPs */
  xpath_first(root, NULL, "lcps", &lcps);
  if (lcps) {
    c = NULL;
    while ((c = xml_child_each(lcps, c, CX_ELMNT)) != NULL) {
      char *vpp_if = xml_find_body(c, "vpp-interface");
      char *host_if = xml_find_body(c, "host-interface");
      char *netns = xml_find_body(c, "netns");

      if (vpp_if && host_if) {
        if (netns)
          snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s netns %s",
                   vpp_if, host_if, netns);
        else
          snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s", vpp_if,
                   host_if);
        vpp_exec(cmd);
      }
    }
  }
}

/*
 * Transaction commit callback
 */
static int vpp_transaction_commit(clixon_handle h, transaction_data td) {
  FILE *dbg = fopen("/tmp/backend_debug.log", "a");
  if (dbg) {
    fprintf(dbg, "COMMIT CALLBACK CALLED\n");
    fflush(dbg);
  }

  clixon_log(h, LOG_INFO, "VPP Backend: Commit received");

  cxobj *root = NULL;

  if (xmldb_get(h, "running", NULL, NULL, &root) >= 0 && root) {
    if (dbg)
      fprintf(dbg, "Got running DB, applying config...\n");
    clixon_log(h, LOG_INFO, "Applying configuration to VPP...");
    apply_config(root);
    if (dbg)
      fprintf(dbg, "apply_config done\n");
  } else {
    if (dbg)
      fprintf(dbg, "FAILED to get running DB\n");
    clixon_log(h, LOG_ERR, "Failed to get running DB");
  }

  if (dbg)
    fclose(dbg);
  return 0;
}

/*
 * Daemon startup callback
 */
static int vpp_daemon_start(clixon_handle h) {
  clixon_log(h, LOG_INFO, "VPP Backend: Daemon started");
  return 0;
}

/*
 * Plugin API structure
 */
static clixon_plugin_api api = {
    "vpp_backend",      /* name */
    clixon_plugin_init, /* init (set below) */
    NULL,               /* start */
    NULL,               /* exit */
    .ca_daemon = vpp_daemon_start,
    .ca_trans_commit = vpp_transaction_commit,
};

/*
 * Plugin initialization
 */
clixon_plugin_api *clixon_plugin_init(clixon_handle h) {
  clixon_log(h, LOG_INFO, "VPP Backend plugin initialized");
  return &api;
}
