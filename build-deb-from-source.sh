#!/bin/bash
#
# build-deb-from-source.sh - Build .deb from existing source installation
#
# Script ini membuat package .deb dari instalasi Clixon yang sudah ada di /usr/local
# Tidak perlu build ulang - langsung package binary yang sudah ada
#
# Target: Debian 12 (Bookworm) amd64
#

set -e

# Configuration
PACKAGE_NAME="clixon-vpp"
PACKAGE_VERSION="1.1.0"
ARCHITECTURE="amd64"
MAINTAINER="VPP Control Plane Team <admin@example.com>"

# Source paths (from source installation)
SRC_PREFIX="/usr/local"

# Destination paths in package
DST_PREFIX="/usr/local"

# Paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_ROOT="${SCRIPT_DIR}/build-pkg"
STAGING_DIR="${BUILD_ROOT}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

check_source_installation() {
    log_step "Checking existing Clixon installation..."
    
    local missing=0
    
    if [ ! -f "${SRC_PREFIX}/sbin/clixon_backend" ]; then
        log_error "clixon_backend not found at ${SRC_PREFIX}/sbin/"
        missing=1
    fi
    
    if [ ! -f "${SRC_PREFIX}/bin/clixon_cli" ]; then
        log_error "clixon_cli not found at ${SRC_PREFIX}/bin/"
        missing=1
    fi
    
    if [ ! -f "${SRC_PREFIX}/lib/libclixon.so" ]; then
        log_error "libclixon.so not found at ${SRC_PREFIX}/lib/"
        missing=1
    fi
    
    if [ ! -f "${SRC_PREFIX}/lib/libcligen.so" ]; then
        log_error "libcligen.so not found at ${SRC_PREFIX}/lib/"
        missing=1
    fi
    
    if [ $missing -eq 1 ]; then
        log_error "Clixon source installation not found!"
        log_info "Make sure Clixon is installed from source first."
        exit 1
    fi
    
    log_info "Clixon installation found at ${SRC_PREFIX}"
}

clean_build() {
    log_step "Cleaning previous build..."
    rm -rf "${BUILD_ROOT}"
    rm -f "${SCRIPT_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}.deb"
    log_info "Clean complete"
}

prepare_staging() {
    log_step "Preparing staging directory..."
    
    mkdir -p "${STAGING_DIR}/DEBIAN"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/bin"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/sbin"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/lib"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/backend"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/cli"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/restconf"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/share/clixon"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/share/clixon/cli"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/include/clixon"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/include/cligen"
    mkdir -p "${STAGING_DIR}${DST_PREFIX}/etc"
    mkdir -p "${STAGING_DIR}/etc/clixon"
    mkdir -p "${STAGING_DIR}/etc/clixon/vpp.d"
    mkdir -p "${STAGING_DIR}/etc/systemd/system"
    mkdir -p "${STAGING_DIR}/var/lib/clixon/vpp"
    
    log_info "Staging directory prepared"
}

copy_clixon_binaries() {
    log_step "Copying Clixon binaries..."
    
    # Copy main binaries
    cp -a "${SRC_PREFIX}/bin/clixon_cli" "${STAGING_DIR}${DST_PREFIX}/bin/" 2>/dev/null || true
    cp -a "${SRC_PREFIX}/sbin/clixon_backend" "${STAGING_DIR}${DST_PREFIX}/sbin/"
    cp -a "${SRC_PREFIX}/sbin/clixon_restconf" "${STAGING_DIR}${DST_PREFIX}/sbin/" 2>/dev/null || true
    cp -a "${SRC_PREFIX}/sbin/clixon_netconf" "${STAGING_DIR}${DST_PREFIX}/sbin/" 2>/dev/null || true
    cp -a "${SRC_PREFIX}/bin/clixon_netconf" "${STAGING_DIR}${DST_PREFIX}/bin/" 2>/dev/null || true
    
    log_info "Binaries copied"
}

copy_libraries() {
    log_step "Copying libraries..."
    
    # Copy CLIgen libraries
    cp -a "${SRC_PREFIX}/lib/libcligen"* "${STAGING_DIR}${DST_PREFIX}/lib/"
    
    # Copy Clixon libraries
    cp -a "${SRC_PREFIX}/lib/libclixon"* "${STAGING_DIR}${DST_PREFIX}/lib/"
    
    log_info "Libraries copied"
}

