/*
 * VPP CLI Plugin - Cisco/Juniper Style with Mode Hierarchy
 * Note: Mode switching is done via CLI spec using cli_set_mode("mode")
 */

#include <arpa/inet.h>
#include <cligen/cligen.h>
#include <clixon/clixon.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "vpp_api.h"

/* Current interface context - stored in cligen userdata */
static char current_interface[128] = "";

/* Track if config has been modified since last commit */
static int config_modified = 0;

/* Macro to mark config as modified after successful change */
#define CONFIG_CHANGED()                                                       \
  do {                                                                         \
    config_modified = 1;                                                       \
  } while (0)

/* Execute vppctl command */
static int vpp_exec(const char *cmd, char *output, size_t output_len) {
  char full_cmd[512];
  FILE *fp;

  snprintf(full_cmd, sizeof(full_cmd),
           "sudo vppctl -s /run/vpp/cli.sock %s 2>&1", cmd);

  fp = popen(full_cmd, "r");
  if (!fp)
    return -1;

  if (output && output_len > 0) {
    output[0] = '\0';
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      size_t len = strlen(output);
      if (len + strlen(buf) < output_len - 1)
        strcat(output, buf);
    }
  }

  int ret = pclose(fp);
  return WEXITSTATUS(ret);
}

/*=============================================================
 * DATASTORE HELPERS - Save config to Clixon candidate datastore
 *=============================================================*/

/* Namespace for VPP interfaces */
#define VPP_INTERFACES_NS "http://example.com/vpp/interfaces"
#define VPP_BONDS_NS "http://example.com/vpp/bonds"
#define VPP_LCP_NS "http://example.com/vpp/lcp"

/* Config file path */
#define VPP_CONFIG_FILE "/var/lib/clixon/vpp/vpp_config.xml"

/* In-memory pending config - stored as simple list */
typedef struct pending_config {
  char ifname[128];
  char enabled[8];
  char mtu[16];
  char ipv4_addr[64];
  int ipv4_prefix;
  char ipv6_addr[128];
  int ipv6_prefix;
  struct pending_config *next;
} pending_config_t;

/* Bond config structure */
typedef struct bond_config {
  char name[64];
  char mode[32];
  char lb[16];
  int id;
  char members[512]; /* Comma-separated member list */
  struct bond_config *next;
} bond_config_t;

/* LCP config structure */
typedef struct lcp_config {
  char vpp_if[128];
  char host_if[64];
  char netns[64];
  struct lcp_config *next;
} lcp_config_t;

/* Sub-interface config structure */
typedef struct subif_config {
  char name[128];   /* Full name: parent.vlanid */
  char parent[128]; /* Parent interface */
  int vlanid;       /* VLAN ID */
  struct subif_config *next;
} subif_config_t;

static pending_config_t *pending_interfaces = NULL;
static bond_config_t *pending_bonds = NULL;
static lcp_config_t *pending_lcps = NULL;
static subif_config_t *pending_subifs = NULL;

/* Forward declaration */
static void ds_load_config_from_file(void);

/* Sanitize interface name - strip newlines and trailing spaces */
static void sanitize_ifname(char *name) {
  if (!name)
    return;
  char *p = name;
  while (*p) {
    if (*p == '\n' || *p == '\r') {
      *p = '\0';
      break;
    }
    p++;
  }
  /* Trim trailing spaces */
  p = name + strlen(name) - 1;
  while (p >= name && (*p == ' ' || *p == '\t')) {
    *p = '\0';
    p--;
  }
}

/* Add or update interface in pending config */
static int ds_save_interface(clixon_handle h, const char *ifname,
                             const char *enabled, const char *mtu,
                             const char *ipv4_addr, int ipv4_prefix,
                             const char *ipv6_addr, int ipv6_prefix) {
  (void)h;

  if (!ifname)
    return -1;

  /* Sanitize interface name */
  char clean_ifname[128];
  strncpy(clean_ifname, ifname, sizeof(clean_ifname) - 1);
  clean_ifname[sizeof(clean_ifname) - 1] = '\0';
  sanitize_ifname(clean_ifname);

  /* Load existing config from file first */
  ds_load_config_from_file();

  /* Find existing or create new */
  pending_config_t *cfg = pending_interfaces;
  while (cfg) {
    if (strcmp(cfg->ifname, clean_ifname) == 0)
      break;
    cfg = cfg->next;
  }

  if (!cfg) {
    cfg = calloc(1, sizeof(pending_config_t));
    if (!cfg)
      return -1;
    strncpy(cfg->ifname, clean_ifname, sizeof(cfg->ifname) - 1);
    cfg->next = pending_interfaces;
    pending_interfaces = cfg;
  }

  /* Update fields */
  if (enabled)
    strncpy(cfg->enabled, enabled, sizeof(cfg->enabled) - 1);
  if (mtu)
    strncpy(cfg->mtu, mtu, sizeof(cfg->mtu) - 1);
  if (ipv4_addr) {
    strncpy(cfg->ipv4_addr, ipv4_addr, sizeof(cfg->ipv4_addr) - 1);
    cfg->ipv4_prefix = ipv4_prefix;
  }
  if (ipv6_addr) {
    strncpy(cfg->ipv6_addr, ipv6_addr, sizeof(cfg->ipv6_addr) - 1);
    cfg->ipv6_prefix = ipv6_prefix;
  }

  return 0;
}

/* Save bond config to pending list */
static int ds_save_bond_config(const char *name, const char *mode,
                               const char *lb, int id) {
  ds_load_config_from_file();

  if (!name)
    return -1;

  /* Sanitize bond name */
  char clean_name[64];
  strncpy(clean_name, name, sizeof(clean_name) - 1);
  clean_name[sizeof(clean_name) - 1] = '\0';
  sanitize_ifname(clean_name);

  /* Find existing or create new */
  bond_config_t *cfg = pending_bonds;
  while (cfg) {
    if (strcmp(cfg->name, clean_name) == 0)
      break;
    cfg = cfg->next;
  }

  if (!cfg) {
    cfg = calloc(1, sizeof(bond_config_t));
    if (!cfg)
      return -1;
    strncpy(cfg->name, clean_name, sizeof(cfg->name) - 1);
    cfg->next = pending_bonds;
    pending_bonds = cfg;
  }

  if (mode)
    strncpy(cfg->mode, mode, sizeof(cfg->mode) - 1);
  if (lb)
    strncpy(cfg->lb, lb, sizeof(cfg->lb) - 1);
  cfg->id = id;

  return 0;
}

/* Add member to bond config */
static int ds_add_bond_member(const char *bondname, const char *member) {
  ds_load_config_from_file();

  if (!bondname || !member)
    return -1;

  bond_config_t *cfg = pending_bonds;
  while (cfg) {
    if (strcmp(cfg->name, bondname) == 0) {
      if (cfg->members[0] != '\0') {
        strncat(cfg->members, ",",
                sizeof(cfg->members) - strlen(cfg->members) - 1);
      }
      strncat(cfg->members, member,
              sizeof(cfg->members) - strlen(cfg->members) - 1);
      return 0;
    }
    cfg = cfg->next;
  }
  return -1;
}

/* Save LCP config to pending list */
static int ds_save_lcp_config(const char *vpp_if, const char *host_if,
                              const char *netns) {
  ds_load_config_from_file();

  if (!vpp_if || !host_if)
    return -1;

  /* Find existing or create new */
  lcp_config_t *cfg = pending_lcps;
  while (cfg) {
    if (strcmp(cfg->vpp_if, vpp_if) == 0)
      break;
    cfg = cfg->next;
  }

  if (!cfg) {
    cfg = calloc(1, sizeof(lcp_config_t));
    if (!cfg)
      return -1;
    cfg->next = pending_lcps;
    pending_lcps = cfg;
  }

  strncpy(cfg->vpp_if, vpp_if, sizeof(cfg->vpp_if) - 1);
  strncpy(cfg->host_if, host_if, sizeof(cfg->host_if) - 1);
  if (netns)
    strncpy(cfg->netns, netns, sizeof(cfg->netns) - 1);

  return 0;
}

/* Save sub-interface config to pending list */
static int ds_save_subif_config(const char *name, const char *parent,
                                int vlanid) {
  ds_load_config_from_file();

  if (!name || !parent || vlanid <= 0)
    return -1;

  /* Find existing or create new */
  subif_config_t *cfg = pending_subifs;
  while (cfg) {
    if (strcmp(cfg->name, name) == 0)
      break;
    cfg = cfg->next;
  }

  if (!cfg) {
    cfg = calloc(1, sizeof(subif_config_t));
    if (!cfg)
      return -1;
    cfg->next = pending_subifs;
    pending_subifs = cfg;
  }

  strncpy(cfg->name, name, sizeof(cfg->name) - 1);
  strncpy(cfg->parent, parent, sizeof(cfg->parent) - 1);
  cfg->vlanid = vlanid;

  return 0;
}

/* Flag to track if config was loaded */
static int config_loaded = 0;

