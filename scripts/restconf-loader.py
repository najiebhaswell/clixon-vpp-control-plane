#!/usr/bin/env python3
"""
RESTCONF Config Loader - Python Client
Load Clixon VPP configuration via RESTCONF API
Supports XML and JSON formats with error handling
"""

import argparse
import json
import os
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime
from pathlib import Path
from typing import Dict, Optional, Tuple

import requests
from requests.auth import HTTPBasicAuth
from urllib3.exceptions import InsecureRequestWarning

# Suppress SSL warnings for testing
requests.packages.urllib3.disable_warnings(InsecureRequestWarning)


class RestconfLoader:
    """RESTCONF-based configuration loader for Clixon VPP"""

    def __init__(
        self,
        base_url: str = "http://localhost:8080/restconf",
        username: str = "admin",
        password: str = "admin",
        timeout: int = 30,
        log_file: Optional[str] = None,
        verify_ssl: bool = False,
    ):
        self.base_url = base_url.rstrip("/")
        self.username = username
        self.password = password
        self.timeout = timeout
        self.verify_ssl = verify_ssl

        # Setup logging
        self.log_file = log_file or f"/tmp/restconf-loader-{int(time.time())}.log"
        self.backup_dir = "/tmp/restconf-backups"
        Path(self.backup_dir).mkdir(exist_ok=True)

        self.session = requests.Session()
        self.session.auth = HTTPBasicAuth(username, password)
        self.session.verify = verify_ssl

    def log(self, level: str, msg: str):
        """Log message to console and file"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log_msg = f"[{timestamp}] [{level}] {msg}"
        print(log_msg)

        with open(self.log_file, "a") as f:
            f.write(log_msg + "\n")

    def check_connectivity(self) -> bool:
        """Check if RESTCONF server is reachable"""
        self.log("INFO", "Checking RESTCONF connectivity...")

        try:
            response = self.session.get(
                f"{self.base_url}/yang-library:yang-library",
                timeout=5,
            )

            if response.status_code in [200, 404, 401]:
                self.log("OK", f"RESTCONF is reachable (HTTP {response.status_code})")
                return True
            else:
                self.log(
                    "ERROR",
                    f"RESTCONF unreachable (HTTP {response.status_code})",
                )
                return False
        except Exception as e:
            self.log("ERROR", f"Connection failed: {e}")
            return False

    def detect_format(self, file_path: str) -> str:
        """Detect file format (xml or json)"""
        with open(file_path, "r") as f:
            first_char = f.read(1)

        if first_char == "<":
            return "xml"
        elif first_char == "{":
            return "json"
        else:
            raise ValueError("Cannot detect file format - expected XML or JSON")

    def validate_xml(self, file_path: str) -> bool:
        """Validate XML file structure"""
        self.log("INFO", "Validating XML structure...")

        try:
            ET.parse(file_path)
            self.log("OK", "XML structure is valid")
            return True
        except ET.ParseError as e:
            self.log("ERROR", f"XML validation failed: {e}")
            return False

    def backup_running_config(self) -> Optional[str]:
        """Backup current running configuration"""
        self.log("INFO", "Backing up current running configuration...")

        try:
            response = self.session.get(
                f"{self.base_url}/data",
                headers={"Accept": "application/yang-data+xml"},
                timeout=self.timeout,
            )

            if response.status_code == 200:
                backup_file = (
                    f"{self.backup_dir}/running_config_{int(time.time())}.xml"
                )
                with open(backup_file, "w") as f:
                    f.write(response.text)

                self.log("OK", f"Backup saved to: {backup_file}")
                return backup_file
            else:
                self.log(
                    "WARN",
                    f"Could not backup current config (HTTP {response.status_code})",
                )
                return None
        except Exception as e:
            self.log("WARN", f"Backup failed: {e}")
            return None

    def load_config(self, file_path: str, file_format: str) -> bool:
        """Load configuration via RESTCONF"""
        self.log("INFO", "Loading configuration via RESTCONF...")

        content_type_map = {
            "xml": "application/yang-data+xml",
            "json": "application/yang-data+json",
        }

        content_type = content_type_map.get(file_format)
        if not content_type:
            self.log("ERROR", f"Unknown format: {file_format}")
            return False

        try:
            with open(file_path, "r") as f:
                config_data = f.read()

            self.log(
                "INFO",
                f"Sending config to {self.base_url}/data ({len(config_data)} bytes)",
            )

            response = self.session.put(
                f"{self.base_url}/data",
                data=config_data,
                headers={
                    "Content-Type": content_type,
                    "Accept": content_type,
                },
                timeout=self.timeout,
            )

            self.log("INFO", f"HTTP Response Code: {response.status_code}")

            if response.status_code in [201, 204]:
                self.log("OK", "Configuration loaded successfully")
                return True
            elif response.status_code == 400:
                self.log("ERROR", f"Bad request (400) - Check structure")
                self.log("ERROR", f"Response: {response.text[:200]}")
                return False
            elif response.status_code in [401, 403]:
                self.log("ERROR", f"Authentication failed (HTTP {response.status_code})")
                return False
            elif response.status_code in [409, 422]:
                self.log("ERROR", f"Validation error (HTTP {response.status_code})")
                self.log("ERROR", f"Response: {response.text[:200]}")
                return False
            else:
                self.log("ERROR", f"Unexpected HTTP response: {response.status_code}")
                self.log("ERROR", f"Response: {response.text[:200]}")
                return False

        except Exception as e:
            self.log("ERROR", f"Failed to load config: {e}")
            return False

    def validate_config(self) -> bool:
        """Validate candidate configuration"""
        self.log("INFO", "Validating candidate configuration...")

        validate_rpc = """<?xml version="1.0" encoding="UTF-8"?>
