#!/bin/bash
#
# VPP Control Plane - Cisco-like Interactive CLI
# With tab completion, configure mode, interface context
#

CLIXON_CONF="/etc/clixon/clixon-vpp.xml"
VPP_NS="http://example.com/vpp/interfaces"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'
BOLD='\033[1m'

# State variables
HOSTNAME=$(hostname -s 2>/dev/null || echo "router")
CLI_MODE="exec"  # exec, config, config-if
CURRENT_IF=""
declare -A IF_CACHE

# ==================== NETCONF Helper ====================
netconf_rpc() {
    echo "<?xml version=\"1.0\"?><hello xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\"><capabilities><capability>urn:ietf:params:netconf:base:1.0</capability></capabilities></hello>]]>]]>$1]]>]]>" | sudo clixon_netconf -f "$CLIXON_CONF" 2>/dev/null
}

# ==================== System Info ====================
get_system_info() {
    local distro=$(grep "PRETTY_NAME" /etc/os-release 2>/dev/null | cut -d'"' -f2)
    local kernel=$(uname -r)
    local mem_used=$(free -h | awk '/Mem:/ {print $3}')
    local mem_total=$(free -h | awk '/Mem:/ {print $2}')
    local cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | cut -d'%' -f1)
    
    echo -e "${CYAN}========================================================================"
    echo "------------------------INFORMASI ROUTER--------------------------------"
    echo -e "========================================================================${NC}"
    echo -e "Device Name             : ${BOLD}${HOSTNAME}${NC}"
    echo -e "Distro                  : ${distro}"
    echo -e "Kernel                  : ${kernel}"
    echo -e "Memory Usage            : ${mem_used} used / ${mem_total} total"
    echo -e "CPU Usage               : ${cpu_usage}%"
    echo -e "${CYAN}========================================================================${NC}"
    echo ""
}

# ==================== Interface Cache ====================
refresh_interface_cache() {
    IF_CACHE=()
    local addr_output=$(sudo vppctl show interface addr 2>/dev/null)
    local current_if=""
    
    while IFS= read -r line; do
        if [[ "$line" =~ ^([A-Za-z0-9/.:-]+)[[:space:]]+\((up|dn)\): ]]; then
            current_if="${BASH_REMATCH[1]}"
            IF_CACHE["$current_if,state"]="${BASH_REMATCH[2]}"
            IF_CACHE["$current_if,ips"]=""
        elif [[ "$line" =~ ^[[:space:]]+L3[[:space:]]+([^[:space:]]+) ]]; then
            [[ -n "${IF_CACHE[$current_if,ips]}" ]] && \
                IF_CACHE["$current_if,ips"]="${IF_CACHE[$current_if,ips]},${BASH_REMATCH[1]}" || \
                IF_CACHE["$current_if,ips"]="${BASH_REMATCH[1]}"
        fi
    done <<< "$addr_output"
}

get_interface_list() {
    sudo vppctl show interface addr 2>/dev/null | grep -oP '^[A-Za-z0-9/.:-]+(?=\s+\()'
}

# ==================== Show Commands ====================
cmd_show_version() {
    sudo vppctl show version 2>/dev/null
}

cmd_show_interfaces() {
    refresh_interface_cache
    
    printf "\n${BOLD}%-30s %-20s %-6s %-8s %-8s${NC}\n" "Interface" "IP-Address" "MTU" "Status" "Protocol"
    
    local if_output=$(sudo vppctl show interface 2>/dev/null)
    
    while IFS= read -r line; do
        [[ "$line" =~ ^[[:space:]] ]] && continue
        [[ "$line" =~ ^[[:space:]]*Name ]] && continue
        [[ -z "$line" ]] && continue
        
        local name=$(echo "$line" | awk '{print $1}')
        local state=$(echo "$line" | awk '{print $3}')
        local mtu=$(echo "$line" | awk '{print $4}' | cut -d'/' -f1)
        
        [[ -z "$name" || ! "$name" =~ ^[A-Za-z] ]] && continue
        
        local ip="${IF_CACHE[$name,ips]:-unassigned}"
        [[ -z "$ip" ]] && ip="unassigned"
        # Show first IP only in summary
        ip=$(echo "$ip" | cut -d',' -f1)
        
        local status_col protocol_col
        if [[ "$state" == "up" ]]; then
            status_col="${GREEN}up${NC}"
            protocol_col="${GREEN}up${NC}"
        else
            status_col="${RED}down${NC}"
            protocol_col="${RED}down${NC}"
        fi
        
        printf "%-30s %-20s %-6s " "$name" "$ip" "$mtu"
        echo -en "$status_col     $protocol_col"
        echo ""
    done <<< "$if_output"
    echo ""
}