/* Load existing config from file */
static void ds_load_config_from_file(void) {
  if (config_loaded)
    return;
  config_loaded = 1;

  FILE *fp = fopen(VPP_CONFIG_FILE, "r");
  if (!fp)
    return;

  char line[512];
  char current_ifname[128] = "";
  char current_enabled[8] = "";
  char current_mtu[16] = "";
  char current_ipv4[64] = "";
  int current_ipv4_prefix = 0;
  char current_ipv6[128] = "";
  int current_ipv6_prefix = 0;

  /* Bond parsing state */
  char current_bondname[64] = "";
  char current_mode[32] = "";
  char current_lb[16] = "";
  int current_id = 0;
  char current_members[512] = "";
  int in_bond = 0;

  /* LCP parsing state */
  char current_vpp_if[128] = "";
  char current_host_if[64] = "";
  char current_netns[64] = "";
  int in_lcp = 0;

  while (fgets(line, sizeof(line), fp)) {
    char *p;

    /* Detect section */
    if (strstr(line, "<bond>"))
      in_bond = 1;
    if (strstr(line, "</bond>")) {
      if (current_bondname[0]) {
        bond_config_t *cfg = calloc(1, sizeof(bond_config_t));
        if (cfg) {
          strncpy(cfg->name, current_bondname, sizeof(cfg->name) - 1);
          strncpy(cfg->mode, current_mode, sizeof(cfg->mode) - 1);
          strncpy(cfg->lb, current_lb, sizeof(cfg->lb) - 1);
          cfg->id = current_id;
          strncpy(cfg->members, current_members, sizeof(cfg->members) - 1);
          cfg->next = pending_bonds;
          pending_bonds = cfg;
        }
      }
      current_bondname[0] = '\0';
      current_mode[0] = '\0';
      current_lb[0] = '\0';
      current_id = 0;
      current_members[0] = '\0';
      in_bond = 0;
    }

    if (strstr(line, "<lcp>"))
      in_lcp = 1;
    if (strstr(line, "</lcp>")) {
      if (current_vpp_if[0] && current_host_if[0]) {
        lcp_config_t *cfg = calloc(1, sizeof(lcp_config_t));
        if (cfg) {
          strncpy(cfg->vpp_if, current_vpp_if, sizeof(cfg->vpp_if) - 1);
          strncpy(cfg->host_if, current_host_if, sizeof(cfg->host_if) - 1);
          strncpy(cfg->netns, current_netns, sizeof(cfg->netns) - 1);
          cfg->next = pending_lcps;
          pending_lcps = cfg;
        }
      }
      current_vpp_if[0] = '\0';
      current_host_if[0] = '\0';
      current_netns[0] = '\0';
      in_lcp = 0;
    }

    /* Parse name - context sensitive */
    if ((p = strstr(line, "<name>")) != NULL) {
      char *end = strstr(p, "</name>");
      if (end) {
        p += 6;
        int len = end - p;
        if (in_bond) {
          if (len > 0 && len < 63) {
            strncpy(current_bondname, p, len);
            current_bondname[len] = '\0';
          }
        } else if (len > 0 && len < 127) {
          strncpy(current_ifname, p, len);
          current_ifname[len] = '\0';
        }
      }
    }

    /* Parse bond-specific fields */
    if (in_bond) {
      if ((p = strstr(line, "<mode>")) != NULL) {
        char *end = strstr(p, "</mode>");
        if (end) {
          p += 6;
          int len = end - p;
          if (len > 0 && len < 31) {
            strncpy(current_mode, p, len);
            current_mode[len] = '\0';
          }
        }
      }
      if ((p = strstr(line, "<load-balance>")) != NULL) {
        char *end = strstr(p, "</load-balance>");
        if (end) {
          p += 14;
          int len = end - p;
          if (len > 0 && len < 15) {
            strncpy(current_lb, p, len);
            current_lb[len] = '\0';
          }
        }
      }
      if ((p = strstr(line, "<id>")) != NULL) {
        p += 4;
        current_id = atoi(p);
      }
      if ((p = strstr(line, "<members>")) != NULL) {
        char *end = strstr(p, "</members>");
        if (end) {
          p += 9;
          int len = end - p;
          if (len > 0 && len < 511) {
            strncpy(current_members, p, len);
            current_members[len] = '\0';
          }
        }
      }
    }

    /* Parse LCP-specific fields */
    if (in_lcp) {
      if ((p = strstr(line, "<vpp-interface>")) != NULL) {
        char *end = strstr(p, "</vpp-interface>");
        if (end) {
          p += 15;
          int len = end - p;
          if (len > 0 && len < 127) {
            strncpy(current_vpp_if, p, len);
            current_vpp_if[len] = '\0';
          }
        }
      }
      if ((p = strstr(line, "<host-interface>")) != NULL) {
        char *end = strstr(p, "</host-interface>");
        if (end) {
          p += 16;
          int len = end - p;
          if (len > 0 && len < 63) {
            strncpy(current_host_if, p, len);
            current_host_if[len] = '\0';
          }
        }
      }
      if ((p = strstr(line, "<netns>")) != NULL) {
        char *end = strstr(p, "</netns>");
        if (end) {
          p += 7;
          int len = end - p;
          if (len > 0 && len < 63) {
            strncpy(current_netns, p, len);
            current_netns[len] = '\0';
          }
        }
      }
    }

    /* Parse interface enabled */
    if (!in_bond && !in_lcp && (p = strstr(line, "<enabled>")) != NULL) {
      char *end = strstr(p, "</enabled>");
      if (end) {
        p += 9;
        int len = end - p;
        if (len > 0 && len < 7) {
          strncpy(current_enabled, p, len);
          current_enabled[len] = '\0';
        }
      }
    }

    /* Parse mtu */
    if (!in_bond && !in_lcp && (p = strstr(line, "<mtu>")) != NULL) {
      char *end = strstr(p, "</mtu>");
      if (end) {
        p += 5;
        int len = end - p;
        if (len > 0 && len < 15) {
          strncpy(current_mtu, p, len);
          current_mtu[len] = '\0';
        }
      }
    }

    /* Parse ipv4 address */
    if (!in_bond && !in_lcp && (p = strstr(line, "<address>")) != NULL &&
        current_ipv4[0] == '\0') {
      char *end = strstr(p, "</address>");
      if (end) {
        p += 9;
        int len = end - p;
        if (len > 0 && len < 63) {
          strncpy(current_ipv4, p, len);
          current_ipv4[len] = '\0';
        }
      }
    }

    /* Parse prefix-length for ipv4 */
    if (!in_bond && !in_lcp && (p = strstr(line, "<prefix-length>")) != NULL &&
        current_ipv4_prefix == 0) {
      p += 15;
      current_ipv4_prefix = atoi(p);
    }

    /* End of interface - save it */
    if (strstr(line, "</interface>") != NULL && current_ifname[0] != '\0') {
      pending_config_t *cfg = calloc(1, sizeof(pending_config_t));
      if (cfg) {
        strncpy(cfg->ifname, current_ifname, sizeof(cfg->ifname) - 1);
        strncpy(cfg->enabled, current_enabled, sizeof(cfg->enabled) - 1);
        strncpy(cfg->mtu, current_mtu, sizeof(cfg->mtu) - 1);
        strncpy(cfg->ipv4_addr, current_ipv4, sizeof(cfg->ipv4_addr) - 1);
        cfg->ipv4_prefix = current_ipv4_prefix;
        strncpy(cfg->ipv6_addr, current_ipv6, sizeof(cfg->ipv6_addr) - 1);
        cfg->ipv6_prefix = current_ipv6_prefix;
        cfg->next = pending_interfaces;
        pending_interfaces = cfg;
      }

      /* Reset for next interface */
      current_ifname[0] = '\0';
      current_enabled[0] = '\0';
      current_mtu[0] = '\0';
      current_ipv4[0] = '\0';
      current_ipv4_prefix = 0;
      current_ipv6[0] = '\0';
      current_ipv6_prefix = 0;
    }
  }

  fclose(fp);
}

/* Write pending config to file on commit */
static int ds_write_config_file(void) {
  FILE *fp = fopen(VPP_CONFIG_FILE, "w");
  if (!fp) {
    fprintf(stderr, "Cannot open config file for writing: %s\n",
            VPP_CONFIG_FILE);
    return -1;
  }

  fprintf(fp, "<config>\n");

  /* Write interfaces */
  fprintf(fp, "  <interfaces xmlns=\"%s\">\n", VPP_INTERFACES_NS);
  pending_config_t *cfg = pending_interfaces;
  while (cfg) {
    fprintf(fp, "    <interface>\n");
    fprintf(fp, "      <name>%s</name>\n", cfg->ifname);
    if (cfg->enabled[0]) {
      fprintf(fp, "      <enabled>%s</enabled>\n", cfg->enabled);
    }
    if (cfg->mtu[0]) {
      fprintf(fp, "      <mtu>%s</mtu>\n", cfg->mtu);
    }
    if (cfg->ipv4_addr[0] && cfg->ipv4_prefix > 0) {
      fprintf(fp, "      <ipv4-address>\n");
      fprintf(fp, "        <address>%s</address>\n", cfg->ipv4_addr);
      fprintf(fp, "        <prefix-length>%d</prefix-length>\n",
              cfg->ipv4_prefix);
      fprintf(fp, "      </ipv4-address>\n");
    }
    if (cfg->ipv6_addr[0] && cfg->ipv6_prefix > 0) {
      fprintf(fp, "      <ipv6-address>\n");
      fprintf(fp, "        <address>%s</address>\n", cfg->ipv6_addr);
      fprintf(fp, "        <prefix-length>%d</prefix-length>\n",
              cfg->ipv6_prefix);
      fprintf(fp, "      </ipv6-address>\n");
    }
    fprintf(fp, "    </interface>\n");
    cfg = cfg->next;
  }
  fprintf(fp, "  </interfaces>\n");

  /* Write bonds */
  if (pending_bonds) {
    fprintf(fp, "  <bonds xmlns=\"%s\">\n", VPP_BONDS_NS);
    bond_config_t *bcfg = pending_bonds;
    while (bcfg) {
      fprintf(fp, "    <bond>\n");
      fprintf(fp, "      <name>%s</name>\n", bcfg->name);
      fprintf(fp, "      <id>%d</id>\n", bcfg->id);
      if (bcfg->mode[0]) {
        fprintf(fp, "      <mode>%s</mode>\n", bcfg->mode);
      }
      if (bcfg->lb[0]) {
        fprintf(fp, "      <load-balance>%s</load-balance>\n", bcfg->lb);
      }
      if (bcfg->members[0]) {
        fprintf(fp, "      <members>%s</members>\n", bcfg->members);
      }
      fprintf(fp, "    </bond>\n");
      bcfg = bcfg->next;
    }
    fprintf(fp, "  </bonds>\n");
  }

  /* Write LCPs */
  if (pending_lcps) {
    fprintf(fp, "  <lcps xmlns=\"%s\">\n", VPP_LCP_NS);
    lcp_config_t *lcfg = pending_lcps;
    while (lcfg) {
      fprintf(fp, "    <lcp>\n");
      fprintf(fp, "      <vpp-interface>%s</vpp-interface>\n", lcfg->vpp_if);
      fprintf(fp, "      <host-interface>%s</host-interface>\n", lcfg->host_if);
      if (lcfg->netns[0]) {
        fprintf(fp, "      <netns>%s</netns>\n", lcfg->netns);
      }
      fprintf(fp, "    </lcp>\n");
      lcfg = lcfg->next;
    }
    fprintf(fp, "  </lcps>\n");
  }

  /* Write Sub-interfaces */
  if (pending_subifs) {
    fprintf(fp, "  <subinterfaces xmlns=\"%s\">\n", VPP_INTERFACES_NS);
    subif_config_t *scfg = pending_subifs;
    while (scfg) {
      fprintf(fp, "    <subinterface>\n");
      fprintf(fp, "      <name>%s</name>\n", scfg->name);
      fprintf(fp, "      <parent>%s</parent>\n", scfg->parent);
      fprintf(fp, "      <vlan-id>%d</vlan-id>\n", scfg->vlanid);
      fprintf(fp, "    </subinterface>\n");
      scfg = scfg->next;
    }
    fprintf(fp, "  </subinterfaces>\n");
  }

  fprintf(fp, "</config>\n");

  fclose(fp);
  return 0;
}