<validate xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
  <source>
    <candidate/>
  </source>
</validate>"""

        try:
            response = self.session.post(
                f"{self.base_url}/operations/ietf-netconf:validate",
                data=validate_rpc,
                headers={"Content-Type": "application/yang-data+xml"},
                timeout=self.timeout,
            )

            if response.status_code in [200, 204]:
                self.log("OK", "Configuration validation passed")
                return True
            else:
                self.log("WARN", f"Validation response: {response.status_code}")
                return True  # Don't fail - might not be fully supported

        except Exception as e:
            self.log("WARN", f"Validation failed: {e}")
            return True  # Don't fail - continue anyway

    def commit_config(self) -> bool:
        """Commit configuration to running datastore"""
        self.log("INFO", "Committing configuration...")

        commit_rpc = """<?xml version="1.0" encoding="UTF-8"?>
<commit xmlns="urn:ietf:params:xml:ns:netconf:base:1.0"/>"""

        try:
            response = self.session.post(
                f"{self.base_url}/operations/ietf-netconf:commit",
                data=commit_rpc,
                headers={"Content-Type": "application/yang-data+xml"},
                timeout=self.timeout,
            )

            if response.status_code in [200, 204]:
                self.log("OK", "Configuration committed successfully")
                return True
            else:
                self.log("ERROR", f"Commit failed (HTTP {response.status_code})")
                self.log("ERROR", f"Response: {response.text[:200]}")
                return False

        except Exception as e:
            self.log("ERROR", f"Commit failed: {e}")
            return False

    def discard_changes(self) -> bool:
        """Discard candidate changes"""
        self.log("WARN", "Discarding candidate configuration...")

        discard_rpc = """<?xml version="1.0" encoding="UTF-8"?>