cmd_show_interface_detail() {
    local ifname="$1"
    [[ -z "$ifname" ]] && { echo "Usage: show interface <name>"; return; }
    
    local if_info=$(sudo vppctl show interface "$ifname" 2>&1)
    [[ "$if_info" =~ "unknown" ]] && { echo "Interface not found: $ifname"; return; }
    
    echo -e "\n${BOLD}Interface: ${ifname}${NC}"
    echo "────────────────────────────────────────────────"
    
    local main_line=$(echo "$if_info" | head -2 | tail -1)
    local state=$(echo "$main_line" | awk '{print $3}')
    local mtu=$(echo "$main_line" | awk '{print $4}' | cut -d'/' -f1)
    
    local hw=$(sudo vppctl show hardware-interfaces "$ifname" 2>/dev/null)
    local mac=$(echo "$hw" | grep "Ethernet address" | awk '{print $3}')
    local speed=$(echo "$hw" | grep "Link speed:" | sed 's/.*Link speed://')
    
    echo "  Hardware is $(echo "$hw" | head -1 | awk '{print $1}')"
    [[ -n "$mac" ]] && echo "  Hardware address is $mac"
    echo "  MTU $mtu bytes"
    [[ "$state" == "up" ]] && echo -e "  Line protocol is ${GREEN}up${NC}" || echo -e "  Line protocol is ${RED}down${NC}"
    [[ -n "$speed" ]] && echo "  Link speed:$speed"
    
    echo -e "\n  Internet address(es):"
    local ips=$(sudo vppctl show interface addr "$ifname" 2>/dev/null | grep "L3" | awk '{print $2}')
    if [[ -n "$ips" ]]; then
        echo "$ips" | while read ip; do echo "    $ip"; done
    else
        echo "    No IP address configured"
    fi
    
    echo -e "\n  Traffic statistics:"
    echo "$if_info" | grep -E "packets|bytes" | head -4 | while read l; do echo "    $l"; done
    echo ""
}

cmd_show_running_config() {
    echo -e "\n${BOLD}Current Configuration:${NC}"
    echo "────────────────────────────────────────────────"
    
    local interfaces=$(get_interface_list)
    for ifname in $interfaces; do
        local state=$(sudo vppctl show interface "$ifname" 2>/dev/null | head -2 | tail -1 | awk '{print $3}')
        local ips=$(sudo vppctl show interface addr "$ifname" 2>/dev/null | grep "L3" | awk '{print $2}')
        
        [[ "$ifname" == "local0" ]] && continue
        
        echo "!"
        echo "interface $ifname"
        if [[ -n "$ips" ]]; then
            echo "$ips" | while read ip; do echo " ip address $ip"; done
        fi
        [[ "$state" == "up" ]] && echo " no shutdown" || echo " shutdown"
    done
    echo "!"
    echo ""
}

cmd_show_lcp() {
    echo -e "\n${BOLD}LCP Interface Pairs:${NC}"
    sudo vppctl show lcp 2>/dev/null
    echo ""
}

cmd_show_bond() {
    echo -e "\n${BOLD}Bond Interfaces:${NC}"
    sudo vppctl show bond 2>/dev/null
    echo ""
}

# ==================== Configuration Commands ====================
cmd_interface_ip() {
    local addr="$1"
    [[ -z "$CURRENT_IF" ]] && { echo "No interface selected"; return; }
    [[ -z "$addr" ]] && { echo "Usage: ip address <ip/prefix>"; return; }
    
    local ip=$(echo "$addr" | cut -d/ -f1)
    local prefix=$(echo "$addr" | cut -d/ -f2)
    local typ="ipv4"; echo "$ip" | grep -q ":" && typ="ipv6"
    
    local r=$(netconf_rpc "<?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\"><edit-config><target><candidate/></target><config><interfaces xmlns=\"${VPP_NS}\"><interface><name>$CURRENT_IF</name><${typ}><address><ip>${ip}</ip><prefix-length>${prefix}</prefix-length></address></${typ}></interface></interfaces></config></edit-config></rpc>]]>]]><?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"2\"><commit/></rpc>")
    
    echo "$r" | grep -q "<ok/>" && echo "IP address $addr configured on $CURRENT_IF" || echo "Failed to configure IP address"
}