copy_includes() {
    log_step "Copying header files..."
    
    # Copy Clixon headers
    if [ -d "${SRC_PREFIX}/include/clixon" ]; then
        cp -a "${SRC_PREFIX}/include/clixon/"* "${STAGING_DIR}${DST_PREFIX}/include/clixon/" 2>/dev/null || true
    fi
    
    # Copy CLIgen headers
    if [ -d "${SRC_PREFIX}/include/cligen" ]; then
        cp -a "${SRC_PREFIX}/include/cligen/"* "${STAGING_DIR}${DST_PREFIX}/include/cligen/" 2>/dev/null || true
    fi
    
    # Copy individual headers
    cp -a "${SRC_PREFIX}/include/"*.h "${STAGING_DIR}${DST_PREFIX}/include/" 2>/dev/null || true
    
    log_info "Headers copied"
}

copy_yang_models() {
    log_step "Copying YANG models..."
    
    # Copy Clixon YANG models
    if [ -d "${SRC_PREFIX}/share/clixon" ]; then
        cp -a "${SRC_PREFIX}/share/clixon/"*.yang "${STAGING_DIR}${DST_PREFIX}/share/clixon/" 2>/dev/null || true
    fi
    
    # Copy our VPP YANG models
    cp -f "${SCRIPT_DIR}/yang/"*.yang "${STAGING_DIR}${DST_PREFIX}/share/clixon/"
    
    log_info "YANG models copied"
}

copy_vpp_plugins() {
    log_step "Copying VPP plugins..."
    
    # Copy from project directory
    if [ -f "${SCRIPT_DIR}/vpp_plugin.so" ]; then
        cp -f "${SCRIPT_DIR}/vpp_plugin.so" "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/backend/"
        log_info "Copied vpp_plugin.so from project"
    fi
    
    if [ -f "${SCRIPT_DIR}/vpp_backend.so" ]; then
        cp -f "${SCRIPT_DIR}/vpp_backend.so" "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/backend/"
        log_info "Copied vpp_backend.so from project"
    fi
    
    if [ -f "${SCRIPT_DIR}/vpp_cli.so" ]; then
        cp -f "${SCRIPT_DIR}/vpp_cli.so" "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/cli/"
        log_info "Copied vpp_cli.so from project"
    fi
    
    # Also copy from existing installation if present
    if [ -d "${SRC_PREFIX}/lib/clixon/plugins/backend" ]; then
        cp -a "${SRC_PREFIX}/lib/clixon/plugins/backend/"*.so "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/backend/" 2>/dev/null || true
    fi
    
    if [ -d "${SRC_PREFIX}/lib/clixon/plugins/cli" ]; then
        cp -a "${SRC_PREFIX}/lib/clixon/plugins/cli/"*.so "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/cli/" 2>/dev/null || true
    fi
    
    log_info "VPP plugins copied"
}

copy_cli_specs() {
    log_step "Copying CLI specifications..."
    
    # Copy from project
    if [ -d "${SCRIPT_DIR}/cli" ]; then
        cp -f "${SCRIPT_DIR}/cli/"*.cli "${STAGING_DIR}${DST_PREFIX}/share/clixon/cli/" 2>/dev/null || true
    fi
    
    # Copy from installation
    if [ -d "${SRC_PREFIX}/share/clixon/cli" ]; then
        cp -a "${SRC_PREFIX}/share/clixon/cli/"* "${STAGING_DIR}${DST_PREFIX}/share/clixon/cli/" 2>/dev/null || true
    fi
    
    # Copy cli plugins
    if [ -d "${SRC_PREFIX}/lib/clixon/plugins/cli" ]; then
        cp -a "${SRC_PREFIX}/lib/clixon/plugins/cli/"*.cli "${STAGING_DIR}${DST_PREFIX}/lib/clixon/plugins/cli/" 2>/dev/null || true
    fi
    
    log_info "CLI specs copied"
}

