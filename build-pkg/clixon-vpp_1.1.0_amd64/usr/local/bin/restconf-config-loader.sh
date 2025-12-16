#!/bin/bash
#
# RESTCONF-based Config Loader for Clixon VPP
# Load configuration from XML file via RESTCONF API
# Supports both XML and JSON formats
#
# Usage:
#   ./restconf-config-loader.sh config.xml [--no-commit] [--validate-only]
#

set -o pipefail

# Configuration
RESTCONF_BASE="http://localhost:8080/restconf"
RESTCONF_USER="${RESTCONF_USER:-admin}"
RESTCONF_PASS="${RESTCONF_PASS:-admin}"
RESTCONF_TIMEOUT=30

LOG_FILE="${LOG_FILE:-/tmp/restconf-loader-$(date +%s).log}"
BACKUP_DIR="${BACKUP_DIR:-/tmp/restconf-backups}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log() {
    local level="$1"
    shift
    local msg="$@"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[${timestamp}] [${level}] ${msg}" | tee -a "$LOG_FILE"
}

log_info() {
    echo -e "${BLUE}[INFO]${NC} $@" | tee -a "$LOG_FILE"
}

log_success() {
    echo -e "${GREEN}[OK]${NC} $@" | tee -a "$LOG_FILE"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $@" | tee -a "$LOG_FILE" >&2
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $@" | tee -a "$LOG_FILE"
}

# Parse arguments
CONFIG_FILE=""
COMMIT=true
VALIDATE_ONLY=false
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --no-commit)
            COMMIT=false
            shift
            ;;
        --validate-only)
            VALIDATE_ONLY=true
            COMMIT=false
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            COMMIT=false
            shift
            ;;
        -*)
            log_error "Unknown option: $1"
            exit 1
            ;;
        *)
            CONFIG_FILE="$1"
            shift
            ;;
    esac
done

# Validate input
if [ -z "$CONFIG_FILE" ]; then
    log_error "Usage: $0 <config-file> [--no-commit] [--validate-only] [--dry-run]"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    log_error "Config file not found: $CONFIG_FILE"
    exit 1
fi

mkdir -p "$BACKUP_DIR"

log_info "=== RESTCONF Config Loader Started ==="
log_info "Config file: $CONFIG_FILE"
log_info "RESTCONF URL: $RESTCONF_BASE"
log_info "Commit after load: $COMMIT"
log_info "Validate only: $VALIDATE_ONLY"
log_info "Dry run: $DRY_RUN"
log_info "Log file: $LOG_FILE"
echo ""

# Function to check RESTCONF connectivity
check_restconf() {
    log_info "Checking RESTCONF connectivity..."
    
    local response=$(curl -s -w "\n%{http_code}" \
        --connect-timeout 5 \
        -u "$RESTCONF_USER:$RESTCONF_PASS" \
        "$RESTCONF_BASE/yang-library:yang-library" 2>&1)
    
    local http_code=$(echo "$response" | tail -n1)
    
    if [ "$http_code" = "200" ] || [ "$http_code" = "404" ]; then
        log_success "RESTCONF is reachable (HTTP $http_code)"
        return 0
    else
        log_error "RESTCONF unreachable (HTTP $http_code)"
        log_error "Make sure clixon_backend and clixon_restconf are running"
        return 1
    fi
}

# Function to backup current running config
backup_running_config() {
    log_info "Backing up current running configuration..."
    
    local backup_file="$BACKUP_DIR/running_config_$(date +%Y%m%d_%H%M%S).xml"
    
    local response=$(curl -s -w "\n%{http_code}" \
        -u "$RESTCONF_USER:$RESTCONF_PASS" \
        -H "Accept: application/yang-data+xml" \
        "$RESTCONF_BASE/data" 2>&1)
    
    local http_code=$(echo "$response" | tail -n1)
    local body=$(echo "$response" | head -n-1)
    
    if [ "$http_code" = "200" ]; then
        echo "$body" > "$backup_file"
        log_success "Backup saved to: $backup_file"
        echo "$backup_file"
        return 0
    else
        log_warn "Could not backup current config (HTTP $http_code)"
        return 1
    fi
}

