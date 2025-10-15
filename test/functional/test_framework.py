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
from pathlib import Path


class BlockweaveNode:
    """
    Manages a blockweave node process for testing.

    Provides methods to start, stop, and query a local blockweave node.
    """

    def __init__(self, project_root=None, port=28443, config_file=None):
        """
        Initialize the node manager.

        Args:
            project_root: Path to project root directory (auto-detected if None)
            port: REST API port (default: 28443)
            config_file: Path to config file (default: blockweave.conf in project root)
        """
        if project_root is None:
            # Auto-detect project root (two levels up from this file)
            self.project_root = Path(__file__).parent.parent.parent.resolve()
        else:
            self.project_root = Path(project_root).resolve()

        self.port = port
        self.config_file = config_file or (self.project_root / "blockweave.conf")
        self.daemon_cli = self.project_root / "build" / "daemon_cli"
        self.base_url = f"http://localhost:{self.port}"
        self.process = None

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

        print(f"Starting blockweave node on port {self.port}...")

        # Start the daemon
        try:
            result = subprocess.run(
                [str(self.daemon_cli), "start"],
                cwd=str(self.project_root),
                capture_output=True,
                text=True,
                timeout=timeout
            )

            if result.returncode != 0:
                print(f"Failed to start daemon: {result.stderr}")
                return False

            # Wait for node to be ready
            start_time = time.time()
            while time.time() - start_time < timeout:
                if self.is_ready():
                    print(f"Node started successfully (PID from daemon)")
                    return True
                time.sleep(0.5)

            print("Timeout waiting for node to become ready")
            self.stop()
            return False

        except subprocess.TimeoutExpired:
            print(f"Timeout starting daemon after {timeout} seconds")
            return False
        except Exception as e:
            print(f"Error starting node: {e}")
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

    def main(self):
        """Main test execution flow."""
        print(f"\n{'='*70}")
        print(f"Running: {self.__class__.__name__}")
        print(f"{'='*70}\n")

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
            self.num_failed += 1

        finally:
            # Cleanup
            self.log_info("Cleaning up...")
            self.cleanup()

            # Print summary
            print(f"\n{'='*70}")
            print(f"TEST SUMMARY")
            print(f"{'='*70}")
            print(f"Passed: {self.num_success}")
            print(f"Failed: {self.num_failed}")
            print(f"Total:  {self.num_success + self.num_failed}")

            if self.num_failed == 0:
                print(f"\n✓ ALL TESTS PASSED\n")
                return 0
            else:
                print(f"\n✗ SOME TESTS FAILED\n")
                return 1