create_configuration() {
    log_step "Creating configuration files..."
    
    # Main config
    cat > "${STAGING_DIR}/etc/clixon/clixon-vpp.xml" << 'EOFCONFIG'
<clixon-config xmlns="http://clicon.org/config">
  <CLICON_CONFIGFILE>/etc/clixon/clixon-vpp.xml</CLICON_CONFIGFILE>
  <CLICON_CONFIGDIR>/etc/clixon/vpp.d</CLICON_CONFIGDIR>
  <CLICON_FEATURE>ietf-netconf:startup</CLICON_FEATURE>
  <CLICON_FEATURE>clixon-restconf:allow-auth-none</CLICON_FEATURE>
  <CLICON_YANG_DIR>/usr/local/share/clixon</CLICON_YANG_DIR>
  <CLICON_YANG_MAIN_FILE>/usr/local/share/clixon/vpp-interfaces.yang</CLICON_YANG_MAIN_FILE>
  <CLICON_CLI_MODE>base</CLICON_CLI_MODE>
  <CLICON_CLISPEC_DIR>/usr/local/share/clixon/cli</CLICON_CLISPEC_DIR>
  <CLICON_BACKEND_DIR>/usr/local/lib/clixon/plugins/backend</CLICON_BACKEND_DIR>
  <CLICON_CLI_DIR>/usr/local/lib/clixon/plugins/cli</CLICON_CLI_DIR>
  <CLICON_SOCK>/var/run/clixon-vpp.sock</CLICON_SOCK>
  <CLICON_SOCK_GROUP>clixon</CLICON_SOCK_GROUP>
  <CLICON_BACKEND_PIDFILE>/var/run/clixon-vpp.pid</CLICON_BACKEND_PIDFILE>
  <CLICON_XMLDB_DIR>/var/lib/clixon/vpp</CLICON_XMLDB_DIR>
  <CLICON_STARTUP_MODE>running</CLICON_STARTUP_MODE>
  <CLICON_NACM_MODE>disabled</CLICON_NACM_MODE>
  <CLICON_YANG_LIBRARY>false</CLICON_YANG_LIBRARY>
  <restconf>
    <enable>true</enable>
    <auth-type>none</auth-type>
    <socket>
      <namespace>default</namespace>
      <address>0.0.0.0</address>
      <port>8080</port>
      <ssl>false</ssl>
    </socket>
  </restconf>
  <autocli>
    <module-default>true</module-default>
    <list-keyword-default>kw-nokey</list-keyword-default>
    <treeref-state-default>true</treeref-state-default>
  </autocli>
</clixon-config>
EOFCONFIG

    # Also create a symlink location config
    if [ -f "${SCRIPT_DIR}/config/clixon-vpp.xml" ]; then
        cp -f "${SCRIPT_DIR}/config/clixon-vpp.xml" "${STAGING_DIR}${DST_PREFIX}/etc/"
    fi

    log_info "Configuration created"
}

create_systemd_services() {
    log_step "Creating systemd services..."
    
    # Backend service
    cat > "${STAGING_DIR}/etc/systemd/system/clixon-vpp-backend.service" << 'EOFSVC'
[Unit]
Description=Clixon VPP Backend Daemon
Documentation=https://clixon-docs.readthedocs.io/
After=network.target vpp.service
Wants=vpp.service

[Service]
Type=forking
ExecStart=/usr/local/sbin/clixon_backend -f /etc/clixon/clixon-vpp.xml -s running
ExecReload=/bin/kill -HUP $MAINPID
PIDFile=/var/run/clixon-vpp.pid
Restart=on-failure
RestartSec=5
User=root
Group=clixon

[Install]
WantedBy=multi-user.target
EOFSVC

    # RESTCONF service
    cat > "${STAGING_DIR}/etc/systemd/system/clixon-vpp-restconf.service" << 'EOFSVC'
[Unit]
Description=Clixon VPP RESTCONF Daemon
Documentation=https://clixon-docs.readthedocs.io/
After=network.target clixon-vpp-backend.service
Requires=clixon-vpp-backend.service

[Service]
Type=simple
ExecStart=/usr/local/sbin/clixon_restconf -f /etc/clixon/clixon-vpp.xml
Restart=on-failure
RestartSec=5
User=www-data
Group=clixon

[Install]
WantedBy=multi-user.target
EOFSVC

    log_info "Systemd services created"
}

