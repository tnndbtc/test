#!/usr/bin/env python3
"""
Functional test framework for Blockweave REST daemon.

This module provides utilities for starting, stopping, and interacting
with a local blockweave node for functional testing.
"""

import os
import sys
import time
import subprocess
import requests
import json
import logging
import tempfile
import shutil
from pathlib import Path


class BlockweaveNode:
    """
    Manages a blockweave node process for testing.

    Provides methods to start, stop, and query a local blockweave node.
    """

    def __init__(self, project_root=None, port=28443, config_file=None, datadir=None, node_index=0):
        """
        Initialize the node manager.

        Args:
            project_root: Path to project root directory (auto-detected if None)
            port: REST API port (default: 28443)
            config_file: Path to config file (default: blockweave.conf in project root)
            datadir: Data directory for this node (default: auto-generated temp directory)
            node_index: Node index for logging (default: 0, creates node0 folder)
        """
        if project_root is None:
            # Auto-detect project root
            # If running from build/test/functional, go up 3 levels to project root
            # If running from test/functional, go up 2 levels to project root
            test_file_path = Path(__file__).resolve()

            # Check if we're in build/test/functional
            if test_file_path.parent.parent.name == "test" and test_file_path.parent.parent.parent.name == "build":
                # Running from build/test/functional -> go up 4 levels
                self.project_root = test_file_path.parent.parent.parent.parent
            else:
                # Running from test/functional -> go up 2 levels
                self.project_root = test_file_path.parent.parent.parent
        else:
            self.project_root = Path(project_root).resolve()

        self.port = port
        self.config_file = config_file or (self.project_root / "blockweave.conf")

        # Check if daemon_cli exists in current directory (when running from build/)
        if (Path.cwd() / "daemon_cli").exists():
            self.daemon_cli = Path.cwd() / "daemon_cli"
        else:
            self.daemon_cli = self.project_root / "build" / "daemon_cli"
        self.base_url = f"http://localhost:{self.port}"
        self.process = None
        self.node_index = node_index
        self.datadir = datadir
        self.logger = logging.getLogger(f"node{node_index}")
        self.custom_config_file = None

    def create_custom_config(self):
        """
        Create a custom config file with node-specific settings.

        Returns:
            Path: Path to the custom config file
        """
        if not self.config_file.exists():
            raise FileNotFoundError(
                f"Base config file not found at {self.config_file}. "
                "Please create blockweave.conf in project root."
            )

        # Read the base config
        with open(self.config_file, 'r') as f:
            config_lines = f.readlines()

        # Create custom config with node-specific settings
        if self.datadir:
            node_dir = Path(self.datadir)
            log_dir = node_dir / "logs"
            log_dir.mkdir(parents=True, exist_ok=True)

            # Create custom config file in the node directory
            custom_config_path = node_dir / f"node{self.node_index}.conf"

            with open(custom_config_path, 'w') as f:
                for line in config_lines:
                    # Override log_dir and data_dir settings
                    if line.strip().startswith('log_dir='):
                        f.write(f"log_dir={log_dir}\n")
                    elif line.strip().startswith('data_dir='):
                        f.write(f"data_dir={node_dir / 'data'}\n")
                    else:
                        f.write(line)

            self.logger.info(f"Created custom config at {custom_config_path}")
            self.logger.info(f"Log directory: {log_dir}")
            return custom_config_path

        return self.config_file

    def start(self, timeout=10):
        """
        Start the blockweave node.

        Args:
            timeout: Maximum time to wait for node to start (seconds)

        Returns:
            bool: True if node started successfully, False otherwise
        """
        if not self.daemon_cli.exists():
            raise FileNotFoundError(
                f"daemon_cli not found at {self.daemon_cli}. "
                "Please build the project first: cd build && make"
            )

        if not self.config_file.exists():
            raise FileNotFoundError(
                f"Config file not found at {self.config_file}. "
                "Please create blockweave.conf in project root."
            )

        # Create custom config with node-specific log directory
        self.custom_config_file = self.create_custom_config()

        self.logger.info(f"Starting blockweave node on port {self.port}...")
        self.logger.info(f"Using config: {self.custom_config_file}")
        print(f"Starting blockweave node on port {self.port}...")

        # Start the daemon with custom config
        try:
            cmd = [str(self.daemon_cli), "start"]
            if self.custom_config_file != self.config_file:
                cmd.extend(["-c", str(self.custom_config_file)])

            result = subprocess.run(
                cmd,
                cwd=str(self.project_root),
                capture_output=True,
                text=True,
                timeout=timeout
            )

            if result.returncode != 0:
                error_msg = f"Failed to start daemon: {result.stderr}"
                self.logger.error(error_msg)
                print(error_msg)
                return False

            # Wait for node to be ready
            start_time = time.time()
            while time.time() - start_time < timeout:
                if self.is_ready():
                    success_msg = f"Node started successfully (PID from daemon)"
                    self.logger.info(success_msg)
                    print(success_msg)
                    return True
                time.sleep(0.5)

            timeout_msg = "Timeout waiting for node to become ready"
            self.logger.error(timeout_msg)
            print(timeout_msg)
            self.stop()
            return False

        except subprocess.TimeoutExpired:
            error_msg = f"Timeout starting daemon after {timeout} seconds"
            self.logger.error(error_msg)
            print(error_msg)
            return False
        except Exception as e:
            error_msg = f"Error starting node: {e}"
            self.logger.error(error_msg)
            print(error_msg)
            return False

    def stop(self, timeout=10):
        """
        Stop the blockweave node.

        Args:
            timeout: Maximum time to wait for node to stop (seconds)

        Returns:
            bool: True if node stopped successfully, False otherwise
        """
        print("Stopping blockweave node...")

        try:
            result = subprocess.run(
                [str(self.daemon_cli), "stop"],
                cwd=str(self.project_root),
                capture_output=True,
                text=True,
                timeout=timeout
            )

            if result.returncode != 0:
                print(f"Warning: daemon_cli stop returned non-zero: {result.stderr}")

            # Wait for node to stop responding
            start_time = time.time()
            while time.time() - start_time < timeout:
                if not self.is_ready():
                    print("Node stopped successfully")
                    return True
                time.sleep(0.5)

            print("Warning: Node may still be running after stop command")
            return False

        except Exception as e:
            print(f"Error stopping node: {e}")
            return False

    def is_ready(self):
        """
        Check if the node is ready to accept requests.

        Returns:
            bool: True if node is ready, False otherwise
        """
        try:
            response = requests.get(f"{self.base_url}/chain", timeout=1)
            return response.status_code == 200
        except:
            return False

    def get(self, endpoint, timeout=5):
        """
        Make a GET request to the node.

        Args:
            endpoint: API endpoint (e.g., "/chain")
            timeout: Request timeout in seconds

        Returns:
            requests.Response: The response object
        """
        url = f"{self.base_url}{endpoint}"
        return requests.get(url, timeout=timeout)

    def post(self, endpoint, data=None, json_data=None, timeout=5):
        """
        Make a POST request to the node.

        Args:
            endpoint: API endpoint (e.g., "/transaction")
            data: Request body data
            json_data: JSON data to send
            timeout: Request timeout in seconds

        Returns:
            requests.Response: The response object
        """
        url = f"{self.base_url}{endpoint}"
        return requests.post(url, data=data, json=json_data, timeout=timeout)

    def __enter__(self):
        """Context manager entry - start the node."""
        if not self.start():
            raise RuntimeError("Failed to start blockweave node")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit - stop the node."""
        self.stop()


class TestFramework:
    """
    Base class for functional tests.

    Provides common test utilities and setup/teardown functionality.
    """

    def __init__(self):
        """Initialize the test framework."""
        self.node = None
        self.num_success = 0
        self.num_failed = 0
        self.tmpdir = None
        self.nocleanup = False
        self.nodes = []
        self.node_counter = 0
        self.init_tmpdir()
        self.setup_logging()

    def init_tmpdir(self):
        """Initialize tmpdir from environment or create temporary directory."""
        tmpdir = os.environ.get("TEST_TMPDIR")
        if tmpdir:
            self.tmpdir = Path(tmpdir)
            self.tmpdir.mkdir(parents=True, exist_ok=True)
        else:
            self.tmpdir = Path(tempfile.mkdtemp(prefix="blockweave_test_"))

        self.nocleanup = bool(os.environ.get("TEST_NOCLEANUP"))

    def setup_logging(self):
        """Setup logging to test_framework.log in tmpdir."""
        if not self.tmpdir:
            return

        log_file = self.tmpdir / "test_framework.log"

        # Configure root logger
        logging.basicConfig(
            level=logging.DEBUG,
            format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            handlers=[
                logging.FileHandler(log_file),
                logging.StreamHandler(sys.stdout)
            ]
        )

        self.logger = logging.getLogger("test_framework")
        self.logger.info(f"Test framework initialized in {self.tmpdir}")

    def add_node(self, port=28443, **kwargs):
        """
        Create and add a new BlockweaveNode for testing.

        Args:
            port: REST API port for the node
            **kwargs: Additional arguments passed to BlockweaveNode

        Returns:
            BlockweaveNode: The created node instance
        """
        node_dir = self.tmpdir / f"node{self.node_counter}"
        node_dir.mkdir(parents=True, exist_ok=True)

        node = BlockweaveNode(
            port=port,
            datadir=str(node_dir),
            node_index=self.node_counter,
            **kwargs
        )

        self.nodes.append(node)
        self.node_counter += 1

        return node

    def setup(self):
        """
        Setup before running tests.

        Override this method in test classes for custom setup.
        """
        pass

    def run_test(self):
        """
        Run the actual test.

        Override this method in test classes to implement test logic.
        """
        raise NotImplementedError("Subclasses must implement run_test()")

    def cleanup(self):
        """
        Cleanup after running tests.

        Override this method in test classes for custom cleanup.
        """
        pass

    def assert_equal(self, actual, expected, message=""):
        """Assert that two values are equal."""
        if actual == expected:
            self.num_success += 1
            print(f"✓ PASS: {message or f'{actual} == {expected}'}")
        else:
            self.num_failed += 1
            print(f"✗ FAIL: {message or f'{actual} != {expected}'}")
            print(f"  Expected: {expected}")
            print(f"  Actual:   {actual}")

    def assert_true(self, condition, message=""):
        """Assert that a condition is true."""
        if condition:
            self.num_success += 1
            print(f"✓ PASS: {message or 'condition is True'}")
        else:
            self.num_failed += 1
            print(f"✗ FAIL: {message or 'condition is False'}")

    def assert_in(self, item, container, message=""):
        """Assert that an item is in a container."""
        if item in container:
            self.num_success += 1
            print(f"✓ PASS: {message or f'{item} in {container}'}")
        else:
            self.num_failed += 1
            print(f"✗ FAIL: {message or f'{item} not in {container}'}")

    def log_info(self, message):
        """Log an informational message."""
        print(f"ℹ INFO: {message}")
        if hasattr(self, 'logger'):
            self.logger.info(message)

    def main(self):
        """Main test execution flow."""
        print(f"\n{'='*70}")
        print(f"Running: {self.__class__.__name__}")
        print(f"{'='*70}\n")

        if self.tmpdir:
            print(f"Test tmpdir: {self.tmpdir}")
            self.log_info(f"Test tmpdir: {self.tmpdir}")

        try:
            # Setup
            self.log_info("Setting up test environment...")
            self.setup()

            # Run test
            self.log_info("Running test...")
            self.run_test()

        except Exception as e:
            print(f"\n✗ TEST FAILED WITH EXCEPTION: {e}")
            import traceback
            traceback.print_exc()
            if hasattr(self, 'logger'):
                self.logger.error(f"Test failed with exception: {e}", exc_info=True)
            self.num_failed += 1

        finally:
            # Cleanup
            self.log_info("Cleaning up...")
            self.cleanup()

            # Stop all nodes
            for node in self.nodes:
                try:
                    node.stop()
                except Exception as e:
                    print(f"Warning: Failed to stop node{node.node_index}: {e}")

            # Print summary
            print(f"\n{'='*70}")
            print(f"TEST SUMMARY")
            print(f"{'='*70}")
            print(f"Passed: {self.num_success}")
            print(f"Failed: {self.num_failed}")
            print(f"Total:  {self.num_success + self.num_failed}")

            if self.num_failed == 0:
                print(f"\n✓ ALL TESTS PASSED\n")
                result = 0
            else:
                print(f"\n✗ SOME TESTS FAILED\n")
                result = 1

            # Cleanup tmpdir if not preserving
            if not self.nocleanup and self.tmpdir and self.tmpdir.exists():
                try:
                    shutil.rmtree(self.tmpdir)
                    self.log_info(f"Cleaned up tmpdir: {self.tmpdir}")
                except Exception as e:
                    print(f"Warning: Failed to cleanup tmpdir {self.tmpdir}: {e}")
            elif self.nocleanup:
                print(f"\nTest data preserved in: {self.tmpdir}")

            return result
