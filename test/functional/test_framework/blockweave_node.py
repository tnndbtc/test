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

        # Locate bweave executable
        if (Path.cwd() / "bweave").exists():
            self.bweave = Path.cwd() / "bweave"
        else:
            self.bweave = self.project_root / "build" / "bweave"

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

            with open(stdout_log, 'w') as stdout_f, open(stderr_log, 'w') as stderr_f:
                self.process = subprocess.Popen(
                    cmd,
                    cwd=str(self.project_root),
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

    def __enter__(self):
        """Context manager entry - start the node."""
        if not self.start():
            raise RuntimeError("Failed to start blockweave node")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit - stop the node."""
        self.stop()