copy_scripts() {
    log_step "Copying helper scripts..."
    
    if [ -d "${SCRIPT_DIR}/scripts" ]; then
        cp -f "${SCRIPT_DIR}/scripts/"*.sh "${STAGING_DIR}${DST_PREFIX}/bin/" 2>/dev/null || true
        chmod +x "${STAGING_DIR}${DST_PREFIX}/bin/"*.sh 2>/dev/null || true
        
        # Copy CLI wrapper
        if [ -f "${SCRIPT_DIR}/scripts/cli" ]; then
            cp -f "${SCRIPT_DIR}/scripts/cli" "${STAGING_DIR}${DST_PREFIX}/bin/"
            chmod +x "${STAGING_DIR}${DST_PREFIX}/bin/cli"
            log_info "CLI wrapper copied"
        fi
    fi
    
    log_info "Scripts copied"
}

calculate_installed_size() {
    du -sk "${STAGING_DIR}" | cut -f1
}

create_debian_control() {
    log_step "Creating DEBIAN control files..."
    
    local installed_size=$(calculate_installed_size)
    
    # Control file
    cat > "${STAGING_DIR}/DEBIAN/control" << EOFCTL
Package: ${PACKAGE_NAME}
Version: ${PACKAGE_VERSION}
Section: admin
Priority: optional
Architecture: ${ARCHITECTURE}
Installed-Size: ${installed_size}
Depends: libc6, libnghttp2-14, libssl3, libcurl4, libxml2, libpcre2-8-0, libsystemd0
Provides: clixon
Conflicts: clixon
Replaces: clixon
Maintainer: ${MAINTAINER}
Description: VPP Control Plane with Clixon (self-contained)
 Complete VPP control plane application with embedded Clixon framework.
 Provides NETCONF, RESTCONF, and CLI interfaces for managing VPP.
 .
 Features:
  - YANG-based configuration management
  - RESTCONF API (RFC 8040)
  - NETCONF support (RFC 6241)
  - Interactive CLI with auto-completion
  - VPP interface management
  - LCP (Linux Control Plane) support
  - Bonding/LACP configuration
EOFCTL

    # postinst script
    cat > "${STAGING_DIR}/DEBIAN/postinst" << 'EOFPOST'
#!/bin/bash
set -e

case "$1" in
    configure)
        # Create clixon group if not exists
        if ! getent group clixon > /dev/null 2>&1; then
            groupadd -r clixon
        fi
        
        # Create clixon user if not exists  
        if ! getent passwd clixon > /dev/null 2>&1; then
            useradd -r -g clixon -d /var/lib/clixon -s /usr/sbin/nologin -c "Clixon daemon" clixon 2>/dev/null || true
        fi
        
        # Add www-data to clixon group
        usermod -a -G clixon www-data 2>/dev/null || true
        
        # Set permissions
        chown -R root:clixon /var/lib/clixon 2>/dev/null || true
        chmod 775 /var/lib/clixon/vpp 2>/dev/null || true
        
        # Update library cache
        ldconfig
        
        # Reload systemd
        systemctl daemon-reload || true
        
        echo ""
        echo "=============================================="
        echo "  clixon-vpp installed successfully!"
        echo "=============================================="
        echo ""
        echo "To start services:"
        echo "  sudo systemctl start clixon-vpp-backend"
        echo "  sudo systemctl start clixon-vpp-restconf"
        echo ""
        echo "To enable on boot:"
        echo "  sudo systemctl enable clixon-vpp-backend clixon-vpp-restconf"
        echo ""
        echo "CLI (use either):"
        echo "  cli"
        echo "  clixon_cli -f /etc/clixon/clixon-vpp.xml"
        echo ""
        echo "RESTCONF:"
        echo "  http://localhost:8080/restconf/data"
        echo ""
        ;;
esac
exit 0
EOFPOST
    chmod 755 "${STAGING_DIR}/DEBIAN/postinst"

    # prerm script
    cat > "${STAGING_DIR}/DEBIAN/prerm" << 'EOFPRERM'
#!/bin/bash
set -e
case "$1" in
    remove|upgrade|deconfigure)
        systemctl stop clixon-vpp-restconf 2>/dev/null || true
        systemctl stop clixon-vpp-backend 2>/dev/null || true
        systemctl disable clixon-vpp-restconf 2>/dev/null || true
        systemctl disable clixon-vpp-backend 2>/dev/null || true
        ;;
esac
exit 0
EOFPRERM
    chmod 755 "${STAGING_DIR}/DEBIAN/prerm"

    # postrm script
    cat > "${STAGING_DIR}/DEBIAN/postrm" << 'EOFPOSTRM'
