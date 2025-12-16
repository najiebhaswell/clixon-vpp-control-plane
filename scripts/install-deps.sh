#!/bin/bash
#
# install-deps.sh - Install dependencies for Clixon VPP Control Plane
#
# This script installs:
# - CLIgen (CLI generator library)
# - Clixon (YANG-based configuration framework)
# - VPP development packages (if not already installed)
#
# Run as root or with sudo

set -e

# Configuration
CLIGEN_REPO="https://github.com/clicon/cligen.git"
CLIXON_REPO="https://github.com/clicon/clixon.git"
BUILD_DIR="/tmp/clixon-build"
PREFIX="/usr/local"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

install_base_deps() {
    log_info "Installing base dependencies..."
    
    apt-get update
    apt-get install -y \
        git \
        build-essential \
        autoconf \
        automake \
        libtool \
        pkg-config \
        flex \
        bison \
        libnghttp2-dev \
        libssl-dev \
        libcurl4-openssl-dev \
        libfcgi-dev \
        libxml2-dev \
        libsystemd-dev \
        nginx \
        libpcre2-dev
    
    log_info "Base dependencies installed"
}

install_cligen() {
    log_info "Installing CLIgen..."
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    if [[ -d "cligen" ]]; then
        log_info "Updating existing CLIgen source..."
        cd cligen
        git pull
    else
        log_info "Cloning CLIgen from $CLIGEN_REPO..."
        git clone "$CLIGEN_REPO"
        cd cligen
    fi
    
    # Build and install
    log_info "Building CLIgen..."
    autoreconf -i -f 2>/dev/null || true
    if [[ -f configure.ac ]]; then
        autoreconf -i
        ./configure --prefix="$PREFIX"
    else
        # Fallback for older CLIgen versions
        ./configure --prefix="$PREFIX"
    fi
    
    make clean || true
    make -j$(nproc)
    make install
    
    # Update library cache
    ldconfig
    
    log_info "CLIgen installed successfully"
}

install_clixon() {
    log_info "Installing Clixon..."
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    if [[ -d "clixon" ]]; then
        log_info "Updating existing Clixon source..."
        cd clixon
        git pull
    else
        log_info "Cloning Clixon from $CLIXON_REPO..."
        git clone "$CLIXON_REPO"
        cd clixon
    fi
    
    # Build and install
    log_info "Building Clixon..."
    autoreconf -i -f 2>/dev/null || true
    
    ./configure \
        --prefix="$PREFIX" \
        --with-cligen="$PREFIX" \
        --with-restconf=native \
        --enable-netconf-monitoring
    
    make clean || true
    make -j$(nproc)
    make install
    
    # Install YANG models
    make install-include
    
    # Update library cache
    ldconfig
    
    log_info "Clixon installed successfully"
}

install_vpp_dev() {
    log_info "Checking VPP development packages..."
    
    # Check if VPP is already installed
    if dpkg -l | grep -q "vpp-dev"; then
        log_info "VPP development packages already installed"
        return 0
    fi
    
    log_warn "VPP development packages not found"
    log_info "Please install VPP 25.06 from fd.io repository:"
    echo ""
    echo "  # Add FD.io repository"
    echo "  curl -s https://packagecloud.io/install/repositories/fdio/release/script.deb.sh | sudo bash"
    echo ""
    echo "  # Install VPP packages"
    echo "  sudo apt-get install -y vpp vpp-plugin-core vpp-dev"
    echo ""
    
    read -p "Would you like to install VPP from fd.io now? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        # Add FD.io repository
        curl -s https://packagecloud.io/install/repositories/fdio/release/script.deb.sh | bash
        
        # Install VPP packages
        apt-get install -y vpp vpp-plugin-core vpp-plugin-dpdk vpp-dev
        
        log_info "VPP installed successfully"
    else
        log_warn "Skipping VPP installation - please install manually"
    fi
}

setup_clixon_dirs() {
    log_info "Setting up Clixon directories..."
    
    # Create required directories
    mkdir -p /var/lib/clixon/vpp
    mkdir -p /var/run
    mkdir -p /etc/clixon
    mkdir -p "$PREFIX/share/clixon"
    mkdir -p "$PREFIX/lib/clixon/plugins/backend"
    mkdir -p "$PREFIX/lib/clixon/plugins/cli"
    mkdir -p "$PREFIX/lib/clixon/plugins/restconf"
    
    # Create clixon group if it doesn't exist
    if ! getent group clixon > /dev/null 2>&1; then
        groupadd clixon
        log_info "Created 'clixon' group"
    fi
    
    # Set permissions
    chown -R root:clixon /var/lib/clixon
    chmod 775 /var/lib/clixon/vpp
    
    log_info "Clixon directories configured"
}

verify_installation() {
    log_info "Verifying installation..."
    
    local errors=0
    
    # Check CLIgen
    if [[ -f "$PREFIX/lib/libcligen.so" ]]; then
        log_info "✓ CLIgen library found"
    else
        log_error "✗ CLIgen library not found"
        ((errors++))
    fi
    
    # Check Clixon
    if [[ -f "$PREFIX/bin/clixon_backend" ]]; then
        log_info "✓ Clixon backend found"
    else
        log_error "✗ Clixon backend not found"
        ((errors++))
    fi
    
    if [[ -f "$PREFIX/bin/clixon_cli" ]]; then
        log_info "✓ Clixon CLI found"
    else
        log_error "✗ Clixon CLI not found"
        ((errors++))
    fi
    
    # Check VPP
    if command -v vpp &> /dev/null; then
        local vpp_version=$(vpp --version 2>/dev/null | head -1)
        log_info "✓ VPP found: $vpp_version"
    else
        log_warn "⚠ VPP not found in PATH"
    fi
    
    # Check VPP headers
    if [[ -f "/usr/include/vapi/vapi.h" ]]; then
        log_info "✓ VPP VAPI headers found"
    else
        log_warn "⚠ VPP VAPI headers not found (install vpp-dev)"
    fi
    
    if [[ $errors -eq 0 ]]; then
        log_info "All core dependencies installed successfully!"
        return 0
    else
        log_error "Some dependencies failed to install"
        return 1
    fi
}

print_next_steps() {
    echo ""
    echo "=============================================="
    echo "  Installation Complete!"
    echo "=============================================="
    echo ""
    echo "Next steps:"
    echo ""
    echo "1. Build the VPP plugin:"
    echo "   cd $(dirname "$0")/.."
    echo "   make"
    echo ""
    echo "2. Install the plugin:"
    echo "   sudo make install"
    echo ""
    echo "3. Start VPP (if not running):"
    echo "   sudo systemctl start vpp"
    echo ""
    echo "4. Start Clixon backend:"
    echo "   sudo clixon_backend -f /etc/clixon/clixon-vpp.xml"
    echo ""
    echo "5. Start Clixon CLI:"
    echo "   clixon_cli -f /etc/clixon/clixon-vpp.xml"
    echo ""
}

# Main execution
main() {
    echo "========================================"
    echo " Clixon VPP Control Plane Dependencies"
    echo "========================================"
    echo ""
    
    check_root
    install_base_deps
    install_cligen
    install_clixon
    install_vpp_dev
    setup_clixon_dirs
    
    if verify_installation; then
        print_next_steps
    fi
}

# Run main
main "$@"
