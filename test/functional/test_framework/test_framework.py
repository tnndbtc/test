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
import platform
import time
import subprocess
from pathlib import Path

from .blockweave_node import BlockweaveNode


class TestFramework(unittest.TestCase):
    """
    Base class for functional tests.

    Provides common test utilities and setup/teardown functionality.
    Extends unittest.TestCase for standard Python test framework integration.
    """

    @staticmethod
    def check_for_running_bweave_processes():
        """
        Check if there are any bweave processes currently running.

        Returns:
            list: List of (pid, command_line) tuples for running bweave processes.
                  Empty list if no processes found.

        This helps detect orphaned processes from previous test runs that could
        interfere with the current test by holding ports or resources.

        Enhanced to avoid false positives from:
        - Text editors opening bweave.log or bweave.conf
        - tail/cat/less viewing log files
        - Other utilities operating on bweave-related files
        """
        running_processes = []

        try:
            if platform.system() == "Windows":
                # Windows: Use tasklist command
                # This directly filters for bweave.exe processes, so it's precise
                result = subprocess.run(
                    ["tasklist", "/FI", "IMAGENAME eq bweave.exe", "/FO", "CSV", "/NH"],
                    capture_output=True,
                    text=True,
                    timeout=5
                )

                if result.returncode == 0:
                    # Parse CSV output (format: "Image Name","PID","Session Name","Session#","Mem Usage")
                    for line in result.stdout.strip().split('\n'):
                        if line and 'bweave.exe' in line.lower():
                            parts = line.replace('"', '').split(',')
                            if len(parts) >= 2:
                                try:
                                    pid = int(parts[1].strip())
                                    running_processes.append((pid, "bweave.exe"))
                                except ValueError:
                                    pass
            else:
                # Unix-like systems (Linux, macOS): Use ps command with better filtering
                result = subprocess.run(
                    ["ps", "aux"],
                    capture_output=True,
                    text=True,
                    timeout=5
                )

                if result.returncode == 0:
                    for line in result.stdout.split('\n'):
                        # Skip lines that don't contain 'bweave'
                        if 'bweave' not in line:
                            continue

                        parts = line.split()
                        if len(parts) < 11:
                            continue

                        try:
                            pid = int(parts[1])
                            # Get the command (everything from the 11th column onwards)
                            command = ' '.join(parts[10:])

                            # Enhanced filtering to avoid false positives:
                            # 1. Exclude processes that are clearly not the bweave daemon:
                            exclude_patterns = [
                                'grep',           # grep bweave
                                'bweave_cli',     # CLI tool, not daemon
                                'python',         # Python scripts mentioning bweave
                                'vim',            # vim bweave.log
                                'vi ',            # vi bweave.conf
                                'nano',           # nano bweave.log
                                'emacs',          # emacs bweave.conf
                                'tail',           # tail -f bweave.log
                                'cat',            # cat bweave.log
                                'less',           # less bweave.log
                                'more',           # more bweave.log
                                'head',           # head bweave.log
                                'sed',            # sed editing files
                                'awk',            # awk processing files
                                'code',           # VSCode: code bweave.conf
                                'sublime',        # Sublime Text
                                'atom',           # Atom editor
                                'wallet',         # wallet executable, not daemon
                            ]

                            # Check if command matches any exclusion pattern
                            if any(pattern in command for pattern in exclude_patterns):
                                continue

                            # 2. Include only if it's actually the bweave executable:
                            # Match patterns like:
                            # - /path/to/bweave
                            # - ./bweave
                            # - bweave (bare command)
                            # - bweave -c config.conf (with args)
                            include_patterns = [
                                '/bweave ',       # /path/to/bweave with args
                                '/bweave\n',      # /path/to/bweave at end of line
                                '/bweave$',       # /path/to/bweave (exact match)
                                './bweave ',      # ./bweave with args
                                './bweave\n',     # ./bweave at end of line
                                './bweave$',      # ./bweave (exact match)
                            ]

                            # Also check if command starts with 'bweave' (bare command)
                            # but not 'bweave_cli' or 'bweave.log' etc.
                            command_parts = command.split()
                            if command_parts:
                                first_part = command_parts[0]
                                # Check if first part is exactly 'bweave' or ends with '/bweave'
                                if first_part == 'bweave' or first_part.endswith('/bweave'):
                                    running_processes.append((pid, command))
                                    continue

                            # Check include patterns
                            if any(pattern in command for pattern in include_patterns):
                                running_processes.append((pid, command))

                        except (ValueError, IndexError):
                            pass

        except subprocess.TimeoutExpired:
            print("Warning: Timeout while checking for running bweave processes")
        except Exception as e:
            print(f"Warning: Error checking for running bweave processes: {e}")

        return running_processes

    def __init__(self, methodName='runTest'):
        """Initialize the test framework."""
        super().__init__(methodName)
        self.node = None
        self.tmpdir = None
        self.nocleanup = False
        self.nodes = []
        self.node_counter = 0
        self.num_nodes = 0  # Number of nodes to create (set by test case)

    @staticmethod
    def force_remove_tree(path):
        """
        Force remove a directory tree, handling Windows file locking issues.

        On Windows, files may be locked by processes or file handles, preventing
        deletion. This function retries with delays and sets files to writable.

        Args:
            path: Path object or string of directory to remove
        """
        path = Path(path)
        if not path.exists():
            return True

        if platform.system() != "Windows":
            # On Unix, use standard shutil.rmtree
            shutil.rmtree(path)
            return True

        # Windows: Force removal with retries
        max_retries = 3
        retry_delay = 0.5  # seconds

        for attempt in range(max_retries):
            try:
                # First, make all files writable (remove read-only flag)
                for root, dirs, files in os.walk(path):
                    for name in files:
                        file_path = os.path.join(root, name)
                        try:
                            os.chmod(file_path, 0o777)
                        except:
                            pass

                # Try to remove the directory
                shutil.rmtree(path)
                return True

            except PermissionError as e:
                if attempt < max_retries - 1:
                    # Wait and retry
                    time.sleep(retry_delay)
                    continue
                else:
                    # Final attempt failed
                    print(f"Warning: Failed to remove {path} after {max_retries} attempts: {e}")
                    return False
            except Exception as e:
                print(f"Warning: Unexpected error removing {path}: {e}")
                return False

        return False

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
        # Use custom format with UTC time in brackets and milliseconds
        formatter = logging.Formatter('[%(asctime)s.%(msecs)03d UTC] - %(name)s - %(levelname)s - %(message)s',
                                      datefmt='%Y-%m-%d %H:%M:%S')
        formatter.converter = time.gmtime  # Use UTC time instead of local time
        file_handler.setFormatter(formatter)
        self.logger.addHandler(file_handler)

        # Prevent propagation to root logger to avoid duplicate logs
        self.logger.propagate = False

        self.logger.info(f"Test framework initialized in {self.tmpdir}")

    def add_node(self, port=None, **kwargs):
        """
        Create and add a new BlockweaveNode for testing.

        Args:
            port: REST API port for the node (default: auto-assign starting from 48443 for localnet)
            **kwargs: Additional arguments passed to BlockweaveNode

        Returns:
            BlockweaveNode: The created node instance
        """
        # Auto-assign port if not specified (start from 48443 + node_counter for localnet)
        if port is None:
            port = 48443 + self.node_counter

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
        Nodes are NOT connected to each other - test cases should call
        self.nodes[0].connect_to_peer(self.nodes[1]) as needed.

        This method is called automatically by setUp() if num_nodes > 0.
        """
        if self.num_nodes <= 0:
            return

        self.log_info(f"Starting {self.num_nodes} local blockweave nodes...")

        # Calculate port numbers (using localnet ports: 48443 REST, 48333 P2P)
        base_rest_port = 48443
        base_p2p_port = 48333

        # Create and start nodes
        for i in range(self.num_nodes):
            rest_port = base_rest_port + i
            p2p_port = base_p2p_port + i

            self.log_info(f"Starting node {i} (REST: {rest_port}, P2P: {p2p_port})")
            blockweave_node = self.add_node(port=rest_port, p2p_port=p2p_port)

            if not blockweave_node.start(timeout=20):
                raise RuntimeError(f"Failed to start node {i}")

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

        # Check for running bweave processes before starting tests
        running_processes = cls.check_for_running_bweave_processes()
        if running_processes:
            error_msg = "\n" + "="*70 + "\n"
            error_msg += "ERROR: Found running bweave process(es) before test start!\n"
            error_msg += "="*70 + "\n"
            error_msg += "\nRunning bweave processes detected:\n"
            for pid, command in running_processes:
                error_msg += f"  PID {pid}: {command}\n"
            error_msg += "\nThese processes may interfere with the tests by holding ports or resources.\n"
            error_msg += "Please stop all bweave processes before running tests.\n"
            error_msg += "\nTo stop them:\n"
            if platform.system() == "Windows":
                error_msg += "  taskkill /F /IM bweave.exe\n"
            else:
                error_msg += "  pkill bweave\n"
                error_msg += "  # Or kill specific processes:\n"
                for pid, _ in running_processes:
                    error_msg += f"  kill {pid}\n"
            error_msg += "="*70 + "\n"

            # Print to stderr so it's visible even if test output is captured
            import sys
            print(error_msg, file=sys.stderr)

            # Raise exception to stop test execution
            raise RuntimeError("Running bweave processes detected. Please clean up before running tests.")

        # Initialize class-level attributes
        cls.tmpdir = None
        cls.nocleanup = False
        cls.nodes = []
        cls.node_counter = 0

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

            # Close logger handlers to release file locks (Windows)
            if hasattr(cls, 'logger') and cls.logger:
                handlers = cls.logger.handlers[:]
                for handler in handlers:
                    try:
                        handler.close()
                        cls.logger.removeHandler(handler)
                    except:
                        pass

            # Cleanup tmpdir if not preserving
            if not cls.nocleanup and cls.tmpdir and cls.tmpdir.exists():
                cls.force_remove_tree(cls.tmpdir)
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
            # Check for running bweave processes before starting test
            running_processes = self.check_for_running_bweave_processes()
            if running_processes:
                error_msg = "\n" + "="*70 + "\n"
                error_msg += "ERROR: Found running bweave process(es) before test start!\n"
                error_msg += "="*70 + "\n"
                error_msg += "\nRunning bweave processes detected:\n"
                for pid, command in running_processes:
                    error_msg += f"  PID {pid}: {command}\n"
                error_msg += "\nThese processes may interfere with the tests by holding ports or resources.\n"
                error_msg += "Please stop all bweave processes before running tests.\n"
                error_msg += "\nTo stop them:\n"
                if platform.system() == "Windows":
                    error_msg += "  taskkill /F /IM bweave.exe\n"
                else:
                    error_msg += "  pkill bweave\n"
                    error_msg += "  # Or kill specific processes:\n"
                    for pid, _ in running_processes:
                        error_msg += f"  kill {pid}\n"
                error_msg += "="*70 + "\n"

                # Print to stderr so it's visible even if test output is captured
                import sys
                print(error_msg, file=sys.stderr)

                # Raise exception to stop test execution
                raise RuntimeError("Running bweave processes detected. Please clean up before running tests.")

            self.tmpdir = None
            self.nocleanup = False
            self.nodes = []
            self.node_counter = 0

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
                self.node_counter = self.__class__.node_counter
                self.logger = self.__class__.logger if hasattr(self.__class__, 'logger') else None

                # Copy node reference if single node test
                if hasattr(self.__class__._temp_instance, 'node'):
                    self.node = self.__class__._temp_instance.node

                # Copy any other custom attributes from _temp_instance
                # This allows test classes to set attributes in setup() that are accessible in test methods
                for attr_name in dir(self.__class__._temp_instance):
                    if not attr_name.startswith('_') and attr_name not in ['node', 'nodes', 'tmpdir', 'nocleanup', 'logger', 'node_counter']:
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

                # Close logger handlers to release file locks (Windows)
                if hasattr(self, 'logger') and self.logger:
                    handlers = self.logger.handlers[:]
                    for handler in handlers:
                        try:
                            handler.close()
                            self.logger.removeHandler(handler)
                        except:
                            pass

                # Cleanup tmpdir if not preserving
                if not self.nocleanup and self.tmpdir and self.tmpdir.exists():
                    self.force_remove_tree(self.tmpdir)
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

    def log_error(self, message):
        """Log an error message."""
        if hasattr(self, 'logger'):
            self.logger.error(message)

    def log_warn(self, message):
        """Log a warning message."""
        if hasattr(self, 'logger'):
            self.logger.warning(message)

    def log_debug(self, message):
        """Log a debug message."""
        if hasattr(self, 'logger'):
            self.logger.debug(message)

    def get_node_log_file(self, node):
        """
        Get the log file path for a given node.

        Args:
            node: BlockweaveNode instance

        Returns:
            Path: Path to the node's log file (bweave.log)
        """
        # Convert to Path if it's a string
        datadir = Path(node.datadir) if isinstance(node.datadir, str) else node.datadir
        return datadir / "log" / "bweave.log"

    def get_log_position(self, log_file):
        """
        Get current position in log file for tracking new entries.

        Args:
            log_file: Path to the log file

        Returns:
            int: Current file position, or None on error
        """
        try:
            with open(log_file, 'r') as f:
                f.seek(0, 2)  # Seek to end
                return f.tell()
        except Exception as e:
            self.log_info(f"Could not get log position: {e}")
            return None

    def read_new_logs(self, log_file, log_position):
        """
        Read log entries added since the given position.

        Args:
            log_file: Path to the log file
            log_position: Position to start reading from (from get_log_position)

        Returns:
            str: New log content since position, or empty string on error
        """
        if log_position is None:
            return ""
        try:
            import os
            platform_specific_behavior = 'r+'  # for apple darwin
            with open(log_file, platform_specific_behavior) as f:
                # on Linux, log contents is still in OS buffer because node only called flush().
                # Force write to disk first
                os.fsync(f.fileno())
                f.seek(log_position)
                return f.read()
        except Exception as e:
            self.log_info(f"Could not read logs: {e}")
            return ""

    def check_log_for_message(self, node, message, since_position=None):
        """
        Check if a node's log file contains a specific message.

        Args:
            node: BlockweaveNode instance
            message: String to search for in the log
            since_position: Optional position to start reading from (from get_log_position)

        Returns:
            bool: True if message found, False otherwise
        """
        log_file = self.get_node_log_file(node)
        if not log_file.exists():
            self.log_warn(f"Log file does not exist: {log_file}")
            return False

        try:
            if since_position is not None:
                log_content = self.read_new_logs(log_file, since_position)
            else:
                with open(log_file, 'r') as f:
                    log_content = f.read()

            return message in log_content
        except Exception as e:
            self.log_warn(f"Error checking log for message: {e}")
            return False