/*=============================================================
 * EXPAND/COMPLETION CALLBACKS - For tab completion
 *=============================================================*/

/* Expand callback for interface names - provides tab completion */
int cli_expand_interfaces(void *h, char *name, cvec *cvv, cvec *argv,
                          cvec *commands, cvec *helptexts) {
  (void)h;
  (void)name;
  (void)cvv;
  (void)argv;

  char output[8192];

  if (vpp_exec("show interface", output, sizeof(output)) != 0) {
    return 0;
  }

  /* Parse interface names from output */
  char *saveptr = NULL;
  char *line = strtok_r(output, "\r\n", &saveptr);

  /* Skip header line */
  if (line) {
    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  while (line) {
    char ifname[64];
    int idx;
    char state[16];

    /* Parse: name idx state ... */
    if (sscanf(line, "%63s %d %15s", ifname, &idx, state) >= 2) {
      /* Skip header and invalid entries */
      if (ifname[0] >= 'A' && ifname[0] <= 'z' && strcmp(ifname, "Name") != 0) {
        cg_var *cv = cvec_add(commands, CGV_STRING);
        if (cv)
          cv_string_set(cv, ifname);
        cv = cvec_add(helptexts, CGV_STRING);
        if (cv)
          cv_string_set(cv, state);
      }
    }
    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  return 0;
}

/* Expand ethernet (physical) interfaces only */
int cli_expand_ethernet(void *h, char *name, cvec *cvv, cvec *argv,
                        cvec *commands, cvec *helptexts) {
  (void)h;
  (void)name;
  (void)cvv;
  (void)argv;

  char output[8192];

  if (vpp_exec("show interface", output, sizeof(output)) != 0) {
    return 0;
  }

  char *saveptr = NULL;
  char *line = strtok_r(output, "\r\n", &saveptr);

  /* Skip header */
  if (line)
    line = strtok_r(NULL, "\r\n", &saveptr);

  while (line) {
    char ifname[64];
    int idx;
    char state[16];

    if (sscanf(line, "%63s %d %15s", ifname, &idx, state) >= 2) {
      /* Only include physical ethernet interfaces (not Bond, loop, local0, tap)
       */
      if ((strstr(ifname, "Ethernet") || strstr(ifname, "ethernet")) &&
          !strstr(ifname, "Bond") && !strchr(ifname, '.')) {
        cg_var *cv = cvec_add(commands, CGV_STRING);
        if (cv)
          cv_string_set(cv, ifname);
        cv = cvec_add(helptexts, CGV_STRING);
        if (cv)
          cv_string_set(cv, state);
      }
    }
    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  return 0;
}

/* Expand bond interfaces and suggest new bond names */
int cli_expand_bonds(void *h, char *name, cvec *cvv, cvec *argv, cvec *commands,
                     cvec *helptexts) {
  (void)h;
  (void)name;
  (void)cvv;
  (void)argv;

  char output[8192];
  int max_bond_id = -1;

  if (vpp_exec("show interface", output, sizeof(output)) == 0) {
    char *saveptr = NULL;
    char *line = strtok_r(output, "\r\n", &saveptr);

    /* Skip header */
    if (line)
      line = strtok_r(NULL, "\r\n", &saveptr);

    while (line) {
      char ifname[64];
      int idx;
      char state[16];

      if (sscanf(line, "%63s %d %15s", ifname, &idx, state) >= 2) {
        /* Include existing bond interfaces */
        if (strncmp(ifname, "BondEthernet", 12) == 0 && !strchr(ifname, '.')) {
          cg_var *cv = cvec_add(commands, CGV_STRING);
          if (cv)
            cv_string_set(cv, ifname);
          cv = cvec_add(helptexts, CGV_STRING);
          if (cv)
            cv_string_set(cv, state);

          /* Track max bond ID for suggestion */
          int bond_id = atoi(ifname + 12);
          if (bond_id > max_bond_id)
            max_bond_id = bond_id;
        }
      }
      line = strtok_r(NULL, "\r\n", &saveptr);
    }
  }

  /* Suggest BondEthernet as template for new bond */
  cg_var *cv = cvec_add(commands, CGV_STRING);
  if (cv)
    cv_string_set(cv, "BondEthernet");
  cv = cvec_add(helptexts, CGV_STRING);
  if (cv)
    cv_string_set(cv, "(add number, e.g. BondEthernet0)");

  return 0;
}

/* Expand sub-interfaces (vlan) */
int cli_expand_subifs(void *h, char *name, cvec *cvv, cvec *argv,
                      cvec *commands, cvec *helptexts) {
  (void)h;
  (void)name;
  (void)cvv;
  (void)argv;

  char output[8192];

  if (vpp_exec("show interface", output, sizeof(output)) != 0) {
    return 0;
  }

  char *saveptr = NULL;
  char *line = strtok_r(output, "\r\n", &saveptr);

  /* Skip header */
  if (line)
    line = strtok_r(NULL, "\r\n", &saveptr);

  while (line) {
    char ifname[64];
    int idx;
    char state[16];

    if (sscanf(line, "%63s %d %15s", ifname, &idx, state) >= 2) {
      /* Include sub-interfaces (contain '.') */
      if (strchr(ifname, '.')) {
        cg_var *cv = cvec_add(commands, CGV_STRING);
        if (cv)
          cv_string_set(cv, ifname);
        cv = cvec_add(helptexts, CGV_STRING);
        if (cv)
          cv_string_set(cv, state);
      }
    }
    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  /* Also list parent interfaces that can have sub-interfaces */
  if (vpp_exec("show interface", output, sizeof(output)) == 0) {
    char *saveptr2 = NULL;
    char *line2 = strtok_r(output, "\r\n", &saveptr2);

    if (line2)
      line2 = strtok_r(NULL, "\r\n", &saveptr2);

    while (line2) {
      char ifname[64];
      int idx;
      char state[16];

      if (sscanf(line2, "%63s %d %15s", ifname, &idx, state) >= 2) {
        /* Parent candidates: ethernet or bond interfaces without '.' */
        if (!strchr(ifname, '.') && strcmp(ifname, "local0") != 0 &&
            strncmp(ifname, "tap", 3) != 0 && strncmp(ifname, "loop", 4) != 0) {
          char suggestion[80];
          snprintf(suggestion, sizeof(suggestion), "%s.", ifname);
          cg_var *cv = cvec_add(commands, CGV_STRING);
          if (cv)
            cv_string_set(cv, suggestion);
          cv = cvec_add(helptexts, CGV_STRING);
          if (cv)
            cv_string_set(cv, "(add VLAN ID)");
        }
      }
      line2 = strtok_r(NULL, "\r\n", &saveptr2);
    }
  }

  return 0;
}

/*=============================================================
 * INTERFACE CONFIG - Stores interface context then switches mode
 *=============================================================*/

/* Store interface name for subsequent commands
 * If interface format is "parent.vlan" and doesn't exist, auto-create it
 * Handles case normalization for BondEthernet names
 */
int cli_interface_select(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  /* Try both ifname and bondname */
  cg_var *cv = cvec_find(cvv, "ifname");
  if (!cv)
    cv = cvec_find(cvv, "bondname");
  if (!cv) {
    fprintf(stderr, "Error: Interface name required\n");
    return -1;
  }

  const char *ifname = cv_string_get(cv);
  if (!ifname) {
    fprintf(stderr, "Error: Invalid interface name\n");
    return -1;
  }

  /* Normalize BondEthernet case (bondethernet -> BondEthernet) */
  char normalized[128];
  if (strncasecmp(ifname, "bondethernet", 12) == 0) {
    snprintf(normalized, sizeof(normalized), "BondEthernet%s", ifname + 12);
    strncpy(current_interface, normalized, sizeof(current_interface) - 1);
  } else {
    strncpy(current_interface, ifname, sizeof(current_interface) - 1);
  }

  /* Check if interface contains '.' (potential VLAN sub-interface) */
  char *dot = strchr(ifname, '.');
  if (dot) {
    /* Extract parent and VLAN ID */
    char parent[64];
    int vlanid;

    size_t parent_len = dot - ifname;
    if (parent_len >= sizeof(parent))
      parent_len = sizeof(parent) - 1;

    strncpy(parent, ifname, parent_len);
    parent[parent_len] = '\0';

    vlanid = atoi(dot + 1);

    if (vlanid > 0 && vlanid <= 4094) {
      /* Check if sub-interface exists by trying to get its info */
      char cmd[256];
      char output[2048];

      snprintf(cmd, sizeof(cmd), "show interface %s", ifname);
      int ret = vpp_exec(cmd, output, sizeof(output));

      /* If interface doesn't exist, create it */
      if (ret != 0 || strstr(output, "unknown interface") ||
          strlen(output) < 10) {
        snprintf(cmd, sizeof(cmd),
                 "create sub-interfaces %s %d dot1q %d exact-match", parent,
                 vlanid, vlanid);

        if (vpp_exec(cmd, output, sizeof(output)) == 0) {
          fprintf(stdout, "Created sub-interface: %s\n", current_interface);
          /* Save sub-interface config for persistence */
          ds_save_subif_config(current_interface, parent, vlanid);
          CONFIG_CHANGED();
        } else {
          fprintf(stderr, "Failed to create sub-interface: %s\n", output);
          return -1;
        }
      }
    }
  }

  return 0;
}

/* Create VLAN sub-interface */
int cli_create_subinterface(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  cg_var *cv_parent = cvec_find(cvv, "parent");
  cg_var *cv_vlan = cvec_find(cvv, "vlan");

  if (!cv_parent || !cv_vlan) {
    fprintf(stderr, "Error: Parent interface and VLAN ID required\n");
    return -1;
  }

  const char *parent = cv_string_get(cv_parent);
  int vlanid = cv_int32_get(cv_vlan);

  if (!parent || vlanid <= 0 || vlanid > 4094) {
    fprintf(stderr, "Error: Invalid parent or VLAN ID\n");
    return -1;
  }

  char cmd[256];
  char output[2048];

  /* Create sub-interface name */
  snprintf(current_interface, sizeof(current_interface), "%s.%d", parent,
           vlanid);

  /* Check if already exists */
  snprintf(cmd, sizeof(cmd), "show interface %s", current_interface);
  int ret = vpp_exec(cmd, output, sizeof(output));

  if (ret == 0 && strlen(output) > 10 && !strstr(output, "unknown interface")) {
    fprintf(stdout, "Sub-interface %s already exists\n", current_interface);
    return 0;
  }

  /* Create the sub-interface */
  snprintf(cmd, sizeof(cmd), "create sub-interfaces %s %d dot1q %d exact-match",
           parent, vlanid, vlanid);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "Created sub-interface: %s\n", current_interface);
    /* Save for persistence */
    ds_save_subif_config(current_interface, parent, vlanid);
    CONFIG_CHANGED();
    return 0;
  } else {
    fprintf(stderr, "Failed to create sub-interface: %s\n", output);
    return -1;
  }
}

/* Create VLAN sub-interface (TNSR-style: interface subif <parent> <vlanid>) */
int cli_create_subif_simple(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  cg_var *cv_parent = cvec_find(cvv, "parent");
  cg_var *cv_vlan = cvec_find(cvv, "vlanid");

  if (!cv_parent || !cv_vlan) {
    fprintf(stderr, "Error: Parent interface and VLAN ID required\n");
    return -1;
  }

  const char *parent = cv_string_get(cv_parent);
  int vlanid = cv_int32_get(cv_vlan);

  if (!parent || vlanid <= 0 || vlanid > 4094) {
    fprintf(stderr, "Error: Invalid parent or VLAN ID\n");
    return -1;
  }

  char cmd[256];
  char output[2048];

  /* Create sub-interface name */
  snprintf(current_interface, sizeof(current_interface), "%s.%d", parent,
           vlanid);

  /* Check if already exists */
  snprintf(cmd, sizeof(cmd), "show interface %s", current_interface);
  int ret = vpp_exec(cmd, output, sizeof(output));

  /* VPP returns "unknown input" or "unknown interface" for non-existent
   * interfaces */
  if (ret == 0 && strlen(output) > 10 && !strstr(output, "unknown")) {
    fprintf(stdout, "Sub-interface %s already exists\n", current_interface);
    return 0;
  }

  /* Create the sub-interface */
  snprintf(cmd, sizeof(cmd), "create sub-interfaces %s %d dot1q %d exact-match",
           parent, vlanid, vlanid);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "Created sub-interface: %s\n", current_interface);
    /* Save for persistence */
    ds_save_subif_config(current_interface, parent, vlanid);
    CONFIG_CHANGED();
    return 0;
  } else {
    fprintf(stderr, "Failed to create sub-interface: %s\n", output);
    return -1;
  }
}