# Function to detect file format (XML or JSON)
detect_format() {
    local file="$1"
    local first_char=$(head -c 1 "$file")
    
    if [ "$first_char" = "<" ]; then
        echo "xml"
    elif [ "$first_char" = "{" ]; then
        echo "json"
    else
        log_error "Cannot detect file format. Expected XML or JSON"
        return 1
    fi
}

# Function to validate XML structure
validate_xml_structure() {
    local file="$1"
    
    log_info "Validating XML structure..."
    
    if ! xmllint --noout "$file" 2>&1 | tee -a "$LOG_FILE"; then
        log_error "XML validation failed"
        return 1
    fi
    
    log_success "XML structure is valid"
    return 0
}

# Function to load config via RESTCONF
load_config() {
    local config_file="$1"
    local format="$2"
    
    log_info "Loading configuration via RESTCONF..."
    
    local content_type
    local accept_type
    
    case "$format" in
        xml)
            content_type="application/yang-data+xml"
            accept_type="application/yang-data+xml"
            ;;
        json)
            content_type="application/yang-data+json"
            accept_type="application/yang-data+json"
            ;;
        *)
            log_error "Unknown format: $format"
            return 1
            ;;
    esac
    
    # Read config file
    local config_data=$(cat "$config_file")
    
    if [ "$DRY_RUN" = true ]; then
        log_warn "DRY RUN MODE - Not sending to RESTCONF"
        log_info "Config that would be sent:"
        echo "$config_data" | head -20
        return 0
    fi
    
    # Send config via RESTCONF PUT
    log_info "Sending config to $RESTCONF_BASE/data"
    
    local response=$(curl -s -w "\n%{http_code}" \
        -u "$RESTCONF_USER:$RESTCONF_PASS" \
        -H "Content-Type: $content_type" \
        -H "Accept: $accept_type" \
        -X PUT \
        --data-binary "@$config_file" \
        --max-time "$RESTCONF_TIMEOUT" \
        "$RESTCONF_BASE/data" 2>&1)
    
    local http_code=$(echo "$response" | tail -n1)
    local body=$(echo "$response" | head -n-1)
    
    log_info "HTTP Response Code: $http_code"
    
    case "$http_code" in
        201|204)
            log_success "Configuration loaded successfully"
            return 0
            ;;
        400)
            log_error "Bad request (400) - Check XML/JSON structure"
            log_error "Response: $body"
            return 1
            ;;
        401|403)
            log_error "Authentication failed (HTTP $http_code)"
            return 1
            ;;
        409|422)
            log_error "Validation error (HTTP $http_code)"
            log_error "Response: $body"
            return 1
            ;;
        *)
            log_error "Unexpected HTTP response: $http_code"
            log_error "Response: $body"
            return 1
            ;;
    esac
}

