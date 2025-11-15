#!/usr/bin/env python3
"""
BlockweaveNode class for managing blockweave daemon processes in tests.

Provides low-level interface for starting, stopping, and querying a local
blockweave node process.
"""

import time
import subprocess
import requests
import logging
import tempfile
import platform
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
            config_file: Path to config file (default: bweave.conf in project root)
            datadir: Data directory for this node (default: auto-generated temp directory)
            node_index: Node index for logging (default: 0, creates node0 folder)
        """
        if project_root is None:
            # Auto-detect project root
            # test_file_path is .../test_framework/blockweave_node.py
            # From build/test/functional/test_framework/blockweave_node.py -> go up 4 levels to project root
            # From test/functional/test_framework/blockweave_node.py -> go up 4 levels to project root
            test_file_path = Path(__file__).resolve()

            # Check if we're in build/test/functional/test_framework
            # Navigate up: test_framework -> functional -> test -> build -> project_root
            if "build" in test_file_path.parts and "test" in test_file_path.parts:
                # Find the "build" directory in the path and go up one level
                # Path is like: /path/to/project/build/test/functional/test_framework/blockweave_node.py
                build_index = test_file_path.parts.index("build")
                self.project_root = Path(*test_file_path.parts[:build_index])
            else:
                # Running from test/functional/test_framework -> go up 4 levels
                # Path is like: /path/to/project/test/functional/test_framework/blockweave_node.py
                # blockweave_node.py -> test_framework -> functional -> test -> project_root
                self.project_root = test_file_path.parent.parent.parent.parent
        else:
            self.project_root = Path(project_root).resolve()

        self.port = port
        self.p2p_port = p2p_port
        # Config file is copied to build/ during build from src/conf/bweave.conf
        self.config_file = config_file or (self.project_root / "build" / "bweave.conf")

        # Locate bweave executable (cross-platform)
        # On Windows, executable is bweave.exe; on Unix, it's bweave
        exe_name = "bweave.exe" if platform.system() == "Windows" else "bweave"

        if (Path.cwd() / exe_name).exists():
            self.bweave = Path.cwd() / exe_name
        else:
            self.bweave = self.project_root / "build" / exe_name

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
                "Please build the project first (it copies src/conf/bweave.conf to build/)."
            )

        # Read the base config
        with open(self.config_file, 'r') as f:
            config_lines = f.readlines()

        # Create custom config with node-specific settings
        if self.datadir:
            node_dir = Path(self.datadir)
            log_dir = node_dir / "log"
            log_dir.mkdir(parents=True, exist_ok=True)

            # Create custom config file in the node directory
            custom_config_path = node_dir / f"node{self.node_index}.conf"

            with open(custom_config_path, 'w') as f:
                p2p_port_written = False
                log_level_written = False
                network_written = False
                for line in config_lines:
                    # Override log_dir, data_dir, rest_api_port, p2p_port, log_level, network, and daemon settings
                    if line.strip().startswith('log_dir='):
                        # Use forward slashes for cross-platform compatibility (Windows accepts them)
                        f.write(f"log_dir={log_dir.as_posix()}\n")
                    elif line.strip().startswith('data_dir='):
                        f.write(f"data_dir={(node_dir / 'data').as_posix()}\n")
                    elif line.strip().startswith('rest_api_port='):
                        f.write(f"rest_api_port={self.port}\n")
                    elif line.strip().startswith('p2p_port='):
                        if self.p2p_port is not None:
                            f.write(f"p2p_port={self.p2p_port}\n")
                            p2p_port_written = True
                        else:
                            f.write(line)
                    elif line.strip().startswith('log_level='):
                        f.write("log_level=TRACE\n")  # Set TRACE log level for tests
                        log_level_written = True
                    elif line.strip().startswith('network='):
                        f.write("network=localnet\n")  # Use localnet for functional tests
                        network_written = True
                    elif line.strip().startswith('daemon='):
                        f.write("daemon=false\n")  # Force foreground mode for tests
                    else:
                        f.write(line)

                # Add p2p_port if it wasn't in the config and we have a value
                if self.p2p_port is not None and not p2p_port_written:
                    f.write(f"\n# P2P port (added by test framework)\n")
                    f.write(f"p2p_port={self.p2p_port}\n")

                # Add log_level if it wasn't in the config
                if not log_level_written:
                    f.write(f"\n# Log level (added by test framework)\n")
                    f.write(f"log_level=TRACE\n")

                # Add network if it wasn't in the config
                if not network_written:
                    f.write(f"\n# Network (added by test framework)\n")
                    f.write(f"network=localnet\n")

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
        if not self.bweave.exists():
            raise FileNotFoundError(
                f"bweave not found at {self.bweave}. "
                "Please build the project first: cd build && make"
            )

        if not self.config_file.exists():
            raise FileNotFoundError(
                f"Config file not found at {self.config_file}. "
                "Please build the project first (it copies src/conf/bweave.conf to build/)."
            )

        # Create custom config with node-specific settings
        self.custom_config_file = self.create_custom_config()

        self.logger.info(f"Starting blockweave node on port {self.port}...")
        self.logger.info(f"Using config: {self.custom_config_file}")

        # Start bweave in foreground mode as a subprocess
        try:
            cmd = [str(self.bweave), "-c", str(self.custom_config_file)]

            # Redirect stdout/stderr to log files in node directory
            node_dir = Path(self.datadir) if self.datadir else Path(tempfile.mkdtemp())
            stdout_log = node_dir / f"node{self.node_index}_stdout.log"
            stderr_log = node_dir / f"node{self.node_index}_stderr.log"

            # On Windows, run from the same directory as the executable to find DLLs
            # On Unix, can run from project root
            if platform.system() == "Windows":
                run_dir = str(self.bweave.parent)  # Run from directory containing bweave.exe
            else:
                run_dir = str(self.project_root)  # Run from project root on Unix

            with open(stdout_log, 'w') as stdout_f, open(stderr_log, 'w') as stderr_f:
                self.process = subprocess.Popen(
                    cmd,
                    cwd=run_dir,
                    stdout=stdout_f,
                    stderr=stderr_f,
                    start_new_session=True  # Detach from terminal
                )

            self.logger.info(f"Started bweave process (PID: {self.process.pid})")
            self.logger.info(f"Stdout log: {stdout_log}")
            self.logger.info(f"Stderr log: {stderr_log}")

            # Wait for node to be ready
            start_time = time.time()
            while time.time() - start_time < timeout:
                if self.is_ready():
                    self.logger.info(f"Node started successfully (PID: {self.process.pid})")
                    return True

                # Check if process died
                if self.process.poll() is not None:
                    self.logger.error(f"Process died with returncode {self.process.returncode}")
                    # Log stderr for debugging
                    with open(stderr_log, 'r') as f:
                        stderr_content = f.read()
                        if stderr_content:
                            self.logger.error(f"Stderr: {stderr_content}")
                    return False

                time.sleep(0.5)

            self.logger.error("Timeout waiting for node to become ready")
            self.stop()
            return False

        except Exception as e:
            self.logger.error(f"Error starting node: {e}")
            return False

    def stop(self, timeout=10):
        """
        Stop the blockweave node.

        Args:
            timeout: Maximum time to wait for node to stop (seconds)

        Returns:
            bool: True if node stopped successfully, False otherwise
        """
        if self.process is None:
            self.logger.info("Node process not found (already stopped or never started)")
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
                return True
            except subprocess.TimeoutExpired:
                # Force kill if it doesn't stop gracefully
                self.logger.warning(f"Process didn't stop gracefully, sending SIGKILL")
                self.process.kill()
                self.process.wait(timeout=5)
                self.logger.info("Process killed")
                return True

        except Exception as e:
            self.logger.error(f"Error stopping node: {e}")
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

    def create_transaction(self, transaction):
        """
        Create and submit a transaction to the node.

        Args:
            transaction: Dictionary with transaction data containing:
                - from (str): Sender address
                - to (str): Recipient address
                - data (str): Transaction data/payload
                - fee (int, optional): Transaction fee (default: 0)

        Returns:
            tuple: (success: bool, response_data: dict or None)
                - success: True if transaction was submitted successfully
                - response_data: JSON response from server (contains transaction_id on success)

        Raises:
            Exception: If transaction submission fails with detailed error information
        """
        try:
            self.logger.info(f"Submitting transaction to /transaction...")
            response = self.post("/transaction", json_data=transaction)

            if response.status_code == 200:
                tx_response = response.json()
                tx_id = tx_response.get("transaction_id", "unknown")
                self.logger.info(f"Transaction submitted successfully (ID: {tx_id})")
                # return True, tx_response
                return True
            else:
                # Log detailed error information
                error_msg = (
                    f"Transaction submission failed. "
                    f"Status: {response.status_code}, "
                    f"Headers: {dict(response.headers)}, "
                    f"Body: {response.text}"
                )
                self.logger.error(error_msg)
                return False
                # raise Exception(error_msg)

        except Exception as e:
            self.logger.error(f"Exception submitting transaction: {e}")
            return False
            # raise

    def trigger_mining(self):
        """
        Trigger mining of one block immediately (localnet only).

        This method calls the /rpc/minetrigger endpoint which mines a single block
        on demand. This endpoint is only available on localnet for testing purposes.

        Returns:
            bool: True if block was mined successfully, False otherwise
        """
        try:
            response = self.post("/rpc/minetrigger")

            if response.status_code == 200:
                data = response.json()
                block_hash = data.get("block_hash", "unknown")
                block_height = data.get("block_height", "unknown")
                self.logger.info(
                    f"Node{self.node_index} mined block #{block_height} "
                    f"(hash: {block_hash[:16]}...)"
                )
                return True
            elif response.status_code == 403:
                self.logger.error(
                    f"Mining trigger forbidden: Only available on localnet"
                )
                return False
            else:
                self.logger.error(
                    f"Failed to trigger mining: HTTP {response.status_code}"
                )
                return False

        except Exception as e:
            self.logger.error(f"Exception triggering mining: {e}")
            return False

    def __enter__(self):
        """Context manager entry - start the node."""
        if not self.start():
            raise RuntimeError("Failed to start blockweave node")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit - stop the node."""
        self.stop()