/* Create VLAN sub-interface by full name (e.g., BondEthernet0.100) */
int cli_create_subif_byname(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  cg_var *cv = cvec_find(cvv, "subifname");
  if (!cv) {
    fprintf(stderr,
            "Error: Sub-interface name required (e.g., BondEthernet0.100)\n");
    return -1;
  }

  const char *subifname = cv_string_get(cv);
  if (!subifname) {
    fprintf(stderr, "Error: Invalid sub-interface name\n");
    return -1;
  }

  /* Parse parent and vlan from name */
  char *dot = strchr(subifname, '.');
  if (!dot) {
    fprintf(stderr, "Error: Invalid format. Use: parent.vlanid (e.g., "
                    "BondEthernet0.100)\n");
    return -1;
  }

  char parent[64];
  size_t parent_len = dot - subifname;
  if (parent_len >= sizeof(parent))
    parent_len = sizeof(parent) - 1;
  strncpy(parent, subifname, parent_len);
  parent[parent_len] = '\0';

  int vlanid = atoi(dot + 1);
  if (vlanid <= 0 || vlanid > 4094) {
    fprintf(stderr, "Error: VLAN ID must be 1-4094\n");
    return -1;
  }

  /* Set current interface */
  strncpy(current_interface, subifname, sizeof(current_interface) - 1);

  /* Check if already exists */
  char cmd[256];
  char output[2048];
  snprintf(cmd, sizeof(cmd), "show interface %s", subifname);
  int ret = vpp_exec(cmd, output, sizeof(output));

  if (ret == 0 && strlen(output) > 10 && !strstr(output, "unknown")) {
    fprintf(stdout, "Sub-interface %s already exists\n", subifname);
    return 0;
  }

  /* Create the sub-interface */
  snprintf(cmd, sizeof(cmd), "create sub-interfaces %s %d dot1q %d exact-match",
           parent, vlanid, vlanid);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "Created sub-interface: %s\n", current_interface);
    ds_save_subif_config(current_interface, parent, vlanid);
    CONFIG_CHANGED();
    return 0;
  } else {
    fprintf(stderr, "Failed to create sub-interface: %s\n", output);
    return -1;
  }
}

/* Create bond with explicit name (e.g., BondEthernet0) */
int cli_create_bond_named(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;

  cg_var *cv_name = cvec_find(cvv, "bondname");

  if (!cv_name) {
    fprintf(stderr, "Error: Bond name required (e.g., BondEthernet0)\n");
    return -1;
  }

  const char *bondname = cv_string_get(cv_name);
  if (!bondname) {
    fprintf(stderr, "Error: Invalid bond name\n");
    return -1;
  }

  /* Extract bond ID from name (BondEthernetN) */
  int bondid = 0;
  if (strncasecmp(bondname, "BondEthernet", 12) == 0) {
    bondid = atoi(bondname + 12);
  } else if (strncasecmp(bondname, "bondethernet", 12) == 0) {
    bondid = atoi(bondname + 12);
  } else {
    fprintf(stderr,
            "Error: Bond name must be BondEthernetN (e.g., BondEthernet0)\n");
    return -1;
  }

  /* Get mode and lb from callback arguments (argv) */
  const char *mode = "lacp";
  const char *lb = "l2";
  if (argv && cvec_len(argv) > 0) {
    cg_var *cv_mode = cvec_i(argv, 0);
    if (cv_mode)
      mode = cv_string_get(cv_mode);
  }
  if (argv && cvec_len(argv) > 1) {
    cg_var *cv_lb = cvec_i(argv, 1);
    if (cv_lb)
      lb = cv_string_get(cv_lb);
  }

  char cmd[256];
  char output[1024];

  /* Check if bond already exists */
  char check_name[64];
  snprintf(check_name, sizeof(check_name), "BondEthernet%d", bondid);
  snprintf(cmd, sizeof(cmd), "show interface %s", check_name);
  int ret = vpp_exec(cmd, output, sizeof(output));

  if (ret == 0 && strlen(output) > 10 && !strstr(output, "unknown")) {
    /* Bond exists, just select it */
    strncpy(current_interface, check_name, sizeof(current_interface) - 1);
    fprintf(stdout, "Selected existing bond: %s\n", current_interface);
    return 0;
  }

  /* Create new bond */
  if (argv && cvec_len(argv) > 1) {
    snprintf(cmd, sizeof(cmd), "create bond mode %s id %d load-balance %s",
             mode, bondid, lb);
  } else {
    snprintf(cmd, sizeof(cmd), "create bond mode %s id %d", mode, bondid);
  }

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    strncpy(current_interface, check_name, sizeof(current_interface) - 1);
    fprintf(stdout, "Created: %s (Mode: %s, Load-Balance: %s)\n",
            current_interface, mode, lb);
    ds_save_bond_config(current_interface, mode, lb, bondid);
    CONFIG_CHANGED();
    return 0;
  }

  fprintf(stderr, "Failed to create bond: %s\n", output);
  return -1;
}

/* Create bond and store name */
int cli_create_bond(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  /* Get mode from callback argument (argv) */
  const char *mode = "lacp";
  if (argv && cvec_len(argv) > 0) {
    cg_var *cv_mode = cvec_i(argv, 0);
    if (cv_mode)
      mode = cv_string_get(cv_mode);
  }

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "create bond mode %s", mode);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    char *newline = strchr(output, '\n');
    if (newline)
      *newline = '\0';

    strncpy(current_interface, output, sizeof(current_interface) - 1);
    fprintf(stdout, "Created: %s (Mode: %s, Load-Balance: l2)\n",
            current_interface, mode);
    return 0;
  }

  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Create bond with load-balance option */
int cli_create_bond_lb(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  cg_var *cv_lb = cvec_find(cvv, "lb");

  /* Get mode from callback argument (argv) */
  const char *mode = "lacp";
  if (argv && cvec_len(argv) > 0) {
    cg_var *cv_mode = cvec_i(argv, 0);
    if (cv_mode)
      mode = cv_string_get(cv_mode);
  }
  const char *lb = cv_lb ? cv_string_get(cv_lb) : "l2";

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "create bond mode %s load-balance %s", mode, lb);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    char *newline = strchr(output, '\n');
    if (newline)
      *newline = '\0';

    strncpy(current_interface, output, sizeof(current_interface) - 1);
    fprintf(stdout, "Created: %s (Mode: %s, Load-Balance: %s)\n",
            current_interface, mode, lb);
    return 0;
  }

  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Create bond with specific ID */
int cli_create_bond_id(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  cg_var *cv_id = cvec_find(cvv, "bondid");

  int bondid = cv_id ? cv_int32_get(cv_id) : 0;

  /* Get mode from callback argument (argv) */
  const char *mode = "lacp";
  if (argv && cvec_len(argv) > 0) {
    cg_var *cv_mode = cvec_i(argv, 0);
    if (cv_mode)
      mode = cv_string_get(cv_mode);
  }

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "create bond mode %s id %d", mode, bondid);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    char *newline = strchr(output, '\n');
    if (newline)
      *newline = '\0';

    strncpy(current_interface, output, sizeof(current_interface) - 1);
    fprintf(stdout, "Created: %s (Mode: %s, Load-Balance: l2)\n",
            current_interface, mode);
    return 0;
  }

  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Create bond with specific ID and load-balance */