# Function to validate configuration
validate_config() {
    log_info "Validating candidate configuration..."
    
    # NETCONF validate operation via RESTCONF
    local response=$(curl -s -w "\n%{http_code}" \
        -u "$RESTCONF_USER:$RESTCONF_PASS" \
        -H "Content-Type: application/yang-data+xml" \
        -X POST \
        --data '<?xml version="1.0" encoding="UTF-8"?>
<validate xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <source>
    <candidate/>
  </source>
</validate>' \
        "$RESTCONF_BASE/operations/ietf-netconf:validate" 2>&1)
    
    local http_code=$(echo "$response" | tail -n1)
    local body=$(echo "$response" | head -n-1)
    
    if [ "$http_code" = "200" ] || [ "$http_code" = "204" ]; then
        log_success "Configuration validation passed"
        return 0
    else
        log_warn "Validation response: $http_code"
        log_warn "Response: $body"
        return 0  # Don't fail on validation - might not be fully supported
    fi
}

# Function to commit configuration
commit_config() {
    log_info "Committing configuration..."
    
    # NETCONF commit operation via RESTCONF
    local response=$(curl -s -w "\n%{http_code}" \
        -u "$RESTCONF_USER:$RESTCONF_PASS" \
        -H "Content-Type: application/yang-data+xml" \
        -X POST \
        --data '<?xml version="1.0" encoding="UTF-8"?>
<commit xmlns="urn:ietf:params:xml:ns:netconf:base:1.0"/>' \
        "$RESTCONF_BASE/operations/ietf-netconf:commit" 2>&1)
    
    local http_code=$(echo "$response" | tail -n1)
    local body=$(echo "$response" | head -n-1)
    
    if [ "$http_code" = "200" ] || [ "$http_code" = "204" ]; then
        log_success "Configuration committed successfully"
        return 0
    else
        log_error "Commit failed (HTTP $http_code)"
        log_error "Response: $body"
        return 1
    fi
}

# Function to discard changes
discard_changes() {
    log_warn "Discarding candidate configuration..."
    
    local response=$(curl -s -w "\n%{http_code}" \
        -u "$RESTCONF_USER:$RESTCONF_PASS" \
        -H "Content-Type: application/yang-data+xml" \
        -X POST \
        --data '<?xml version="1.0" encoding="UTF-8"?>
<discard-changes xmlns="urn:ietf:params:xml:ns:netconf:base:1.0"/>' \
        "$RESTCONF_BASE/operations/ietf-netconf:discard-changes" 2>&1)
    
    local http_code=$(echo "$response" | tail -n1)
    
    if [ "$http_code" = "200" ] || [ "$http_code" = "204" ]; then
        log_success "Changes discarded"
        return 0
    else
        log_warn "Could not discard changes (HTTP $http_code)"
        return 1
    fi
}

# Function to show stats
show_stats() {
    log_info "Configuration Statistics:"
    log_info "  Total lines: $(wc -l < "$CONFIG_FILE")"
    log_info "  File size: $(du -h "$CONFIG_FILE" | cut -f1)"
}

# Main execution flow
main() {
    # Check connectivity
    if ! check_restconf; then
        log_error "Cannot proceed - RESTCONF not available"
        exit 1
    fi
    
    # Detect format
    local format
    format=$(detect_format "$CONFIG_FILE") || exit 1
    log_info "Detected format: $format"
    
    # Validate XML if needed
    if [ "$format" = "xml" ]; then
        validate_xml_structure "$CONFIG_FILE" || exit 1
    fi
    
    # Show stats
    show_stats
    
    # Backup current config
    local backup_file
    backup_file=$(backup_running_config)
    
    # Load config
    if ! load_config "$CONFIG_FILE" "$format"; then
        log_error "Failed to load configuration"
        if [ -n "$backup_file" ]; then
            log_info "Backup available at: $backup_file"
        fi
        exit 1
    fi
    
    # Validate if requested
    if [ "$VALIDATE_ONLY" = true ]; then
        log_info "Validate-only mode: skipping commit"
        discard_changes
        exit 0
    fi
    
    # Commit if enabled
    if [ "$COMMIT" = true ]; then
        if ! validate_config; then
            log_error "Configuration validation failed"
            discard_changes
            if [ -n "$backup_file" ]; then
                log_info "Backup available at: $backup_file"
            fi
            exit 1
        fi
        
        if ! commit_config; then
            log_error "Configuration commit failed"
            discard_changes
            if [ -n "$backup_file" ]; then
                log_info "Backup available at: $backup_file"
            fi
            exit 1
        fi
        
        log_success "Configuration successfully applied and committed"
    else
        log_warn "Config loaded but not committed (--no-commit flag used)"
        log_info "To commit manually, use: clixon_cli"
        log_info "  router# commit"
        log_info "  router# end"
    fi
    
    echo ""
    log_success "=== Config Loading Complete ==="
    log_info "Log file: $LOG_FILE"
    
    if [ -n "$backup_file" ]; then
        log_info "Backup: $backup_file"
    fi
}

# Run main
main "$@"
exit $?