#!/bin/bash
set -e
case "$1" in
    purge)
        rm -rf /etc/clixon
        rm -rf /var/lib/clixon
        ldconfig
        systemctl daemon-reload || true
        ;;
    remove|upgrade)
        ldconfig
        ;;
esac
exit 0
EOFPOSTRM
    chmod 755 "${STAGING_DIR}/DEBIAN/postrm"

    # conffiles
    cat > "${STAGING_DIR}/DEBIAN/conffiles" << 'EOFCONF'
/etc/clixon/clixon-vpp.xml
/etc/systemd/system/clixon-vpp-backend.service
/etc/systemd/system/clixon-vpp-restconf.service
EOFCONF

    log_info "DEBIAN control files created"
}

set_permissions() {
    log_step "Setting file permissions..."
    
    # Directories
    find "${STAGING_DIR}" -type d -exec chmod 755 {} \;
    
    # Regular files
    find "${STAGING_DIR}" -type f -exec chmod 644 {} \;
    
    # Executables
    chmod 755 "${STAGING_DIR}${DST_PREFIX}/bin/"* 2>/dev/null || true
    chmod 755 "${STAGING_DIR}${DST_PREFIX}/sbin/"* 2>/dev/null || true
    
    # Libraries
    find "${STAGING_DIR}${DST_PREFIX}/lib" -name "*.so*" -exec chmod 755 {} \; 2>/dev/null || true
    
    # DEBIAN scripts
    chmod 755 "${STAGING_DIR}/DEBIAN/postinst"
    chmod 755 "${STAGING_DIR}/DEBIAN/prerm"
    chmod 755 "${STAGING_DIR}/DEBIAN/postrm"
    
    log_info "Permissions set"
}

build_package() {
    log_step "Building .deb package..."
    
    cd "${BUILD_ROOT}"
    
    # Build the package
    dpkg-deb --build --root-owner-group "${STAGING_DIR}"
    
    # Move to project root
    mv "${STAGING_DIR}.deb" "${SCRIPT_DIR}/"
    
    local deb_file="${SCRIPT_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}.deb"
    
    log_info "Package built: ${deb_file}"
    
    # Show package info
    echo ""
    echo "Package info:"
    dpkg-deb -I "${deb_file}"
}

show_summary() {
    local deb_file="${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}.deb"
    
    echo ""
    echo "=============================================="
    echo -e "${GREEN}  Build Complete!${NC}"
    echo "=============================================="
    echo ""
    echo "Package: ${deb_file}"
    echo "Location: ${SCRIPT_DIR}/"
    echo ""
    echo "Package contents (first 30 lines):"
    dpkg-deb -c "${SCRIPT_DIR}/${deb_file}" | head -30
    echo "..."
    echo ""
    echo "To install:"
    echo "  sudo dpkg -i ${deb_file}"
    echo "  sudo apt-get install -f  # jika ada dependency issues"
    echo ""
    echo "To reinstall/upgrade:"
    echo "  sudo dpkg -i --force-overwrite ${deb_file}"
    echo ""
}

# Main execution
main() {
    echo ""
    echo "=========================================="
    echo "  Clixon VPP .deb Package Builder"
    echo "  (From Existing Source Installation)"
    echo "  Target: Debian 12 (Bookworm) amd64"
    echo "=========================================="
    echo ""
    
    check_source_installation
    clean_build
    prepare_staging
    copy_clixon_binaries
    copy_libraries
    copy_includes
    copy_yang_models
    copy_vpp_plugins
    copy_cli_specs
    create_configuration
    create_systemd_services
    copy_scripts
    create_debian_control
    set_permissions
    build_package
    show_summary
}

# Parse arguments
case "${1:-}" in
    clean)
        clean_build
        ;;
    help|--help|-h)
        echo "Usage: $0 [clean|help]"
        echo ""
        echo "Script ini mem-package instalasi Clixon yang sudah ada"
        echo "dari /usr/local ke dalam file .deb"
        echo ""
        echo "Commands:"
        echo "  (no args)  - Build .deb package"
        echo "  clean      - Hapus build artifacts"
        echo "  help       - Tampilkan help ini"
        ;;
    *)
        main
        ;;
esac