int cli_create_bond_full(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;

  cg_var *cv_id = cvec_find(cvv, "bondid");
  cg_var *cv_lb = cvec_find(cvv, "lb");

  int bondid = cv_id ? cv_int32_get(cv_id) : 0;

  /* Get mode from callback argument (argv) */
  const char *mode = "lacp";
  if (argv && cvec_len(argv) > 0) {
    cg_var *cv_mode = cvec_i(argv, 0);
    if (cv_mode)
      mode = cv_string_get(cv_mode);
  }
  const char *lb = cv_lb ? cv_string_get(cv_lb) : "l2";

  char cmd[256];
  char output[1024];
  char bondname[64];

  /* Construct expected bond name */
  snprintf(bondname, sizeof(bondname), "BondEthernet%d", bondid);

  snprintf(cmd, sizeof(cmd), "create bond mode %s id %d load-balance %s", mode,
           bondid, lb);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    /* Use constructed bond name instead of output (may be empty if already
     * exists) */
    strncpy(current_interface, bondname, sizeof(current_interface) - 1);
    fprintf(stdout, "Created: %s (Mode: %s, Load-Balance: %s)\n",
            current_interface, mode, lb);
    /* Save bond config */
    ds_save_bond_config(bondname, mode, lb, bondid);
    CONFIG_CHANGED();
    return 0;
  }

  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Create loopback and store name */
int cli_create_loopback(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  char output[1024];

  if (vpp_exec("create loopback interface", output, sizeof(output)) == 0) {
    char *newline = strchr(output, '\n');
    if (newline)
      *newline = '\0';

    strncpy(current_interface, output, sizeof(current_interface) - 1);
    fprintf(stdout, "Created: %s\n", current_interface);
    return 0;
  }

  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Create VLAN sub-interface */
int cli_vlan_create(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  cg_var *cv_vlan = cvec_find(cvv, "vlanid");
  cg_var *cv_parent = cvec_find(cvv, "parent");

  if (!cv_vlan || !cv_parent) {
    fprintf(stderr, "Error: VLAN ID and parent interface required\n");
    return -1;
  }

  int vlanid = cv_int32_get(cv_vlan);
  const char *parent = cv_string_get(cv_parent);

  if (!parent) {
    fprintf(stderr, "Error: Invalid parent interface\n");
    return -1;
  }

  char cmd[256];
  char output[1024];

  /* Create sub-interface with dot1q encapsulation */
  snprintf(cmd, sizeof(cmd), "create sub-interfaces %s %d dot1q %d exact-match",
           parent, vlanid, vlanid);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    /* Set current interface to the new sub-interface */
    snprintf(current_interface, sizeof(current_interface), "%s.%d", parent,
             vlanid);
    fprintf(stdout, "Created VLAN: %s\n", current_interface);
    return 0;
  }

  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/*=============================================================
 * INTERFACE MODE COMMANDS (work on current_interface)
 *=============================================================*/

int cli_if_description(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0) {
    fprintf(stderr, "Error: No interface selected\n");
    return -1;
  }

  cg_var *cv = cvec_find(cvv, "desc");
  if (!cv)
    return -1;

  fprintf(stdout, "[%s] Description: %s\n", current_interface,
          cv_string_get(cv));
  return 0;
}

int cli_if_mtu(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)argv;

  if (strlen(current_interface) == 0)
    return -1;

  cg_var *cv = cvec_find(cvv, "mtu");
  if (!cv)
    return -1;

  int mtu = cv_int32_get(cv);
  char cmd[256];
  char output[1024];
  char mtu_str[16];

  snprintf(cmd, sizeof(cmd), "set interface mtu %d %s", mtu, current_interface);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] MTU: %d\n", current_interface, mtu);
    /* Save MTU to config */
    snprintf(mtu_str, sizeof(mtu_str), "%d", mtu);
    ds_save_interface(h, current_interface, NULL, mtu_str, NULL, 0, NULL, 0);
    CONFIG_CHANGED();
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

int cli_if_no_shutdown(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)cvv;
  (void)argv;

  if (strlen(current_interface) == 0)
    return -1;

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "set interface state %s up", current_interface);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] Enabled\n", current_interface);
    /* Save to datastore */
    ds_save_interface(h, current_interface, "true", NULL, NULL, 0, NULL, 0);
    CONFIG_CHANGED();
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

int cli_if_shutdown(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  if (strlen(current_interface) == 0)
    return -1;

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "set interface state %s down", current_interface);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] Disabled\n", current_interface);
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

int cli_if_ip_address(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0) {
    fprintf(stderr, "Error: No interface selected\n");
    return -1;
  }

  cg_var *cv_ip = cvec_find(cvv, "ip");
  cg_var *cv_prefix = cvec_find(cvv, "prefix");

  if (!cv_ip || !cv_prefix) {
    fprintf(stderr, "Error: IP address and prefix required\n");
    return -1;
  }

  /* Get IP as string - handle different cv types */
  char ip_str[64];
  if (cv_type_get(cv_ip) == CGV_IPV4ADDR) {
    struct in_addr *addr = cv_ipv4addr_get(cv_ip);
    if (addr) {
      inet_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
    } else {
      fprintf(stderr, "Error: Invalid IPv4 address\n");
      return -1;
    }
  } else {
    const char *ip = cv_string_get(cv_ip);
    if (!ip) {
      fprintf(stderr, "Error: Could not get IP address\n");
      return -1;
    }
    strncpy(ip_str, ip, sizeof(ip_str) - 1);
  }

  int prefix = cv_int32_get(cv_prefix);

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "set interface ip address %s %s/%d",
           current_interface, ip_str, prefix);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] IPv4: %s/%d\n", current_interface, ip_str, prefix);
    /* Save to datastore */
    ds_save_interface(h, current_interface, NULL, NULL, ip_str, prefix, NULL,
                      0);
    CONFIG_CHANGED();
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* IPv6 address configuration */
int cli_if_ipv6_address(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)argv;

  if (strlen(current_interface) == 0) {
    fprintf(stderr, "Error: No interface selected\n");
    return -1;
  }

  cg_var *cv_ip = cvec_find(cvv, "ip");
  cg_var *cv_prefix = cvec_find(cvv, "prefix");

  if (!cv_ip || !cv_prefix) {
    fprintf(stderr, "Error: IPv6 address and prefix required\n");
    return -1;
  }

  /* Get IPv6 as string - handle different cv types */
  char ip_str[128];
  if (cv_type_get(cv_ip) == CGV_IPV6ADDR) {
    struct in6_addr *addr = cv_ipv6addr_get(cv_ip);
    if (addr) {
      inet_ntop(AF_INET6, addr, ip_str, sizeof(ip_str));
    } else {
      fprintf(stderr, "Error: Invalid IPv6 address\n");
      return -1;
    }
  } else {
    const char *ip = cv_string_get(cv_ip);
    if (!ip) {
      fprintf(stderr, "Error: Could not get IPv6 address\n");
      return -1;
    }
    strncpy(ip_str, ip, sizeof(ip_str) - 1);
  }

  int prefix = cv_int32_get(cv_prefix);

  char cmd[256];
  char output[1024];

  /* VPP uses same command for both IPv4 and IPv6 */
  snprintf(cmd, sizeof(cmd), "set interface ip address %s %s/%d",
           current_interface, ip_str, prefix);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] IPv6: %s/%d\n", current_interface, ip_str, prefix);
    /* Save IPv6 to config */
    ds_save_interface(h, current_interface, NULL, NULL, NULL, 0, ip_str,
                      prefix);
    CONFIG_CHANGED();
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

int cli_if_channel_group(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0)
    return -1;

  cg_var *cv = cvec_find(cvv, "bondid");
  if (!cv)
    return -1;

  int bondid = cv_int32_get(cv);
  char cmd[256];
  char output[1024];
  char bondname[64];

  snprintf(bondname, sizeof(bondname), "BondEthernet%d", bondid);
  snprintf(cmd, sizeof(cmd), "bond add %s %s", bondname, current_interface);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] Added to %s\n", current_interface, bondname);
    /* Save member to bond config */
    ds_add_bond_member(bondname, current_interface);
    CONFIG_CHANGED();
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Create VLAN sub-interface from current interface */
int cli_if_vlan(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0) {
    fprintf(stderr, "Error: No interface selected\n");
    return -1;
  }

  cg_var *cv = cvec_find(cvv, "vlan");
  if (!cv) {
    fprintf(stderr, "Error: VLAN ID required\n");
    return -1;
  }

  int vlanid = cv_int32_get(cv);
  if (vlanid <= 0 || vlanid > 4094) {
    fprintf(stderr, "Error: VLAN ID must be 1-4094\n");
    return -1;
  }

  char parent[128];
  strncpy(parent, current_interface, sizeof(parent) - 1);

  char subif_name[256];
  snprintf(subif_name, sizeof(subif_name), "%s.%d", parent, vlanid);

  char cmd[256];
  char output[2048];

  /* Check if already exists */
  snprintf(cmd, sizeof(cmd), "show interface %s", subif_name);
  int ret = vpp_exec(cmd, output, sizeof(output));

  if (ret == 0 && strlen(output) > 10 && !strstr(output, "unknown interface")) {
    fprintf(stdout, "Sub-interface %s already exists, selecting it\n",
            subif_name);
    strncpy(current_interface, subif_name, sizeof(current_interface) - 1);
    return 0;
  }

  /* Create the sub-interface */
  snprintf(cmd, sizeof(cmd), "create sub-interfaces %s %d dot1q %d exact-match",
           parent, vlanid, vlanid);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "Created sub-interface: %s\n", subif_name);
    /* Switch context to new sub-interface */
    strncpy(current_interface, subif_name, sizeof(current_interface) - 1);
    /* Save for persistence */
    ds_save_subif_config(subif_name, parent, vlanid);
    CONFIG_CHANGED();
    return 0;
  } else {
    fprintf(stderr, "Failed to create sub-interface: %s\n", output);
    return -1;
  }
}

int cli_if_encapsulation(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0)
    return -1;

  cg_var *cv = cvec_find(cvv, "vlan");
  if (!cv)
    return -1;

  int vlan = cv_int32_get(cv);
  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "create sub-interfaces %s %d dot1q %d exact-match",
           current_interface, vlan, vlan);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "Created: %s.%d\n", current_interface, vlan);
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