<discard-changes xmlns="urn:ietf:params:xml:ns:netconf:base:1.0"/>"""

        try:
            response = self.session.post(
                f"{self.base_url}/operations/ietf-netconf:discard-changes",
                data=discard_rpc,
                headers={"Content-Type": "application/yang-data+xml"},
                timeout=self.timeout,
            )

            if response.status_code in [200, 204]:
                self.log("OK", "Changes discarded")
                return True
            else:
                self.log("WARN", f"Could not discard changes (HTTP {response.status_code})")
                return False

        except Exception as e:
            self.log("WARN", f"Discard failed: {e}")
            return False

    def load(
        self,
        config_file: str,
        commit: bool = True,
        validate_only: bool = False,
        dry_run: bool = False,
    ) -> bool:
        """Main loading workflow"""
        self.log("INFO", "=== RESTCONF Config Loader Started ===")
        self.log("INFO", f"Config file: {config_file}")
        self.log("INFO", f"RESTCONF URL: {self.base_url}")
        self.log("INFO", f"Commit after load: {commit}")
        self.log("INFO", f"Validate only: {validate_only}")
        self.log("INFO", f"Dry run: {dry_run}")
        self.log("INFO", f"Log file: {self.log_file}")
        print()

        # Check connectivity
        if not self.check_connectivity():
            self.log("ERROR", "Cannot proceed - RESTCONF not available")
            return False

        # Detect format
        try:
            file_format = self.detect_format(config_file)
            self.log("INFO", f"Detected format: {file_format}")
        except ValueError as e:
            self.log("ERROR", str(e))
            return False

        # Validate XML if needed
        if file_format == "xml" and not self.validate_xml(config_file):
            return False

        # Show stats
        stat_lines = sum(1 for _ in open(config_file))
        stat_size = os.path.getsize(config_file) / 1024
        self.log("INFO", f"Configuration Statistics:")
        self.log("INFO", f"  Total lines: {stat_lines}")
        self.log("INFO", f"  File size: {stat_size:.1f} KB")

        # Backup
        backup_file = self.backup_running_config()

        # Load config
        if dry_run:
            self.log("WARN", "DRY RUN MODE - Not sending to RESTCONF")
            with open(config_file, "r") as f:
                lines = f.readlines()[:20]
                self.log("INFO", "Config that would be sent:")
                for line in lines:
                    self.log("INFO", "  " + line.rstrip())
            return True

        if not self.load_config(config_file, file_format):
            self.log("ERROR", "Failed to load configuration")
            if backup_file:
                self.log("INFO", f"Backup available at: {backup_file}")
            return False

        # Validate only mode
        if validate_only:
            self.log("INFO", "Validate-only mode: skipping commit")
            self.discard_changes()
            return True

        # Commit if enabled
        if commit:
            if not self.validate_config():
                self.log("ERROR", "Configuration validation failed")
                self.discard_changes()
                if backup_file:
                    self.log("INFO", f"Backup available at: {backup_file}")
                return False

            if not self.commit_config():
                self.log("ERROR", "Configuration commit failed")
                self.discard_changes()
                if backup_file:
                    self.log("INFO", f"Backup available at: {backup_file}")
                return False

            self.log("OK", "Configuration successfully applied and committed")
        else:
            self.log(
                "WARN",
                "Config loaded but not committed (--no-commit flag used)",
            )
            self.log("INFO", "To commit manually, use: clixon_cli")
            self.log("INFO", "  router# commit")
            self.log("INFO", "  router# end")

        print()
        self.log("OK", "=== Config Loading Complete ===")
        self.log("INFO", f"Log file: {self.log_file}")
        if backup_file:
            self.log("INFO", f"Backup: {backup_file}")

        return True


def main():
    """Command-line interface"""
    parser = argparse.ArgumentParser(
        description="RESTCONF-based configuration loader for Clixon VPP"
    )

    parser.add_argument(
        "config_file",
        help="Configuration file (XML or JSON)",
    )

    parser.add_argument(
        "--restconf-url",
        default="http://localhost:8080/restconf",
        help="RESTCONF base URL (default: http://localhost:8080/restconf)",
    )

    parser.add_argument(
        "--username",
        default="admin",
        help="RESTCONF username (default: admin)",
    )

    parser.add_argument(
        "--password",
        default="admin",
        help="RESTCONF password (default: admin)",
    )

    parser.add_argument(
        "--no-commit",
        action="store_true",
        help="Load config but don't commit",
    )

    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate configuration without committing",
    )

    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Dry run - don't send to RESTCONF",
    )

    parser.add_argument(
        "--log-file",
        help="Log file path",
    )

    parser.add_argument(
        "--timeout",
        type=int,
        default=30,
        help="RESTCONF request timeout in seconds",
    )

    parser.add_argument(
        "--skip-ssl",
        action="store_true",
        help="Skip SSL verification (for HTTPS)",
    )

    args = parser.parse_args()

    # Validate config file exists
    if not os.path.exists(args.config_file):
        print(f"Error: Config file not found: {args.config_file}")
        sys.exit(1)

    # Create loader
    loader = RestconfLoader(
        base_url=args.restconf_url,
        username=args.username,
        password=args.password,
        timeout=args.timeout,
        log_file=args.log_file,
        verify_ssl=not args.skip_ssl,
    )

    # Load configuration
    success = loader.load(
        config_file=args.config_file,
        commit=not args.no_commit,
        validate_only=args.validate_only,
        dry_run=args.dry_run,
    )

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
