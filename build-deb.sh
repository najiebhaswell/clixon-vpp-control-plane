#!/bin/bash
#
# build-deb.sh - Build self-contained .deb package for clixon-vpp
#
# This script builds a complete .deb package that includes:
# - CLIgen library
# - Clixon framework (backend, CLI, RESTCONF, NETCONF)
# - VPP backend plugin
# - YANG models
# - Systemd service files
#
# Target: Debian 12 (Bookworm) amd64
#

set -e

# Configuration
PACKAGE_NAME="clixon-vpp"
PACKAGE_VERSION="1.1.0"
ARCHITECTURE="amd64"
PREFIX="/opt/clixon-vpp"
MAINTAINER="VPP Control Plane Team <admin@example.com>"

# Paths
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_ROOT="${SCRIPT_DIR}/build-pkg"
STAGING_DIR="${BUILD_ROOT}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}"
CLIGEN_DIR="${BUILD_ROOT}/cligen"
CLIXON_DIR="${BUILD_ROOT}/clixon"

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

check_dependencies() {
    log_step "Checking build dependencies..."
    
    local missing=()
    
    for cmd in git make gcc autoconf automake libtool pkg-config flex bison dpkg-deb; do
        if ! command -v $cmd &> /dev/null; then
            missing+=($cmd)
        fi
    done
    
    if [ ${#missing[@]} -ne 0 ]; then
        log_error "Missing dependencies: ${missing[*]}"
        log_info "Install with: sudo apt-get install git build-essential autoconf automake libtool pkg-config flex bison dpkg-dev"
        exit 1
    fi
    
    # Check dev libraries
    local libs="libnghttp2-dev libssl-dev libcurl4-openssl-dev libxml2-dev libpcre2-dev libsystemd-dev"
    for lib in $libs; do
        if ! dpkg -l | grep -q "ii  $lib"; then
            log_warn "Package $lib may not be installed"
        fi
    done
    
    log_info "Dependencies OK"
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
    mkdir -p "${STAGING_DIR}${PREFIX}/bin"
    mkdir -p "${STAGING_DIR}${PREFIX}/sbin"
    mkdir -p "${STAGING_DIR}${PREFIX}/lib"
    mkdir -p "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/backend"
    mkdir -p "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/cli"
    mkdir -p "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/restconf"
    mkdir -p "${STAGING_DIR}${PREFIX}/share/clixon"
    mkdir -p "${STAGING_DIR}${PREFIX}/share/clixon/cli"
    mkdir -p "${STAGING_DIR}${PREFIX}/include"
    mkdir -p "${STAGING_DIR}/etc/clixon"
    mkdir -p "${STAGING_DIR}/etc/clixon/vpp.d"
    mkdir -p "${STAGING_DIR}/etc/systemd/system"
    mkdir -p "${STAGING_DIR}/etc/ld.so.conf.d"
    mkdir -p "${STAGING_DIR}/var/lib/clixon/vpp"
    mkdir -p "${STAGING_DIR}/usr/local/bin"
    mkdir -p "${STAGING_DIR}/usr/local/sbin"
    mkdir -p "${STAGING_DIR}/usr/local/lib"
    
    log_info "Staging directory prepared"
}

build_cligen() {
    log_step "Building CLIgen..."
    
    mkdir -p "${BUILD_ROOT}"
    cd "${BUILD_ROOT}"
    
    if [ ! -d "cligen" ]; then
        git clone --depth 1 https://github.com/clicon/cligen.git
    fi
    
    cd cligen
    
    if [ -f configure.ac ]; then
        autoreconf -i -f
    fi
    
    ./configure --prefix="${PREFIX}"
    make clean || true
    make -j$(nproc)
    
    # Install to staging
    make DESTDIR="${STAGING_DIR}" install
    
    log_info "CLIgen built successfully"
}

build_clixon() {
    log_step "Building Clixon..."
    
    cd "${BUILD_ROOT}"
    
    if [ ! -d "clixon" ]; then
        git clone --depth 1 https://github.com/clicon/clixon.git
    fi
    
    cd clixon
    
    # Configure with CLIgen from staging
    ./configure \
        --prefix="${PREFIX}" \
        --with-cligen="${STAGING_DIR}${PREFIX}" \
        --with-restconf=native \
        --enable-netconf-monitoring \
        --sysconfdir=/etc
    
    make clean || true
    
    # Build with correct library paths
    CFLAGS="-I${STAGING_DIR}${PREFIX}/include" \
    LDFLAGS="-L${STAGING_DIR}${PREFIX}/lib -Wl,-rpath,${PREFIX}/lib" \
    make -j$(nproc)
    
    # Install to staging
    make DESTDIR="${STAGING_DIR}" install
    
    log_info "Clixon built successfully"
}

build_vpp_plugin() {
    log_step "Building VPP plugin..."
    
    cd "${SCRIPT_DIR}"
    
    # Check if we have pre-built plugins
    if [ -f "vpp_plugin.so" ] && [ -f "vpp_cli.so" ]; then
        log_info "Using existing pre-built plugins"
        cp -f vpp_plugin.so "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/backend/"
        [ -f vpp_backend.so ] && cp -f vpp_backend.so "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/backend/"
        cp -f vpp_cli.so "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/cli/"
    else
        log_warn "Pre-built plugins not found, attempting to build..."
        
        # Try to build with correct paths
        CLIXON_INC="${STAGING_DIR}${PREFIX}/include"
        CLIXON_LIB="${STAGING_DIR}${PREFIX}/lib"
        CLIGEN_INC="${STAGING_DIR}${PREFIX}/include"
        
        make clean || true
        
        CFLAGS="-I${CLIXON_INC} -I${CLIGEN_INC} -fPIC -g -O2" \
        LDFLAGS="-L${CLIXON_LIB} -Wl,-rpath,${PREFIX}/lib" \
        CLIXON_CFLAGS="-I${CLIXON_INC} -I${CLIGEN_INC}" \
        CLIXON_LIBS="-L${CLIXON_LIB} -lclixon -lcligen" \
        make all cli 2>/dev/null || log_warn "Plugin build had warnings"
        
        if [ -f "vpp_plugin.so" ]; then
            cp -f vpp_plugin.so "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/backend/"
        fi
        if [ -f "vpp_cli_plugin.so" ]; then
            cp -f vpp_cli_plugin.so "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/cli/"
        fi
    fi
    
    log_info "VPP plugin installation complete"
}

install_yang_models() {
    log_step "Installing YANG models..."
    
    cd "${SCRIPT_DIR}"
    
    # Install custom YANG models
    cp -f yang/*.yang "${STAGING_DIR}${PREFIX}/share/clixon/"
    
    log_info "YANG models installed"
}

install_cli_specs() {
    log_step "Installing CLI specifications..."
    
    cd "${SCRIPT_DIR}"
    
    # Install CLI specs
    if [ -d cli ] && ls cli/*.cli 1>/dev/null 2>&1; then
        cp -f cli/*.cli "${STAGING_DIR}${PREFIX}/share/clixon/cli/"
    fi
    
    log_info "CLI specs installed"
}

install_configuration() {
    log_step "Installing configuration files..."
    
    cd "${SCRIPT_DIR}"
    
    # Create main config file
    cat > "${STAGING_DIR}/etc/clixon/clixon-vpp.xml" << 'EOFCONFIG'
<clixon-config xmlns="http://clicon.org/config">
  <CLICON_CONFIGFILE>/etc/clixon/clixon-vpp.xml</CLICON_CONFIGFILE>
  <CLICON_CONFIGDIR>/etc/clixon/vpp.d</CLICON_CONFIGDIR>
  <CLICON_FEATURE>ietf-netconf:startup</CLICON_FEATURE>
  <CLICON_FEATURE>clixon-restconf:allow-auth-none</CLICON_FEATURE>
  <CLICON_YANG_DIR>/opt/clixon-vpp/share/clixon</CLICON_YANG_DIR>
  <CLICON_YANG_MAIN_FILE>/opt/clixon-vpp/share/clixon/vpp-interfaces.yang</CLICON_YANG_MAIN_FILE>
  <CLICON_CLI_MODE>base</CLICON_CLI_MODE>
  <CLICON_CLISPEC_DIR>/opt/clixon-vpp/share/clixon/cli</CLICON_CLISPEC_DIR>
  <CLICON_BACKEND_DIR>/opt/clixon-vpp/lib/clixon/plugins/backend</CLICON_BACKEND_DIR>
  <CLICON_CLI_DIR>/opt/clixon-vpp/lib/clixon/plugins/cli</CLICON_CLI_DIR>
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

    # Copy helper scripts
    if [ -d scripts ]; then
        cp -f scripts/*.sh "${STAGING_DIR}${PREFIX}/bin/" 2>/dev/null || true
        chmod +x "${STAGING_DIR}${PREFIX}/bin/"*.sh 2>/dev/null || true
    fi
    
    log_info "Configuration installed"
}

install_systemd_services() {
    log_step "Installing systemd services..."
    
    # Backend service
    cat > "${STAGING_DIR}/etc/systemd/system/clixon-vpp-backend.service" << 'EOFSVC'
[Unit]
Description=Clixon VPP Backend Daemon
Documentation=https://clixon-docs.readthedocs.io/
After=network.target vpp.service
Wants=vpp.service

[Service]
Type=forking
Environment="LD_LIBRARY_PATH=/opt/clixon-vpp/lib"
ExecStart=/opt/clixon-vpp/sbin/clixon_backend -f /etc/clixon/clixon-vpp.xml -s running
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
Environment="LD_LIBRARY_PATH=/opt/clixon-vpp/lib"
ExecStart=/opt/clixon-vpp/sbin/clixon_restconf -f /etc/clixon/clixon-vpp.xml
Restart=on-failure
RestartSec=5
User=www-data
Group=clixon

[Install]
WantedBy=multi-user.target
EOFSVC

    log_info "Systemd services installed"
}

create_symlinks() {
    log_step "Creating convenience symlinks..."
    
    # Symlinks to /usr/local for convenience
    ln -sf "${PREFIX}/bin/clixon_cli" "${STAGING_DIR}/usr/local/bin/clixon_cli"
    ln -sf "${PREFIX}/sbin/clixon_backend" "${STAGING_DIR}/usr/local/sbin/clixon_backend"
    ln -sf "${PREFIX}/sbin/clixon_restconf" "${STAGING_DIR}/usr/local/sbin/clixon_restconf"
    ln -sf "${PREFIX}/sbin/clixon_netconf" "${STAGING_DIR}/usr/local/sbin/clixon_netconf"
    
    # Library search path
    echo "${PREFIX}/lib" > "${STAGING_DIR}/etc/ld.so.conf.d/clixon-vpp.conf"
    
    log_info "Symlinks created"
}

calculate_installed_size() {
    # Calculate installed size in KB
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
 .
 This package is self-contained and includes:
  - CLIgen library
  - Clixon framework (backend, CLI, RESTCONF, NETCONF)
  - VPP backend plugin
  - YANG models for VPP
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
        echo "To start the services:"
        echo "  sudo systemctl start clixon-vpp-backend"
        echo "  sudo systemctl start clixon-vpp-restconf"
        echo ""
        echo "To enable on boot:"
        echo "  sudo systemctl enable clixon-vpp-backend clixon-vpp-restconf"
        echo ""
        echo "To use CLI:"
        echo "  clixon_cli -f /etc/clixon/clixon-vpp.xml"
        echo ""
        echo "RESTCONF API available at:"
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
        rm -f /etc/ld.so.conf.d/clixon-vpp.conf
        ldconfig
        systemctl daemon-reload || true
        ;;
    remove|upgrade|failed-upgrade|abort-install|abort-upgrade|disappear)
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

build_package() {
    log_step "Building .deb package..."
    
    cd "${BUILD_ROOT}"
    
    # Set correct permissions
    find "${STAGING_DIR}" -type d -exec chmod 755 {} \;
    find "${STAGING_DIR}" -type f -exec chmod 644 {} \;
    chmod 755 "${STAGING_DIR}${PREFIX}/bin/"* 2>/dev/null || true
    chmod 755 "${STAGING_DIR}${PREFIX}/sbin/"* 2>/dev/null || true
    chmod 755 "${STAGING_DIR}${PREFIX}/lib/"*.so* 2>/dev/null || true
    chmod 755 "${STAGING_DIR}${PREFIX}/lib/clixon/plugins/"*/*.so 2>/dev/null || true
    chmod 755 "${STAGING_DIR}/DEBIAN/postinst"
    chmod 755 "${STAGING_DIR}/DEBIAN/prerm"
    chmod 755 "${STAGING_DIR}/DEBIAN/postrm"
    
    # Build the package
    dpkg-deb --build --root-owner-group "${STAGING_DIR}"
    
    # Move to project root
    mv "${STAGING_DIR}.deb" "${SCRIPT_DIR}/"
    
    log_info "Package built: ${SCRIPT_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}.deb"
}

show_summary() {
    echo ""
    echo "=============================================="
    echo -e "${GREEN}  Build Complete!${NC}"
    echo "=============================================="
    echo ""
    echo "Package: ${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}.deb"
    echo "Location: ${SCRIPT_DIR}/"
    echo ""
    echo "Package contents:"
    dpkg-deb -c "${SCRIPT_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}.deb" | head -30
    echo "..."
    echo ""
    echo "To install:"
    echo "  sudo dpkg -i ${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCHITECTURE}.deb"
    echo "  sudo apt-get install -f  # if there are dependency issues"
    echo ""
    echo "To verify:"
    echo "  dpkg -l | grep clixon-vpp"
    echo "  dpkg -L clixon-vpp"
    echo ""
}

# Main execution
main() {
    echo ""
    echo "=========================================="
    echo "  Clixon VPP .deb Package Builder"
    echo "  Target: Debian 12 (Bookworm) amd64"
    echo "=========================================="
    echo ""
    
    check_dependencies
    clean_build
    prepare_staging
    build_cligen
    build_clixon
    build_vpp_plugin
    install_yang_models
    install_cli_specs
    install_configuration
    install_systemd_services
    create_symlinks
    create_debian_control
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
        echo "Commands:"
        echo "  (no args)  - Build the .deb package"
        echo "  clean      - Remove build artifacts"
        echo "  help       - Show this help"
        ;;
    *)
        main
        ;;
esac