int cli_if_lcp(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0)
    return -1;

  cg_var *cv = cvec_find(cvv, "hostif");
  if (!cv)
    return -1;

  const char *hostif = cv_string_get(cv);
  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s", current_interface,
           hostif);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] LCP -> %s\n", current_interface, hostif);
    /* Save LCP config */
    ds_save_lcp_config(current_interface, hostif, NULL);
    CONFIG_CHANGED();
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* LCP with netns option */
int cli_if_lcp_netns(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0)
    return -1;

  cg_var *cv_host = cvec_find(cvv, "hostif");
  cg_var *cv_netns = cvec_find(cvv, "netns");

  if (!cv_host || !cv_netns)
    return -1;

  const char *hostif = cv_string_get(cv_host);
  const char *netns = cv_string_get(cv_netns);

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "lcp create %s host-if %s netns %s",
           current_interface, hostif, netns);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] LCP -> %s (netns: %s)\n", current_interface, hostif,
            netns);
    /* Save LCP config with netns */
    ds_save_lcp_config(current_interface, hostif, netns);
    CONFIG_CHANGED();
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Remove LCP from interface */
int cli_if_no_lcp(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  if (strlen(current_interface) == 0)
    return -1;

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "lcp delete %s", current_interface);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] LCP removed\n", current_interface);
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Clear interface context when exiting interface mode */
int cli_if_exit(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  current_interface[0] = '\0';
  return 0;
}

/* Commit command - write config to file */
/* Helper: Clear all pending lists */
static void ds_clear_pending(void) {
  pending_config_t *cfg = pending_interfaces;
  while (cfg) {
    pending_config_t *next = cfg->next;
    free(cfg);
    cfg = next;
  }
  pending_interfaces = NULL;

  bond_config_t *bcfg = pending_bonds;
  while (bcfg) {
    bond_config_t *next = bcfg->next;
    free(bcfg);
    bcfg = next;
  }
  pending_bonds = NULL;

  lcp_config_t *lcfg = pending_lcps;
  while (lcfg) {
    lcp_config_t *next = lcfg->next;
    free(lcfg);
    lcfg = next;
  }
  pending_lcps = NULL;

  subif_config_t *scfg = pending_subifs;
  while (scfg) {
    subif_config_t *next = scfg->next;
    free(scfg);
    scfg = next;
  }
  pending_subifs = NULL;
}

/* Helper: Sync bonds from VPP using API */
static void ds_sync_bonds_from_vpp(void) {
  /* Connect to VPP API if not connected */
  if (!vpp_api_is_connected()) {
    if (vpp_api_connect("clixon-cli") != 0) {
      /* Fall back to CLI parsing if API connection fails */
      char output[8192];
      if (vpp_exec("show bond", output, sizeof(output)) != 0)
        return;

      char *saveptr = NULL;
      char *line = strtok_r(output, "\r\n", &saveptr);
      if (line)
        line = strtok_r(NULL, "\r\n", &saveptr);

      while (line) {
        char ifname[64];
        int sw_if_idx;
        char mode[32];
        char lb[16];
        int active, members;

        if (sscanf(line, "%63s %d %31s %15s %d %d", ifname, &sw_if_idx, mode,
                   lb, &active, &members) >= 4) {
          if (strncmp(ifname, "BondEthernet", 12) == 0) {
            bond_config_t *bcfg = calloc(1, sizeof(bond_config_t));
            if (bcfg) {
              strncpy(bcfg->name, ifname, sizeof(bcfg->name) - 1);
              strncpy(bcfg->mode, mode, sizeof(bcfg->mode) - 1);
              strncpy(bcfg->lb, lb, sizeof(bcfg->lb) - 1);
              bcfg->id = atoi(ifname + 12);
              bcfg->next = pending_bonds;
              pending_bonds = bcfg;
            }
          }
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
      }
      return;
    }
  }

  /* Use VPP API to get bonds */
  vpp_bond_info_t bonds[32];
  int count = vpp_api_get_bonds(bonds, 32);

  for (int i = 0; i < count; i++) {
    bond_config_t *bcfg = calloc(1, sizeof(bond_config_t));
    if (bcfg) {
      strncpy(bcfg->name, bonds[i].name, sizeof(bcfg->name) - 1);
      strncpy(bcfg->mode, vpp_bond_mode_str(bonds[i].mode),
              sizeof(bcfg->mode) - 1);
      strncpy(bcfg->lb, vpp_lb_mode_str(bonds[i].lb), sizeof(bcfg->lb) - 1);
      bcfg->id = bonds[i].id;
      bcfg->next = pending_bonds;
      pending_bonds = bcfg;
    }
  }
}

/* Helper: Sync LCPs from VPP using API */
static void ds_sync_lcps_from_vpp(void) {
  /* Try VPP API first */
  if (vpp_api_is_connected()) {
    vpp_lcp_info_t lcps[64];
    int count = vpp_api_get_lcps(lcps, 64);

    if (count > 0) {
      for (int i = 0; i < count; i++) {
        lcp_config_t *lcfg = calloc(1, sizeof(lcp_config_t));
        if (lcfg) {
          strncpy(lcfg->vpp_if, lcps[i].vpp_if, sizeof(lcfg->vpp_if) - 1);
          strncpy(lcfg->host_if, lcps[i].host_if, sizeof(lcfg->host_if) - 1);
          if (lcps[i].netns[0])
            strncpy(lcfg->netns, lcps[i].netns, sizeof(lcfg->netns) - 1);
          lcfg->next = pending_lcps;
          pending_lcps = lcfg;
        }
      }
      return;
    }
    /* If count is 0 or negative, fall through to CLI parsing */
  }

  /* Fall back to CLI parsing */
  char output[8192];
  if (vpp_exec("show lcp", output, sizeof(output)) != 0)
    return;

  char *saveptr = NULL;
  char *line = strtok_r(output, "\r\n", &saveptr);

  if (line)
    line = strtok_r(NULL, "\r\n", &saveptr);

  while (line) {
    char itf_pair[32], host_if[64], vpp_if[128], netns[64];
    int phy_sw, host_sw;

    if (sscanf(line, "%31s %63s %d %127s %d %63s", itf_pair, host_if, &phy_sw,
               vpp_if, &host_sw, netns) >= 5) {
      if (strcmp(itf_pair, "itf-pair") != 0) {
        lcp_config_t *lcfg = calloc(1, sizeof(lcp_config_t));
        if (lcfg) {
          strncpy(lcfg->vpp_if, vpp_if, sizeof(lcfg->vpp_if) - 1);
          strncpy(lcfg->host_if, host_if, sizeof(lcfg->host_if) - 1);
          if (strcmp(netns, "-") != 0)
            strncpy(lcfg->netns, netns, sizeof(lcfg->netns) - 1);
          lcfg->next = pending_lcps;
          pending_lcps = lcfg;
        }
      }
    }
    line = strtok_r(NULL, "\r\n", &saveptr);
  }
}

/* Helper: Sync interfaces from VPP (admin state, MTU, IP addresses) */
static void ds_sync_interfaces_from_vpp(void) {
  char output[16384];

  /* Get interface list with state */
  if (vpp_exec("show interface", output, sizeof(output)) != 0)
    return;

  char *saveptr = NULL;
  char *line = strtok_r(output, "\r\n", &saveptr);

  /* Skip header */
  if (line)
    line = strtok_r(NULL, "\r\n", &saveptr);

  while (line) {
    char ifname[128];
    int idx;
    char state[16];

    if (sscanf(line, "%127s %d %15s", ifname, &idx, state) >= 2) {
      /* Skip local0, loop, tap interfaces without LCP */
      if (strcmp(ifname, "local0") != 0 && strncmp(ifname, "loop", 4) != 0 &&
          (strstr(ifname, "Ethernet") || strstr(ifname, "Bond"))) {

        pending_config_t *cfg = calloc(1, sizeof(pending_config_t));
        if (cfg) {
          strncpy(cfg->ifname, ifname, sizeof(cfg->ifname) - 1);

          /* Check if admin up */
          if (strcmp(state, "up") == 0) {
            strncpy(cfg->enabled, "true", sizeof(cfg->enabled) - 1);
          }

          cfg->next = pending_interfaces;
          pending_interfaces = cfg;
        }
      }
    }
    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  /* Get IP addresses for each interface */
  pending_config_t *cfg = pending_interfaces;
  while (cfg) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "show interface %s addr", cfg->ifname);

    if (vpp_exec(cmd, output, sizeof(output)) == 0) {
      char *ip_line = strtok(output, "\r\n");
      while (ip_line) {
        /* Look for IPv4 addresses */
        char *ipv4 = strstr(ip_line, "L3 ");
        if (ipv4) {
          char addr[64];
          int prefix;
          if (sscanf(ipv4, "L3 %63[^/]/%d", addr, &prefix) == 2) {
            if (strchr(addr, ':') == NULL) { /* IPv4 */
              strncpy(cfg->ipv4_addr, addr, sizeof(cfg->ipv4_addr) - 1);
              cfg->ipv4_prefix = prefix;
            } else { /* IPv6 */
              strncpy(cfg->ipv6_addr, addr, sizeof(cfg->ipv6_addr) - 1);
              cfg->ipv6_prefix = prefix;
            }
          }
        }
        ip_line = strtok(NULL, "\r\n");
      }
    }
    cfg = cfg->next;
  }
}

int cli_vpp_commit(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  /* Clear old pending data and sync from VPP running state only */
  ds_clear_pending();
  ds_sync_interfaces_from_vpp();
  ds_sync_bonds_from_vpp();
  ds_sync_lcps_from_vpp();

  int ret = ds_write_config_file();

  if (ret == 0) {
    config_modified = 0;
    fprintf(stdout, "Configuration committed to %s\n", VPP_CONFIG_FILE);
  } else {
    fprintf(stderr, "Failed to commit configuration.\n");
  }

  return ret;
}

/* End command with confirmation for uncommitted changes */
int cli_end_confirm(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  /* Check if there are uncommitted changes */
  if (config_modified) {
    char answer[16];
    fprintf(stdout, "WARNING: Configuration has not been committed!\n");
    fprintf(stdout, "Uncommitted changes will be LOST.\n");
    fprintf(stdout, "Exit anyway? [yes/no]: ");
    fflush(stdout);

    if (fgets(answer, sizeof(answer), stdin) != NULL) {
      /* Remove newline */
      char *nl = strchr(answer, '\n');
      if (nl)
        *nl = '\0';

      if (strcmp(answer, "yes") == 0 || strcmp(answer, "y") == 0) {
        /* Clear and exit */
        current_interface[0] = '\0';
        config_modified = 0;
        fprintf(stdout, "Changes discarded. Returning to exec mode.\n");
        return 0;
      } else {
        fprintf(stdout,
                "Staying in config mode. Use 'commit' to save changes.\n");
        return -1; /* Return -1 to prevent mode switch */
      }
    }
  }

  /* No uncommitted changes, just exit */
  current_interface[0] = '\0';
  return 0;
}

/*=============================================================
 * NEGATION (no) COMMANDS
 *=============================================================*/

