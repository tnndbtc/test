#!/usr/bin/env python3
"""
Base test framework class for Blockweave functional tests.

Provides common test utilities, setup/teardown functionality, and assertion methods.
"""

import os
import logging
import tempfile
import shutil
import unittest
from pathlib import Path

from .blockweave_node import BlockweaveNode
from .test_node import TestNode


class TestFramework(unittest.TestCase):
    """
    Base class for functional tests.

    Provides common test utilities and setup/teardown functionality.
    Extends unittest.TestCase for standard Python test framework integration.
    """

    def __init__(self, methodName='runTest'):
        """Initialize the test framework."""
        super().__init__(methodName)
        self.node = None
        self.tmpdir = None
        self.nocleanup = False
        self.nodes = []
        self.node_counter = 0
        self.num_nodes = 0  # Number of nodes to create (set by test case)
        self.test_nodes = []  # TestNode wrappers (created in setup_nodes)

    def init_tmpdir(self):
        """Initialize tmpdir from environment or create temporary directory with test file name."""
        from datetime import datetime

        # Get the test module name (e.g., 'test_blockcore', 'test_rest_api', 'test_p2p')
        # This comes from the __module__ attribute of the test class
        test_module = self.__class__.__module__  # e.g., 'test_blockcore'

        tmpdir = os.environ.get("TEST_TMPDIR")
        if tmpdir:
            # If TEST_TMPDIR is set, create a subdirectory named after the test module
            base_tmpdir = Path(tmpdir)
            base_tmpdir.mkdir(parents=True, exist_ok=True)
            self.tmpdir = base_tmpdir / test_module
            self.tmpdir.mkdir(parents=True, exist_ok=True)
        else:
            # Create temp directory with test module name prefix and timestamp
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            self.tmpdir = Path(tempfile.mkdtemp(prefix=f"{test_module}_{timestamp}_"))

        self.nocleanup = bool(os.environ.get("TEST_NOCLEANUP"))

    def setup_logging(self):
        """Setup logging to test_framework.log in tmpdir."""
        if not self.tmpdir:
            return

        log_file = self.tmpdir / "test_framework.log"

        # Create logger for this test
        self.logger = logging.getLogger(f"test_framework_{self.__class__.__name__}")
        self.logger.setLevel(logging.DEBUG)

        # Remove any existing handlers to avoid duplicates
        self.logger.handlers.clear()

        # Add file handler for this test
        file_handler = logging.FileHandler(log_file)
        file_handler.setLevel(logging.DEBUG)
        formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
        file_handler.setFormatter(formatter)
        self.logger.addHandler(file_handler)

        # Prevent propagation to root logger to avoid duplicate logs
        self.logger.propagate = False

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

        This method is called automatically by setUp() if num_nodes > 0.
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

    @classmethod
    def setUpClass(cls):
        """
        Setup before running all test methods in the class (unittest standard method).

        This method is called once before all test methods in the class.
        Initializes tmpdir, logging, nodes, and calls the custom setup() method.

        If the test class sets use_per_test_setup=True, this method does nothing
        and setup/teardown happens in setUp/tearDown instead.
        """
        # Check if this test class wants per-test setup instead
        if getattr(cls, 'use_per_test_setup', False):
            return

        # Initialize class-level attributes
        cls.tmpdir = None
        cls.nocleanup = False
        cls.nodes = []
        cls.node_counter = 0
        cls.test_nodes = []

        # Create instance to access instance methods
        cls._temp_instance = cls('setUp')
        cls._temp_instance.init_tmpdir()
        cls._temp_instance.setup_logging()

        # Copy to class level
        cls.tmpdir = cls._temp_instance.tmpdir
        cls.nocleanup = cls._temp_instance.nocleanup
        cls.logger = cls._temp_instance.logger if hasattr(cls._temp_instance, 'logger') else None

        # Setup nodes if num_nodes is set (check both class and instance)
        # This must happen BEFORE calling custom setup() so nodes are available
        num_nodes = getattr(cls, 'num_nodes', 0) or getattr(cls._temp_instance, 'num_nodes', 0)
        if num_nodes > 0:
            # Ensure instance has num_nodes set
            cls._temp_instance.num_nodes = num_nodes
            cls._temp_instance.setup_nodes()
            # Copy to class level
            cls.nodes = cls._temp_instance.nodes
            cls.test_nodes = cls._temp_instance.test_nodes
            cls.node_counter = cls._temp_instance.node_counter

        # Call custom setup AFTER nodes are created
        cls._temp_instance.setup()

    @classmethod
    def tearDownClass(cls):
        """
        Cleanup after running all test methods in the class (unittest standard method).

        This method is called once after all test methods in the class.

        If the test class sets use_per_test_setup=True, this method does nothing
        and setup/teardown happens in setUp/tearDown instead.
        """
        # Check if this test class wants per-test setup instead
        if getattr(cls, 'use_per_test_setup', False):
            return

        try:
            if hasattr(cls, '_temp_instance'):
                cls._temp_instance.cleanup()
        finally:
            # Stop all nodes
            for node in cls.nodes:
                try:
                    node.stop()
                except Exception as e:
                    print(f"Warning: Failed to stop node{node.node_index}: {e}")

            # Cleanup tmpdir if not preserving
            if not cls.nocleanup and cls.tmpdir and cls.tmpdir.exists():
                try:
                    shutil.rmtree(cls.tmpdir)
                    if cls.logger:
                        cls.logger.info(f"Cleaned up tmpdir: {cls.tmpdir}")
                except Exception as e:
                    print(f"Warning: Failed to cleanup tmpdir {cls.tmpdir}: {e}")
            elif cls.nocleanup:
                print(f"\nTest data preserved in: {cls.tmpdir}")

    def setUp(self):
        """
        Setup before each test method (unittest standard method).

        This method is called automatically before each test method.
        If use_per_test_setup=True, does full setup; otherwise copies from setUpClass.
        """
        # If use_per_test_setup is True, do full setup for each test
        if getattr(self.__class__, 'use_per_test_setup', False):
            self.tmpdir = None
            self.nocleanup = False
            self.nodes = []
            self.node_counter = 0
            self.test_nodes = []

            self.init_tmpdir()
            self.setup_logging()
            self.setup()

            # Setup nodes if needed
            num_nodes = getattr(self, 'num_nodes', 0)
            if num_nodes > 0:
                self.setup_nodes()
        else:
            # Copy class-level attributes to instance for convenience
            if hasattr(self.__class__, '_temp_instance'):
                self.tmpdir = self.__class__.tmpdir
                self.nocleanup = self.__class__.nocleanup
                self.nodes = self.__class__.nodes
                self.test_nodes = self.__class__.test_nodes
                self.node_counter = self.__class__.node_counter
                self.logger = self.__class__.logger if hasattr(self.__class__, 'logger') else None

                # Copy node reference if single node test
                if hasattr(self.__class__._temp_instance, 'node'):
                    self.node = self.__class__._temp_instance.node

                # Copy any other custom attributes from _temp_instance
                # This allows test classes to set attributes in setup() that are accessible in test methods
                for attr_name in dir(self.__class__._temp_instance):
                    if not attr_name.startswith('_') and attr_name not in ['node', 'nodes', 'test_nodes', 'tmpdir', 'nocleanup', 'logger', 'node_counter']:
                        attr_value = getattr(self.__class__._temp_instance, attr_name, None)
                        # Only copy non-callable attributes that aren't already set
                        if not callable(attr_value) and not hasattr(self, attr_name):
                            setattr(self, attr_name, attr_value)

    def tearDown(self):
        """
        Cleanup after each test method (unittest standard method).

        This method is called automatically after each test method.
        If use_per_test_setup=True, does full cleanup; otherwise does nothing.
        """
        # If use_per_test_setup is True, do full cleanup for each test
        if getattr(self.__class__, 'use_per_test_setup', False):
            try:
                self.cleanup()
            finally:
                # Stop all nodes
                for node in self.nodes:
                    try:
                        node.stop()
                    except Exception as e:
                        print(f"Warning: Failed to stop node{node.node_index}: {e}")

                # Cleanup tmpdir if not preserving
                if not self.nocleanup and self.tmpdir and self.tmpdir.exists():
                    try:
                        shutil.rmtree(self.tmpdir)
                        self.log_info(f"Cleaned up tmpdir: {self.tmpdir}")
                    except Exception as e:
                        print(f"Warning: Failed to cleanup tmpdir {self.tmpdir}: {e}")
                elif self.nocleanup:
                    print(f"\nTest data preserved in: {self.tmpdir}")

    def setup(self):
        """
        Custom setup before running tests.

        Override this method in test classes for custom setup logic.
        This is called once before all test methods in the class.
        """
        pass

    def cleanup(self):
        """
        Custom cleanup after running tests.

        Override this method in test classes for custom cleanup logic.
        This is called once after all test methods in the class.
        """
        pass

    def assert_equal(self, actual, expected, message=""):
        """
        Assert that two values are equal.

        Wrapper around unittest.assertEqual() for backward compatibility.
        """
        self.assertEqual(actual, expected, message)

    def assert_true(self, condition, message=""):
        """
        Assert that a condition is true.

        Wrapper around unittest.assertTrue() for backward compatibility.
        """
        self.assertTrue(condition, message)

    def assert_in(self, item, container, message=""):
        """
        Assert that an item is in a container.

        Wrapper around unittest.assertIn() for backward compatibility.
        """
        self.assertIn(item, container, message)

    def log_info(self, message):
        """Log an informational message."""
        if hasattr(self, 'logger'):
            self.logger.info(message)
