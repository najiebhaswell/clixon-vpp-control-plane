/*
 * vpp_connection.c - VPP connection management via vppctl
 *
 * Uses the vppctl command to communicate with VPP.
 * This is simpler and more reliable than using the raw CLI socket.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vpp_connection.h"

#define VPP_CLI_BUFSIZE 65536
#define VPPCTL_PATH "/usr/bin/vppctl"

static bool g_connected = false;

int vpp_connect(void) {
  /* Check if vppctl is available and VPP is running */
  FILE *fp = popen(VPPCTL_PATH " show version 2>&1", "r");
  if (!fp) {
    fprintf(stderr, "[vpp] Failed to run vppctl: %s\n", strerror(errno));
    return -1;
  }

  char buf[256];
  if (fgets(buf, sizeof(buf), fp) != NULL) {
    /* Check for error messages */
    if (strstr(buf, "failed") || strstr(buf, "error") ||
        strstr(buf, "Connection refused")) {
      fprintf(stderr, "[vpp] VPP not running or not accessible: %s", buf);
      pclose(fp);
      return -1;
    }
    /* VPP is running */
    fprintf(stderr, "[vpp] Connected: %s", buf);
    g_connected = true;
  }

  pclose(fp);
  return g_connected ? 0 : -1;
}

void vpp_disconnect(void) {
  g_connected = false;
  fprintf(stderr, "[vpp] Disconnected\n");
}

bool vpp_is_connected(void) { return g_connected; }

int vpp_reconnect(void) {
  g_connected = false;
  return vpp_connect();
}

/*
 * Execute a VPP CLI command via vppctl
 * Returns allocated buffer with response (caller must free), or NULL on error
 */
char *vpp_cli_exec(const char *cmd) {
  char *response = NULL;
  char cmdline[512];
  FILE *fp;
  size_t total_read = 0;
  size_t bufsize = VPP_CLI_BUFSIZE;

  if (!g_connected) {
    if (vpp_connect() != 0) {
      return NULL;
    }
  }

  /* Build command line */
  snprintf(cmdline, sizeof(cmdline), "%s %s 2>&1", VPPCTL_PATH, cmd);

  fp = popen(cmdline, "r");
  if (!fp) {
    fprintf(stderr, "[vpp] popen() failed: %s\n", strerror(errno));
    return NULL;
  }

  /* Allocate response buffer */
  response = malloc(bufsize);
  if (!response) {
    pclose(fp);
    return NULL;
  }
  response[0] = '\0';

  /* Read all output */
  while (1) {
    size_t n = fread(response + total_read, 1, bufsize - total_read - 1, fp);
    if (n == 0)
      break;
    total_read += n;

    if (total_read >= bufsize - 1) {
      /* Grow buffer */
      bufsize *= 2;
      char *new_buf = realloc(response, bufsize);
      if (!new_buf) {
        free(response);
        pclose(fp);
        return NULL;
      }
      response = new_buf;
    }
  }

  response[total_read] = '\0';

  int status = pclose(fp);
  if (status != 0) {
    /* Command might have failed but still returned output */
    fprintf(stderr, "[vpp] vppctl returned status %d\n", status);
  }

  return response;
}

/*
 * Execute a CLI command and check for success
 * Returns 0 on success, -1 on failure
 */
int vpp_cli_exec_check(const char *cmd) {
  char *response = vpp_cli_exec(cmd);
  int ret = 0;

  if (!response) {
    return -1;
  }

  /* Check for error indicators in response */
  if (strstr(response, "error") || strstr(response, "Error") ||
      strstr(response, "unknown input") || strstr(response, "failed")) {
    fprintf(stderr, "[vpp] Command failed: %s\nResponse: %s\n", cmd, response);
    ret = -1;
  }

  free(response);
  return ret;
}