/* Remove specific IPv4 address from interface */
int cli_if_no_ip_address(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0) {
    fprintf(stderr, "Error: No interface selected\n");
    return -1;
  }

  cg_var *cv_ip = cvec_find(cvv, "ip");
  cg_var *cv_prefix = cvec_find(cvv, "prefix");

  if (!cv_ip || !cv_prefix) {
    fprintf(stderr, "Error: IP address and prefix required\n");
    return -1;
  }

  char ip_str[64];
  if (cv_type_get(cv_ip) == CGV_IPV4ADDR) {
    struct in_addr *addr = cv_ipv4addr_get(cv_ip);
    if (addr) {
      inet_ntop(AF_INET, addr, ip_str, sizeof(ip_str));
    } else {
      return -1;
    }
  } else {
    const char *ip = cv_string_get(cv_ip);
    if (!ip)
      return -1;
    strncpy(ip_str, ip, sizeof(ip_str) - 1);
  }

  int prefix = cv_int32_get(cv_prefix);

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "set interface ip address del %s %s/%d",
           current_interface, ip_str, prefix);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] Removed: %s/%d\n", current_interface, ip_str, prefix);
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Remove all IP addresses from interface */
int cli_if_no_ip_address_all(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  if (strlen(current_interface) == 0) {
    fprintf(stderr, "Error: No interface selected\n");
    return -1;
  }

  char cmd[256];
  char output[1024];

  /* Use VPP's del all command */
  snprintf(cmd, sizeof(cmd), "set interface ip address del %s all",
           current_interface);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] Removed all IP addresses\n", current_interface);
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Remove specific IPv6 address from interface */
int cli_if_no_ipv6_address(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  if (strlen(current_interface) == 0) {
    fprintf(stderr, "Error: No interface selected\n");
    return -1;
  }

  cg_var *cv_ip = cvec_find(cvv, "ip");
  cg_var *cv_prefix = cvec_find(cvv, "prefix");

  if (!cv_ip || !cv_prefix) {
    fprintf(stderr, "Error: IPv6 address and prefix required\n");
    return -1;
  }

  char ip_str[128];
  if (cv_type_get(cv_ip) == CGV_IPV6ADDR) {
    struct in6_addr *addr = cv_ipv6addr_get(cv_ip);
    if (addr) {
      inet_ntop(AF_INET6, addr, ip_str, sizeof(ip_str));
    } else {
      return -1;
    }
  } else {
    const char *ip = cv_string_get(cv_ip);
    if (!ip)
      return -1;
    strncpy(ip_str, ip, sizeof(ip_str) - 1);
  }

  int prefix = cv_int32_get(cv_prefix);

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "set interface ip address del %s %s/%d",
           current_interface, ip_str, prefix);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] Removed: %s/%d\n", current_interface, ip_str, prefix);
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Remove interface from bond */
int cli_if_no_channel_group(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  if (strlen(current_interface) == 0) {
    fprintf(stderr, "Error: No interface selected\n");
    return -1;
  }

  char cmd[256];
  char output[1024];

  snprintf(cmd, sizeof(cmd), "bond del %s", current_interface);

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "[%s] Removed from bond\n", current_interface);
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/* Delete interface (sub-interface, loopback, or bond) from configure mode */
int cli_no_interface(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  cg_var *cv = cvec_find(cvv, "ifname");
  if (!cv) {
    fprintf(stderr, "Error: Interface name required\n");
    return -1;
  }

  const char *ifname = cv_string_get(cv);
  if (!ifname) {
    fprintf(stderr, "Error: Invalid interface name\n");
    return -1;
  }

  char cmd[256];
  char output[1024];

  /* Determine interface type and use appropriate delete command */
  if (strncmp(ifname, "loop", 4) == 0) {
    /* Delete loopback */
    snprintf(cmd, sizeof(cmd), "delete loopback interface intfc %s", ifname);
  } else if (strncmp(ifname, "BondEthernet", 12) == 0 &&
             strchr(ifname, '.') == NULL) {
    /* Delete bond (only if no dot - not a sub-interface) */
    snprintf(cmd, sizeof(cmd), "delete bond interface BondEthernet%s",
             ifname + 12);
  } else if (strchr(ifname, '.') != NULL) {
    /* Delete sub-interface */
    snprintf(cmd, sizeof(cmd), "delete sub-interface %s", ifname);
  } else {
    fprintf(stderr, "Cannot delete physical interface: %s\n", ifname);
    return -1;
  }

  if (vpp_exec(cmd, output, sizeof(output)) == 0) {
    fprintf(stdout, "Deleted: %s\n", ifname);
    return 0;
  }
  fprintf(stderr, "Failed: %s\n", output);
  return -1;
}

/*=============================================================
 * SHOW COMMANDS - Cisco-style formatted output
 *=============================================================*/