cmd_interface_enable() {
    [[ -z "$CURRENT_IF" ]] && { echo "No interface selected"; return; }
    
    local r=$(netconf_rpc "<?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\"><edit-config><target><candidate/></target><config><interfaces xmlns=\"${VPP_NS}\"><interface><name>$CURRENT_IF</name><enabled>true</enabled></interface></interfaces></config></edit-config></rpc>]]>]]><?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"2\"><commit/></rpc>")
    
    echo "$r" | grep -q "<ok/>" && echo "Interface $CURRENT_IF is now up" || echo "Failed to enable interface"
}

cmd_interface_disable() {
    [[ -z "$CURRENT_IF" ]] && { echo "No interface selected"; return; }
    
    local r=$(netconf_rpc "<?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\"><edit-config><target><candidate/></target><config><interfaces xmlns=\"${VPP_NS}\"><interface><name>$CURRENT_IF</name><enabled>false</enabled></interface></interfaces></config></edit-config></rpc>]]>]]><?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"2\"><commit/></rpc>")
    
    echo "$r" | grep -q "<ok/>" && echo "Interface $CURRENT_IF is now down" || echo "Failed to disable interface"
}

cmd_create_loopback() {
    local name="$1"
    local r=$(netconf_rpc "<?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\"><create-loopback xmlns=\"${VPP_NS}\"/></rpc>")
    local n=$(echo "$r" | grep -oP 'interface-name[^>]*>\K[^<]+')
    [[ -n "$n" ]] && echo "Loopback interface $n created" || echo "Failed to create loopback"
}

cmd_create_subif() {
    local parent="$1"
    local vlan="$2"
    [[ -z "$parent" || -z "$vlan" ]] && { echo "Usage: create subinterface <parent> <vlan>"; return; }
    
    local r=$(netconf_rpc "<?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\"><create-sub-interface xmlns=\"${VPP_NS}\"><parent-interface>$parent</parent-interface><vlan-id>$vlan</vlan-id></create-sub-interface></rpc>")
    local n=$(echo "$r" | grep -oP 'interface-name[^>]*>\K[^<]+')
    [[ -n "$n" ]] && echo "Sub-interface $n created" || echo "Failed to create sub-interface"
}

cmd_create_bond() {
    local mode="${1:-lacp}"
    local r=$(netconf_rpc "<?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\"><create-bond xmlns=\"${VPP_NS}\"><mode>$mode</mode></create-bond></rpc>")
    local n=$(echo "$r" | grep -oP 'interface-name[^>]*>\K[^<]+')
    [[ -n "$n" ]] && echo "Bond interface $n created (mode: $mode)" || echo "Failed to create bond"
}

cmd_create_lcp() {
    local vpp_if="$1"
    local linux_if="$2"
    [[ -z "$vpp_if" || -z "$linux_if" ]] && { echo "Usage: lcp create <vpp-interface> <linux-interface>"; return; }
    
    local r=$(netconf_rpc "<?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\"><lcp-create xmlns=\"${VPP_NS}\"><interface-name>$vpp_if</interface-name><host-interface>$linux_if</host-interface></lcp-create></rpc>")
    echo "$r" | grep -q "true" && echo "LCP pair created: $vpp_if -> $linux_if" || echo "Failed to create LCP pair"
}

cmd_bond_member() {
    local member="$1"
    [[ -z "$CURRENT_IF" ]] && { echo "No bond interface selected"; return; }
    [[ -z "$member" ]] && { echo "Usage: member <interface>"; return; }
    
    local r=$(netconf_rpc "<?xml version=\"1.0\"?><rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"1\"><bond-add-member xmlns=\"${VPP_NS}\"><bond-interface>$CURRENT_IF</bond-interface><member-interface>$member</member-interface></bond-add-member></rpc>")
    echo "$r" | grep -q "true" && echo "Member $member added to bond $CURRENT_IF" || echo "Failed to add member"
}

