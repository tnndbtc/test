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

    def __init__(self, project_root=None, port=28443, p2p_port=None, config_file=None, datadir=None, node_index=0):
        """
        Initialize the node manager.

        Args:
            project_root: Path to project root directory (auto-detected if None)
            port: REST API port (default: 28443)
            p2p_port: P2P network port (default: None, uses default from config)
            config_file: Path to config file (default: blockweave.conf in project root)
            datadir: Data directory for this node (default: auto-generated temp directory)
            node_index: Node index for logging (default: 0, creates node0 folder)
        """
        if project_root is None:
            # Auto-detect project root
            # test_file_path is .../test_framework/__init__.py
            # From build/test/functional/test_framework/__init__.py -> go up 4 levels to project root
            # From test/functional/test_framework/__init__.py -> go up 3 levels to project root
            test_file_path = Path(__file__).resolve()

            # Check if we're in build/test/functional/test_framework
            # Navigate up: test_framework -> functional -> test -> build -> project_root
            if "build" in test_file_path.parts and "test" in test_file_path.parts:
                # Find the "build" directory in the path and go up one level
                # Path is like: /path/to/project/build/test/functional/test_framework/__init__.py
                build_index = test_file_path.parts.index("build")
                self.project_root = Path(*test_file_path.parts[:build_index])
            else:
                # Running from test/functional/test_framework -> go up 3 levels
                # Path is like: /path/to/project/test/functional/test_framework/__init__.py
                self.project_root = test_file_path.parent.parent.parent
        else:
            self.project_root = Path(project_root).resolve()

        self.port = port
        self.p2p_port = p2p_port
        self.config_file = config_file or (self.project_root / "blockweave.conf")

        # Locate rest_daemon executable
        if (Path.cwd() / "rest_daemon").exists():
            self.rest_daemon = Path.cwd() / "rest_daemon"
        else:
            self.rest_daemon = self.project_root / "build" / "rest_daemon"

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
                p2p_port_written = False
                for line in config_lines:
                    # Override log_dir, data_dir, rest_api_port, p2p_port, and daemon settings
                    if line.strip().startswith('log_dir='):
                        f.write(f"log_dir={log_dir}\n")
                    elif line.strip().startswith('data_dir='):
                        f.write(f"data_dir={node_dir / 'data'}\n")
                    elif line.strip().startswith('rest_api_port='):
                        f.write(f"rest_api_port={self.port}\n")
                    elif line.strip().startswith('p2p_port='):
                        if self.p2p_port is not None:
                            f.write(f"p2p_port={self.p2p_port}\n")
                            p2p_port_written = True
                        else:
                            f.write(line)
                    elif line.strip().startswith('daemon='):
                        f.write("daemon=false\n")  # Force foreground mode for tests
                    else:
                        f.write(line)

                # Add p2p_port if it wasn't in the config and we have a value
                if self.p2p_port is not None and not p2p_port_written:
                    f.write(f"\n# P2P port (added by test framework)\n")
                    f.write(f"p2p_port={self.p2p_port}\n")

            self.logger.info(f"Created custom config at {custom_config_path}")
            self.logger.info(f"Log directory: {log_dir}")
            if self.p2p_port is not None:
                self.logger.info(f"P2P port: {self.p2p_port}")
            return custom_config_path

        return self.config_file

    def start(self, timeout=10):
        """
        Start the blockweave node in foreground mode.

        Args:
            timeout: Maximum time to wait for node to start (seconds)

        Returns:
            bool: True if node started successfully, False otherwise
        """
        if not self.rest_daemon.exists():
            raise FileNotFoundError(
                f"rest_daemon not found at {self.rest_daemon}. "
                "Please build the project first: cd build && make"
            )

        if not self.config_file.exists():
            raise FileNotFoundError(
                f"Config file not found at {self.config_file}. "
                "Please create blockweave.conf in project root."
            )

        # Create custom config with node-specific settings
        self.custom_config_file = self.create_custom_config()

        self.logger.info(f"Starting blockweave node on port {self.port}...")
        self.logger.info(f"Using config: {self.custom_config_file}")
        print(f"Starting blockweave node on port {self.port}...")

        # Start rest_daemon in foreground mode as a subprocess
        try:
            cmd = [str(self.rest_daemon), "-c", str(self.custom_config_file)]

            # Redirect stdout/stderr to log files in node directory
            node_dir = Path(self.datadir) if self.datadir else Path(tempfile.mkdtemp())
            stdout_log = node_dir / f"node{self.node_index}_stdout.log"
            stderr_log = node_dir / f"node{self.node_index}_stderr.log"

            with open(stdout_log, 'w') as stdout_f, open(stderr_log, 'w') as stderr_f:
                self.process = subprocess.Popen(
                    cmd,
                    cwd=str(self.project_root),
                    stdout=stdout_f,
                    stderr=stderr_f,
                    start_new_session=True  # Detach from terminal
                )

            self.logger.info(f"Started rest_daemon process (PID: {self.process.pid})")
            self.logger.info(f"Stdout log: {stdout_log}")
            self.logger.info(f"Stderr log: {stderr_log}")

            # Wait for node to be ready
            start_time = time.time()
            while time.time() - start_time < timeout:
                if self.is_ready():
                    success_msg = f"Node started successfully (PID: {self.process.pid})"
                    self.logger.info(success_msg)
                    print(success_msg)
                    return True

                # Check if process died
                if self.process.poll() is not None:
                    error_msg = f"Process died with returncode {self.process.returncode}"
                    self.logger.error(error_msg)
                    print(error_msg)
                    # Print stderr for debugging
                    with open(stderr_log, 'r') as f:
                        stderr_content = f.read()
                        if stderr_content:
                            self.logger.error(f"Stderr: {stderr_content}")
                            print(f"Stderr: {stderr_content}")
                    return False

                time.sleep(0.5)

            timeout_msg = "Timeout waiting for node to become ready"
            self.logger.error(timeout_msg)
            print(timeout_msg)
            self.stop()
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

        if self.process is None:
            print("Node process not found (already stopped or never started)")
            return True

        try:
            import signal

            # Send SIGTERM for graceful shutdown
            self.logger.info(f"Sending SIGTERM to process {self.process.pid}")
            self.process.terminate()

            # Wait for process to exit
            try:
                self.process.wait(timeout=timeout)
                self.logger.info("Process terminated gracefully")
                print("Node stopped successfully")
                return True
            except subprocess.TimeoutExpired:
                # Force kill if it doesn't stop gracefully
                self.logger.warning(f"Process didn't stop gracefully, sending SIGKILL")
                self.process.kill()
                self.process.wait(timeout=5)
                self.logger.info("Process killed")
                print("Node stopped (forcefully)")
                return True

        except Exception as e:
            error_msg = f"Error stopping node: {e}"
            self.logger.error(error_msg)
            print(error_msg)
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