/* Parse and format interface status in Cisco style */
int cli_show_interfaces(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  char addr_output[16384];

  /* Use show interface addr as primary source - cleaner format */
  if (vpp_exec("show interface addr", addr_output, sizeof(addr_output)) != 0) {
    fprintf(stderr, "Failed to get interface info\n");
    return -1;
  }

  /* Print Cisco-style header */
  fprintf(stdout, "\n");
  fprintf(stdout, "%-35s %-12s %-8s %s\n", "Interface", "Admin", "Link",
          "IP Address");
  fprintf(stdout, "============================================================"
                  "====================\n");

  /* Make a copy for parsing */
  char *output_copy = strdup(addr_output);
  if (!output_copy)
    return -1;

  /* Parse each line - format: "ifname (up):" or "ifname (dn):" or "  L3
   * ip/prefix" */
  /* Note: VPP uses CRLF line endings, so tokenize on both \r and \n */
  char *saveptr = NULL;
  char *line = strtok_r(output_copy, "\r\n", &saveptr);

  char current_if[64] = "";
  char current_state[16] = "";
  char ip_list[256] = "";
  int first_ip = 1;

  while (line) {
    /* Check if this is an interface line (starts with non-space, contains "(")
     */
    if (line[0] != ' ' && line[0] != '\t' && strchr(line, '(') != NULL) {
      /* Print previous interface if any */
      if (current_if[0] != '\0') {
        if (ip_list[0] == '\0')
          strcpy(ip_list, "-");
        const char *admin = (strcmp(current_state, "up") == 0) ? "up" : "down";
        const char *link = (strcmp(current_state, "up") == 0) ? "up" : "down";
        fprintf(stdout, "%-35s %-12s %-8s %s\n", current_if, admin, link,
                ip_list);
      }

      /* Parse new interface: "ifname (up):" or "ifname (dn):" */
      /* Find the space before ( */
      char *paren = strchr(line, '(');
      if (paren && paren > line) {
        /* Copy interface name (everything before the space before paren) */
        size_t name_len = paren - line;
        /* Trim trailing space */
        while (name_len > 0 && line[name_len - 1] == ' ') {
          name_len--;
        }
        if (name_len > 0 && name_len < sizeof(current_if)) {
          strncpy(current_if, line, name_len);
          current_if[name_len] = '\0';

          /* Get state (up or dn) */
          char *state_start = paren + 1;
          if (strncmp(state_start, "up", 2) == 0) {
            strcpy(current_state, "up");
          } else {
            strcpy(current_state, "down");
          }
        } else {
          current_if[0] = '\0';
        }
      } else {
        current_if[0] = '\0';
      }
      ip_list[0] = '\0';
      first_ip = 1;

    } else if (strncmp(line, "  L3 ", 5) == 0) {
      /* This is an IP address line */
      char *ip = line + 5;
      if (first_ip) {
        strncpy(ip_list, ip, sizeof(ip_list) - 1);
        first_ip = 0;
      } else {
        /* Append with comma */
        size_t len = strlen(ip_list);
        if (len + strlen(ip) + 3 < sizeof(ip_list)) {
          strcat(ip_list, ", ");
          strcat(ip_list, ip);
        }
      }
    }

    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  /* Print last interface */
  if (current_if[0] != '\0') {
    if (ip_list[0] == '\0')
      strcpy(ip_list, "-");
    const char *admin = (strcmp(current_state, "up") == 0) ? "up" : "down";
    const char *link = (strcmp(current_state, "up") == 0) ? "up" : "down";
    fprintf(stdout, "%-35s %-12s %-8s %s\n", current_if, admin, link, ip_list);
  }

  free(output_copy);
  fprintf(stdout, "\n");
  return 0;
}

/* Show interface detail - detailed info for single interface */
int cli_show_interface_detail(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)argv;

  cg_var *cv = cvec_find(cvv, "ifname");
  if (!cv) {
    fprintf(stderr, "Error: Interface name required\n");
    return -1;
  }

  const char *ifname = cv_string_get(cv);
  char cmd[256];
  char output[8192];

  /* Get detailed interface info */
  snprintf(cmd, sizeof(cmd), "show interface %s", ifname);
  if (vpp_exec(cmd, output, sizeof(output)) != 0) {
    fprintf(stderr, "Failed to get interface info\n");
    return -1;
  }

  fprintf(stdout, "\n");
  fprintf(stdout, "Interface: %s\n", ifname);
  fprintf(stdout, "============================================================"
                  "====================\n");

  /* Parse the raw output for key values */
  char *saveptr = NULL;
  char *line = strtok_r(output, "\r\n", &saveptr);

  char state[16] = "unknown";
  char mtu_str[64] = "-";
  int rx_packets = 0, tx_packets = 0;
  long long rx_bytes = 0, tx_bytes = 0;
  int drops = 0;

  while (line) {
    /* First line usually has: Name Idx State MTU */
    int idx = 0;
    int mtu = 0;
    char name[64];
    if (sscanf(line, "%63s %d %15s %d", name, &idx, state, &mtu) >= 4) {
      if (strcmp(name, ifname) == 0) {
        snprintf(mtu_str, sizeof(mtu_str), "%d", mtu);
      }
    }

    /* Parse counters */
    if (strstr(line, "rx packets")) {
      sscanf(line, " rx packets %d", &rx_packets);
    } else if (strstr(line, "tx packets")) {
      sscanf(line, " tx packets %d", &tx_packets);
    } else if (strstr(line, "rx bytes")) {
      sscanf(line, " rx bytes %lld", &rx_bytes);
    } else if (strstr(line, "tx bytes")) {
      sscanf(line, " tx bytes %lld", &tx_bytes);
    } else if (strstr(line, "drops")) {
      sscanf(line, " drops %d", &drops);
    }

    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  /* Get hardware info */
  char hw_output[4096];
  snprintf(cmd, sizeof(cmd), "show hardware-interfaces %s", ifname);
  vpp_exec(cmd, hw_output, sizeof(hw_output));

  /* Parse hardware info for MAC and speed */
  char mac[32] = "-";
  char speed[64] = "-";
  char driver[128] = "-";

  saveptr = NULL;
  line = strtok_r(hw_output, "\r\n", &saveptr);
  while (line) {
    /* Look for Ethernet address */
    char *eth = strstr(line, "Ethernet address ");
    if (eth) {
      sscanf(eth, "Ethernet address %31s", mac);
    }

    /* Look for link speed - format: "Link speed: 100 Gbps" */
    char *spd = strstr(line, "Link speed:");
    if (spd) {
      /* Copy rest of line after "Link speed:" */
      char *speed_val = spd + 12; /* skip "Link speed: " */
      /* Trim leading space */
      while (*speed_val == ' ')
        speed_val++;
      strncpy(speed, speed_val, sizeof(speed) - 1);
      /* Remove trailing whitespace */
      char *end = speed + strlen(speed) - 1;
      while (end > speed && (*end == ' ' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
      }
    }

    /* Look for driver - typically a line with just the driver name like
     * "Mellanox ConnectX..." */
    if (strstr(line, "Mellanox") || strstr(line, "Intel") ||
        strstr(line, "Amazon") || strstr(line, "Virtio")) {
      /* Trim leading spaces */
      char *drv = line;
      while (*drv == ' ')
        drv++;
      strncpy(driver, drv, sizeof(driver) - 1);
    }

    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  /* Get IP addresses */
  char addr_output[2048];
  snprintf(cmd, sizeof(cmd), "show interface addr %s", ifname);
  vpp_exec(cmd, addr_output, sizeof(addr_output));

  /* Print formatted output */
  fprintf(stdout, "  Status:           %s\n", state);
  fprintf(stdout, "  MTU:              %s bytes\n", mtu_str);
  fprintf(stdout, "  MAC Address:      %s\n", mac);
  fprintf(stdout, "  Speed:            %s\n", speed);
  fprintf(stdout, "  Driver:           %s\n", driver);
  fprintf(stdout, "\n");

  /* Print IP addresses */
  fprintf(stdout, "  IP Addresses:\n");
  saveptr = NULL;
  line = strtok_r(addr_output, "\r\n", &saveptr);
  int found_ip = 0;
  while (line) {
    if (strncmp(line, "  L3 ", 5) == 0) {
      fprintf(stdout, "    - %s\n", line + 5);
      found_ip = 1;
    }
    line = strtok_r(NULL, "\r\n", &saveptr);
  }
  if (!found_ip) {
    fprintf(stdout, "    (none)\n");
  }

  fprintf(stdout, "\n");
  fprintf(stdout, "  Statistics:\n");
  fprintf(stdout, "    RX packets:     %d\n", rx_packets);
  fprintf(stdout, "    RX bytes:       %lld\n", rx_bytes);
  fprintf(stdout, "    TX packets:     %d\n", tx_packets);
  fprintf(stdout, "    TX bytes:       %lld\n", tx_bytes);
  fprintf(stdout, "    Drops:          %d\n", drops);
  fprintf(stdout, "\n");

  return 0;
}

/* Show interface brief - summary table */
int cli_show_interfaces_brief(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  char output[8192];

  if (vpp_exec("show interface", output, sizeof(output)) != 0) {
    return -1;
  }

  fprintf(stdout, "\n");
  fprintf(stdout, "%-40s %-6s %-10s\n", "Interface", "Index", "Status");
  fprintf(stdout, "--------------------------------------------------------\n");

  char *line = strtok(output, "\n");
  while (line) {
    char name[64] = "";
    int idx = 0;
    char state[16] = "";

    if (sscanf(line, "%63s %d %15s", name, &idx, state) >= 3) {
      if (name[0] >= 'A' && name[0] <= 'z' && strcmp(name, "Name") != 0) {
        fprintf(stdout, "%-40s %-6d %-10s\n", name, idx, state);
      }
    }
    line = strtok(NULL, "\n");
  }

  fprintf(stdout, "\n");
  return 0;
}

/* Show bond - Cisco style with member details */
int cli_show_bond(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  char output[8192];

  /* Use show bond details for more info */
  if (vpp_exec("show bond details", output, sizeof(output)) != 0) {
    return -1;
  }

  fprintf(stdout, "\n");
  fprintf(stdout, "Port-channel Summary\n");
  fprintf(stdout, "============================================================"
                  "====================\n");

  /* Parse bond details output */
  char *saveptr = NULL;
  char *line = strtok_r(output, "\r\n", &saveptr);

  char current_bond[32] = "";
  char mode[32] = "";
  char lb[32] = "";
  int members = 0;
  int active = 0;
  char member_list[512] = "";

  while (line) {
    /* Check for bond interface name (starts with BondEthernet) */
    if (strncmp(line, "BondEthernet", 12) == 0) {
      /* Print previous bond if any */
      if (current_bond[0] != '\0') {
        fprintf(stdout, "\n%s\n", current_bond);
        fprintf(stdout, "  Mode:           %s\n", mode);
        fprintf(stdout, "  Load Balance:   %s\n", lb);
        fprintf(stdout, "  Members:        %d (Active: %d)\n", members, active);
        if (member_list[0] != '\0') {
          fprintf(stdout, "  Member List:    %s\n", member_list);
        }
      }

      strncpy(current_bond, line, sizeof(current_bond) - 1);
      mode[0] = '\0';
      lb[0] = '\0';
      members = 0;
      active = 0;
      member_list[0] = '\0';

    } else if (strstr(line, "mode:")) {
      sscanf(line, "  mode: %31s", mode);
    } else if (strstr(line, "load balance:")) {
      sscanf(line, "  load balance: %31s", lb);
    } else if (strstr(line, "number of members:")) {
      sscanf(line, "  number of members: %d", &members);
    } else if (strstr(line, "number of active members:")) {
      sscanf(line, "  number of active members: %d", &active);
    } else if (line[0] == ' ' && line[1] == ' ' && line[2] == ' ' &&
               line[3] == ' ') {
      /* Member interface line (indented with 4 spaces) */
      char *member = line + 4;
      if (member_list[0] != '\0') {
        strncat(member_list, ", ",
                sizeof(member_list) - strlen(member_list) - 1);
      }
      strncat(member_list, member,
              sizeof(member_list) - strlen(member_list) - 1);
    }

    line = strtok_r(NULL, "\r\n", &saveptr);
  }

  /* Print last bond */
  if (current_bond[0] != '\0') {
    fprintf(stdout, "\n%s\n", current_bond);
    fprintf(stdout, "  Mode:           %s\n", mode);
    fprintf(stdout, "  Load Balance:   %s\n", lb);
    fprintf(stdout, "  Members:        %d (Active: %d)\n", members, active);
    if (member_list[0] != '\0') {
      fprintf(stdout, "  Member List:    %s\n", member_list);
    }
  }

  fprintf(stdout, "\n");
  return 0;
}

/* Show LCP - formatted nicely */
int cli_show_lcp(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  char output[4096];

  if (vpp_exec("show lcp", output, sizeof(output)) != 0) {
    return -1;
  }

  fprintf(stdout, "\n");
  fprintf(stdout, "Linux Control Plane Interface Pairs\n");
  fprintf(stdout, "============================================================"
                  "====================\n");
  fprintf(stdout, "%-30s %-15s %-20s %s\n", "VPP Interface", "TAP",
          "Linux Interface", "Netns");
  fprintf(stdout, "------------------------------------------------------------"
                  "--------------------\n");

  /* Parse itf-pair lines */
  char *line = strtok(output, "\n");
  while (line) {
    if (strstr(line, "itf-pair")) {
      char vpp_if[64] = "";
      char tap[16] = "";
      char linux_if[32] = "";
      int idx = 0;

      /* Parse: itf-pair: [idx] vpp_if tap linux_if ... */
      if (sscanf(line, "itf-pair: [%d] %63s %15s %31s", &idx, vpp_if, tap,
                 linux_if) >= 4) {
        const char *netns = strstr(line, "netns ");
        char ns[32] = "default";
        if (netns) {
          sscanf(netns, "netns %31s", ns);
        }
        fprintf(stdout, "%-30s %-15s %-20s %s\n", vpp_if, tap, linux_if, ns);
      }
    }
    line = strtok(NULL, "\n");
  }

  fprintf(stdout, "\n");
  return 0;
}

/* Show IP interface - IP address summary */
int cli_show_ip_interface(clixon_handle h, cvec *cvv, cvec *argv) {
  (void)h;
  (void)cvv;
  (void)argv;

  char output[8192];

  if (vpp_exec("show interface addr", output, sizeof(output)) != 0) {
    return -1;
  }

  fprintf(stdout, "\n");
  fprintf(stdout, "IP Interface Configuration\n");
  fprintf(stdout, "============================================================"
                  "====================\n");
  fprintf(stdout, "%-40s %-8s %s\n", "Interface", "Status", "IP Address");
  fprintf(stdout, "------------------------------------------------------------"
                  "--------------------\n");

  char current_if[64] = "";
  char *line = strtok(output, "\n");

  while (line) {
    /* Check if this is an interface line */
    if (line[0] != ' ' && line[0] != '\t') {
      char name[64] = "";
      char state[16] = "";

      if (sscanf(line, "%63s (%15[^)])", name, state) >= 1) {
        strncpy(current_if, name, sizeof(current_if) - 1);
        fprintf(stdout, "%-40s %-8s ", name, state);
      }
    } else if (strstr(line, "L3 ")) {
      /* This is an IP address line */
      char *ip = strstr(line, "L3 ");
      if (ip) {
        ip += 3;
        /* Remove trailing whitespace */
        char *end = ip + strlen(ip) - 1;
        while (end > ip && (*end == ' ' || *end == '\n')) {
          *end = '\0';
          end--;
        }
        fprintf(stdout, "%s\n", ip);
      }
    }
    line = strtok(NULL, "\n");
  }

  fprintf(stdout, "\n");
  return 0;
}

/*=============================================================
 * PLUGIN INIT
 *=============================================================*/

clixon_plugin_api *clixon_plugin_init(clixon_handle h) {
  (void)h;
  static clixon_plugin_api api = {
      .ca_name = "vpp_cli",
      .ca_init = NULL,
      .ca_start = NULL,
      .ca_exit = NULL,
  };
  return &api;
}
