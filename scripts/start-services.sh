#!/bin/bash
#
# start-services.sh - Start VPP and Clixon services
#
# This script starts the required services in order:
# 1. VPP dataplane
# 2. Clixon backend
# 3. Clixon RESTCONF (optional)

set -e

# Configuration
CLIXON_CONFIG="/etc/clixon/clixon-vpp.xml"
VPP_STARTUP_CONFIG="/etc/vpp/startup.conf"
LOG_DIR="/var/log/clixon"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root"
        exit 1
    fi
}

create_log_dir() {
    mkdir -p "$LOG_DIR"
    chmod 755 "$LOG_DIR"
}

start_vpp() {
    log_info "Starting VPP..."
    
    # Check if VPP is already running
    if pgrep -x vpp > /dev/null; then
        log_info "VPP is already running"
        return 0
    fi
    
    # Start VPP via systemd if available
    if systemctl is-enabled vpp &> /dev/null; then
        systemctl start vpp
        sleep 2
        
        if systemctl is-active vpp &> /dev/null; then
            log_info "VPP started successfully via systemd"
            return 0
        else
            log_error "Failed to start VPP via systemd"
            systemctl status vpp
            return 1
        fi
    else
        # Start VPP directly
        log_warn "Starting VPP without systemd..."
        vpp -c "$VPP_STARTUP_CONFIG" &
        sleep 3
        
        if pgrep -x vpp > /dev/null; then
            log_info "VPP started successfully"
            return 0
        else
            log_error "Failed to start VPP"
            return 1
        fi
    fi
}

wait_for_vpp() {
    log_info "Waiting for VPP API to be ready..."
    
    local max_attempts=30
    local attempt=0
    
    while [[ $attempt -lt $max_attempts ]]; do
        if [[ -S /run/vpp/api.sock ]] || [[ -S /var/run/vpp/api.sock ]]; then
            log_info "VPP API socket is ready"
            return 0
        fi
        
        ((attempt++))
        sleep 1
    done
    
    log_error "Timeout waiting for VPP API socket"
    return 1
}

start_clixon_backend() {
    log_info "Starting Clixon backend..."
    
    # Check if already running
    if pgrep -f "clixon_backend.*clixon-vpp" > /dev/null; then
        log_info "Clixon backend is already running"
        return 0
    fi
    
    # Verify config file exists
    if [[ ! -f "$CLIXON_CONFIG" ]]; then
        log_error "Clixon config not found: $CLIXON_CONFIG"
        log_info "Run 'make install' first"
        return 1
    fi
    
    # Create data directory
    mkdir -p /var/lib/clixon/vpp
    
    # Start backend
    clixon_backend -f "$CLIXON_CONFIG" -l o -s init
    
    sleep 1
    
    if pgrep -f "clixon_backend" > /dev/null; then
        log_info "Clixon backend started successfully"
        return 0
    else
        log_error "Failed to start Clixon backend"
        return 1
    fi
}

start_clixon_restconf() {
    log_info "Starting Clixon RESTCONF..."
    
    # Check if already running
    if pgrep -f "clixon_restconf" > /dev/null; then
        log_info "Clixon RESTCONF is already running"
        return 0
    fi
    
    # Start RESTCONF daemon
    clixon_restconf -f "$CLIXON_CONFIG" -l o &
    
    sleep 1
    
    if pgrep -f "clixon_restconf" > /dev/null; then
        log_info "Clixon RESTCONF started on port 8080"
        return 0
    else
        log_warn "Failed to start Clixon RESTCONF (optional)"
        return 0  # Non-fatal
    fi
}

stop_services() {
    log_info "Stopping services..."
    
    # Stop RESTCONF
    pkill -f "clixon_restconf" 2>/dev/null || true
    
    # Stop backend
    pkill -f "clixon_backend" 2>/dev/null || true
    
    # Optionally stop VPP
    if [[ "$1" == "--include-vpp" ]]; then
        systemctl stop vpp 2>/dev/null || pkill -x vpp 2>/dev/null || true
        log_info "VPP stopped"
    fi
    
    log_info "Services stopped"
}

show_status() {
    echo ""
    echo "Service Status:"
    echo "==============="
    
    # VPP
    if pgrep -x vpp > /dev/null; then
        echo -e "VPP:              ${GREEN}Running${NC}"
    else
        echo -e "VPP:              ${RED}Stopped${NC}"
    fi
    
    # Clixon Backend
    if pgrep -f "clixon_backend" > /dev/null; then
        echo -e "Clixon Backend:   ${GREEN}Running${NC}"
    else
        echo -e "Clixon Backend:   ${RED}Stopped${NC}"
    fi
    
    # Clixon RESTCONF
    if pgrep -f "clixon_restconf" > /dev/null; then
        echo -e "Clixon RESTCONF:  ${GREEN}Running${NC}"
    else
        echo -e "Clixon RESTCONF:  ${YELLOW}Not Running${NC}"
    fi
    
    echo ""
}

print_usage() {
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  start     - Start all services (default)"
    echo "  stop      - Stop Clixon services"
    echo "  stop-all  - Stop all services including VPP"
    echo "  restart   - Restart all services"
    echo "  status    - Show service status"
    echo "  cli       - Start Clixon CLI"
    echo ""
}

start_cli() {
    # Check if backend is running
    if ! pgrep -f "clixon_backend" > /dev/null; then
        log_error "Clixon backend is not running. Start it first."
        exit 1
    fi
    
    log_info "Starting Clixon CLI..."
    echo "(Type 'quit' to exit)"
    echo ""
    
    clixon_cli -f "$CLIXON_CONFIG"
}

# Main
main() {
    case "${1:-start}" in
        start)
            check_root
            create_log_dir
            start_vpp
            wait_for_vpp
            start_clixon_backend
            start_clixon_restconf
            show_status
            ;;
        stop)
            check_root
            stop_services
            show_status
            ;;
        stop-all)
            check_root
            stop_services --include-vpp
            show_status
            ;;
        restart)
            check_root
            stop_services
            sleep 2
            start_vpp
            wait_for_vpp
            start_clixon_backend
            start_clixon_restconf
            show_status
            ;;
        status)
            show_status
            ;;
        cli)
            start_cli
            ;;
        *)
            print_usage
            exit 1
            ;;
    esac
}

main "$@"