# ==================== Tab Completion ====================
_get_completions() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local prev="${COMP_WORDS[COMP_CWORD-1]}"
    local cmd="${COMP_WORDS[1]}"
    
    case "$CLI_MODE" in
        exec)
            if [[ $COMP_CWORD -eq 1 ]]; then
                COMPREPLY=($(compgen -W "show configure exit quit help" -- "$cur"))
            elif [[ "$cmd" == "show" ]]; then
                if [[ $COMP_CWORD -eq 2 ]]; then
                    COMPREPLY=($(compgen -W "interfaces interface version running-config lcp bond" -- "$cur"))
                elif [[ "${COMP_WORDS[2]}" == "interface" && $COMP_CWORD -eq 3 ]]; then
                    COMPREPLY=($(compgen -W "$(get_interface_list)" -- "$cur"))
                fi
            fi
            ;;
        config)
            if [[ $COMP_CWORD -eq 1 ]]; then
                COMPREPLY=($(compgen -W "interface create lcp exit end help" -- "$cur"))
            elif [[ "$cmd" == "interface" && $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=($(compgen -W "$(get_interface_list) loop" -- "$cur"))
            elif [[ "$cmd" == "create" && $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=($(compgen -W "loopback bond subinterface" -- "$cur"))
            elif [[ "$cmd" == "lcp" && $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=($(compgen -W "create delete" -- "$cur"))
            fi
            ;;
        config-if)
            if [[ $COMP_CWORD -eq 1 ]]; then
                COMPREPLY=($(compgen -W "ip no enable shutdown member exit end help" -- "$cur"))
            elif [[ "$cmd" == "ip" && $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=($(compgen -W "address" -- "$cur"))
            elif [[ "$cmd" == "member" && $COMP_CWORD -eq 2 ]]; then
                COMPREPLY=($(compgen -W "$(get_interface_list)" -- "$cur"))
            fi
            ;;
    esac
}

setup_completion() {
    complete -F _get_completions -o default ""
}

# ==================== Prompt ====================
get_prompt() {
    case "$CLI_MODE" in
        exec)
            echo -e "${BOLD}${GREEN}${HOSTNAME}#${NC} "
            ;;
        config)
            echo -e "${BOLD}${GREEN}${HOSTNAME}(config)#${NC} "
            ;;
        config-if)
            echo -e "${BOLD}${GREEN}${HOSTNAME}(config-if:${CURRENT_IF})#${NC} "
            ;;
    esac
}

# ==================== Command Processing ====================
process_exec_command() {
    local cmd="$1"; shift
    
    case "$cmd" in
        show)
            case "$1" in
                version) cmd_show_version ;;
                interfaces) cmd_show_interfaces ;;
                interface) cmd_show_interface_detail "$2" ;;
                running-config|running) cmd_show_running_config ;;
                lcp) cmd_show_lcp ;;
                bond) cmd_show_bond ;;
                *) echo "Usage: show [version|interfaces|interface <name>|running-config|lcp|bond]" ;;
            esac
            ;;
        configure|config)
            CLI_MODE="config"
            echo "Entering configuration mode..."
            ;;
        exit|quit|q)
            echo "Goodbye!"
            exit 0
            ;;
        help|\?)
            echo -e "\n${BOLD}Exec Mode Commands:${NC}"
            echo "  show version           - Show VPP version"
            echo "  show interfaces        - Show all interfaces"
            echo "  show interface <name>  - Show interface details"
            echo "  show running-config    - Show running configuration"
            echo "  show lcp               - Show LCP pairs"
            echo "  show bond              - Show bond interfaces"
            echo "  configure              - Enter configuration mode"
            echo "  exit                   - Exit CLI"
            echo ""
            ;;
        "") ;;
        *) echo "Unknown command: $cmd. Type 'help' for commands." ;;
    esac
}

