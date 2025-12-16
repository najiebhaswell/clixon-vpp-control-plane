/*
 * vpp_connection.h - VPP connection management
 */

#ifndef _VPP_CONNECTION_H_
#define _VPP_CONNECTION_H_

#include <stdbool.h>

#define VPP_CLIENT_NAME "clixon-vpp-plugin"

/* Connection management */
int vpp_connect(void);
void vpp_disconnect(void);
bool vpp_is_connected(void);
int vpp_reconnect(void);

/* CLI command execution */
char *vpp_cli_exec(const char *cmd);
int vpp_cli_exec_check(const char *cmd);

#endif /* _VPP_CONNECTION_H_ */
