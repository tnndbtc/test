#!/usr/bin/env python3
"""
Base test framework class for Blockweave functional tests.

Provides common test utilities, setup/teardown functionality, and assertion methods.
"""

import os
import logging
import tempfile
import shutil
import traceback
from pathlib import Path

from .blockweave_node import BlockweaveNode
from .test_node import TestNode


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
        self.num_nodes = 0  # Number of nodes to create (set by test case)
        self.test_nodes = []  # TestNode wrappers (created in setup_nodes)
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

        # Configure root logger - only to file, not stdout
        logging.basicConfig(
            level=logging.DEBUG,
            format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            handlers=[
                logging.FileHandler(log_file)
            ]
        )

        self.logger = logging.getLogger("test_framework")
        self.logger.info(f"Test framework initialized in {self.tmpdir}")

    def add_node(self, port=None, **kwargs):
        """
        Create and add a new BlockweaveNode for testing.

        Args:
            port: REST API port for the node (default: auto-assign starting from 28443)
            **kwargs: Additional arguments passed to BlockweaveNode

        Returns:
            BlockweaveNode: The created node instance
        """
        # Auto-assign port if not specified (start from 28443 + node_counter)
        if port is None:
            port = 28443 + self.node_counter

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

    def setup_nodes(self):
        """
        Setup test nodes based on self.num_nodes.

        Creates self.num_nodes BlockweaveNode instances, starts them,
        and wraps them in TestNode instances (stored in self.test_nodes).
        Nodes are NOT connected to each other - test cases should call
        self.test_nodes[0].connect_to_peer(self.test_nodes[1]) as needed.

        This method is called automatically by setup() if num_nodes > 0.
        """
        if self.num_nodes <= 0:
            return

        self.log_info(f"Starting {self.num_nodes} local blockweave nodes...")

        # Calculate port numbers
        base_rest_port = 28443
        base_p2p_port = 28333

        # Create and start nodes
        for i in range(self.num_nodes):
            rest_port = base_rest_port + i
            p2p_port = base_p2p_port + i

            self.log_info(f"Starting node {i} (REST: {rest_port}, P2P: {p2p_port})")
            blockweave_node = self.add_node(port=rest_port, p2p_port=p2p_port)

            if not blockweave_node.start(timeout=20):
                raise RuntimeError(f"Failed to start node {i}")

            # Wrap with TestNode for convenience methods
            test_node = TestNode(blockweave_node)
            self.test_nodes.append(test_node)

            self.log_info(f"Node {i} started successfully")

        self.log_info(f"All {len(self.nodes)} nodes started successfully")

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
            # print(f"✓ PASS: {message or f'{actual} == {expected}'}")
        else:
            self.num_failed += 1
            print(f"\n{'='*70}")
            print(f"✗ ASSERTION FAILED: {message or 'Values are not equal'}")
            print(f"{'='*70}")
            print(f"  Expected: {expected}")
            print(f"  Actual:   {actual}")
            print(f"\nCall stack:")
            print("-" * 70)
            # Print call stack excluding this function
            stack = traceback.extract_stack()[:-1]
            for frame in stack:
                print(f"  File \"{frame.filename}\", line {frame.lineno}, in {frame.name}")
                if frame.line:
                    print(f"    {frame.line}")
            print("=" * 70 + "\n")

    def assert_true(self, condition, message=""):
        """Assert that a condition is true."""
        if condition:
            self.num_success += 1
            # print(f"✓ PASS: {message or 'condition is True'}")
        else:
            self.num_failed += 1
            print(f"\n{'='*70}")
            print(f"✗ ASSERTION FAILED: {message or 'Condition is False'}")
            print(f"{'='*70}")
            print(f"  Condition evaluated to: {condition}")
            print(f"\nCall stack:")
            print("-" * 70)
            # Print call stack excluding this function
            stack = traceback.extract_stack()[:-1]
            for frame in stack:
                print(f"  File \"{frame.filename}\", line {frame.lineno}, in {frame.name}")
                if frame.line:
                    print(f"    {frame.line}")
            print("=" * 70 + "\n")

    def assert_in(self, item, container, message=""):
        """Assert that an item is in a container."""
        if item in container:
            self.num_success += 1
            # print(f"✓ PASS: {message or f'{item} in {container}'}")
        else:
            self.num_failed += 1
            print(f"\n{'='*70}")
            print(f"✗ ASSERTION FAILED: {message or 'Item not in container'}")
            print(f"{'='*70}")
            print(f"  Looking for: {item}")
            print(f"  In container: {container}")
            print(f"\nCall stack:")
            print("-" * 70)
            # Print call stack excluding this function
            stack = traceback.extract_stack()[:-1]
            for frame in stack:
                print(f"  File \"{frame.filename}\", line {frame.lineno}, in {frame.name}")
                if frame.line:
                    print(f"    {frame.line}")
            print("=" * 70 + "\n")

    def log_info(self, message):
        """Log an informational message."""
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

            # Setup nodes if num_nodes is set
            if self.num_nodes > 0:
                self.setup_nodes()

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