process_config_command() {
    local cmd="$1"; shift
    
    case "$cmd" in
        interface)
            local ifname="$1"
            [[ -z "$ifname" ]] && { echo "Usage: interface <name|loopN>"; return; }
            
            # Check if creating new loopback
            if [[ "$ifname" =~ ^loop[0-9]+$ ]]; then
                # Check if exists
                if ! sudo vppctl show interface "$ifname" 2>&1 | grep -q "unknown"; then
                    CURRENT_IF="$ifname"
                    CLI_MODE="config-if"
                else
                    # Create new loopback
                    cmd_create_loopback
                    local new_loop=$(sudo vppctl show interface 2>/dev/null | grep "^loop" | tail -1 | awk '{print $1}')
                    if [[ -n "$new_loop" ]]; then
                        CURRENT_IF="$new_loop"
                        CLI_MODE="config-if"
                        echo "Loopback interface $new_loop created"
                    fi
                fi
            else
                # Existing interface
                if sudo vppctl show interface "$ifname" 2>&1 | grep -q "unknown"; then
                    echo "Interface $ifname does not exist"
                else
                    CURRENT_IF="$ifname"
                    CLI_MODE="config-if"
                fi
            fi
            ;;
        create)
            case "$1" in
                loopback) cmd_create_loopback ;;
                bond) cmd_create_bond "$2" ;;
                subinterface|subif) cmd_create_subif "$2" "$3" ;;
                *) echo "Usage: create [loopback|bond <mode>|subinterface <parent> <vlan>]" ;;
            esac
            ;;
        lcp)
            case "$1" in
                create) cmd_create_lcp "$2" "$3" ;;
                *) echo "Usage: lcp create <vpp-interface> <linux-interface>" ;;
            esac
            ;;
        end|exit)
            CLI_MODE="exec"
            echo "Exiting configuration mode..."
            ;;
        help|\?)
            echo -e "\n${BOLD}Config Mode Commands:${NC}"
            echo "  interface <name>                  - Configure interface"
            echo "  interface loop<N>                 - Create/configure loopback"
            echo "  create loopback                   - Create loopback interface"
            echo "  create bond <mode>                - Create bond (lacp|active-backup)"
            echo "  create subinterface <if> <vlan>   - Create sub-interface"
            echo "  lcp create <vpp-if> <linux-if>    - Create LCP pair"
            echo "  end                               - Return to exec mode"
            echo ""
            ;;
        "") ;;
        *) echo "Unknown command: $cmd" ;;
    esac
}

process_config_if_command() {
    local cmd="$1"; shift
    
    case "$cmd" in
        ip)
            case "$1" in
                address) cmd_interface_ip "$2" ;;
                *) echo "Usage: ip address <ip/prefix>" ;;
            esac
            ;;
        no)
            case "$1" in
                shutdown) cmd_interface_enable ;;
                *) echo "Usage: no shutdown" ;;
            esac
            ;;
        enable)
            cmd_interface_enable
            ;;
        shutdown)
            cmd_interface_disable
            ;;
        member)
            cmd_bond_member "$1"
            ;;
        exit)
            CLI_MODE="config"
            CURRENT_IF=""
            ;;
        end)
            CLI_MODE="exec"
            CURRENT_IF=""
            ;;
        help|\?)
            echo -e "\n${BOLD}Interface Config Commands:${NC}"
            echo "  ip address <ip/prefix>  - Configure IP address"
            echo "  enable / no shutdown    - Enable interface"
            echo "  shutdown                - Disable interface"
            echo "  member <interface>      - Add bond member"
            echo "  exit                    - Return to config mode"
            echo "  end                     - Return to exec mode"
            echo ""
            ;;
        "") ;;
        *) echo "Unknown command: $cmd" ;;
    esac
}

process_command() {
    case "$CLI_MODE" in
        exec) process_exec_command "$@" ;;
        config) process_config_command "$@" ;;
        config-if) process_config_if_command "$@" ;;
    esac
}

# ==================== Main Loop ====================
main() {
    # Banner
    get_system_info
    
    # Setup readline if available
    if [[ -t 0 ]]; then
        bind 'set show-all-if-ambiguous on' 2>/dev/null
        bind 'set completion-ignore-case on' 2>/dev/null
    fi
    
    while true; do
        # Get prompt based on mode
        local prompt=$(get_prompt)
        
        # Read with completion
        if [[ "$CLI_MODE" == "exec" ]]; then
            read -e -p "$prompt" -a words
        elif [[ "$CLI_MODE" == "config" ]]; then
            read -e -p "$prompt" -a words
        else
            read -e -p "$prompt" -a words
        fi
        
        [[ $? -ne 0 ]] && { echo; exit 0; }
        
        # Process command
        process_command "${words[@]}"
    done
}

[[ "${BASH_SOURCE[0]}" == "$0" ]] && main
