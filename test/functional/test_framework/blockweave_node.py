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

# Suppress urllib3 connection warnings and errors from being printed to console
# These will still be logged via the logger
logging.getLogger("urllib3").setLevel(logging.WARNING)
logging.getLogger("urllib3.connectionpool").propagate = True


class BlockweaveNode:
    """
    Manages a blockweave node process for testing.

    Provides methods to start, stop, and query a local blockweave node.
    """

    def __init__(self, project_root=None, port=28443, p2p_port=None, config_file=None, datadir=None, node_index=0, bind_ip=None, max_inbound_peers=None, max_outbound_peers=None):
        """
        Initialize the node manager.

        Args:
            project_root: Path to project root directory (auto-detected if None)
            port: REST API port (default: 28443)
            p2p_port: P2P network port (default: None, uses default from config)
            config_file: Path to config file (default: bweave.conf in project root)
            datadir: Data directory for this node (default: auto-generated temp directory)
            node_index: Node index for logging (default: 0, creates node0 folder)
            bind_ip: IP address to bind P2P socket to (default: None, uses default from config)
            max_inbound_peers: Maximum number of inbound peer connections (default: None, uses default from config)
            max_outbound_peers: Maximum number of outbound peer connections (default: None, uses default from config)
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
        self.bind_ip = bind_ip
        self.max_inbound_peers = max_inbound_peers
        self.max_outbound_peers = max_outbound_peers
        # Config file is copied to build/ during build from src/conf/bweave.conf
        self.config_file = config_file or (self.project_root / "build" / "bweave.conf")

        # Locate bweave executable (cross-platform)
        # On Windows, executable is bweave.exe; on Unix, it's bweave
        exe_name = "bweave.exe" if platform.system() == "Windows" else "bweave"

        if (Path.cwd() / exe_name).exists():
            self.bweave = Path.cwd() / exe_name
        else:
            # On Windows, check build/Debug first (default CMake build location)
            if platform.system() == "Windows":
                debug_path = self.project_root / "build" / "Debug" / exe_name
                if debug_path.exists():
                    self.bweave = debug_path
                else:
                    # Fallback to build/ directory
                    self.bweave = self.project_root / "build" / exe_name
            else:
                # On Linux/Mac, use build/ directory
                self.bweave = self.project_root / "build" / exe_name

        self.base_url = f"http://localhost:{self.port}"
        self.process = None
        self.node_index = node_index
        self.datadir = datadir
        self.logger = logging.getLogger(f"node{node_index}")
        self.logger.setLevel(logging.DEBUG)
        self.custom_config_file = None

    @property
    def port(self):
        """Get P2P port."""
        return self._port

    @port.setter
    def port(self, value):
        self._port = value

    @property
    def p2p_port(self):
        """Get P2P port."""
        return self._p2p_port

    @p2p_port.setter
    def p2p_port(self, value):
        self._p2p_port = value

    @property
    def node_index(self):
        """Get P2P port."""
        return self._node_index

    @node_index.setter
    def node_index(self, value):
        self._node_index = value

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
                bind_ip_written = False
                max_inbound_peers_written = False
                max_outbound_peers_written = False
                log_level_written = False
                network_written = False
                for line in config_lines:
                    # Override log_dir, data_dir, rest_api_port, p2p_port, bind_ip, max_inbound_peers, max_outbound_peers, log_level, network, and daemon settings
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
                    elif line.strip().startswith('bind_ip=') or line.strip().startswith('#bind_ip='):
                        if self.bind_ip is not None:
                            f.write(f"bind_ip={self.bind_ip}\n")
                            bind_ip_written = True
                        else:
                            f.write(line)
                    elif line.strip().startswith('max_inbound_peers='):
                        if self.max_inbound_peers is not None:
                            f.write(f"max_inbound_peers={self.max_inbound_peers}\n")
                            max_inbound_peers_written = True
                        else:
                            f.write(line)
                    elif line.strip().startswith('max_outbound_peers='):
                        if self.max_outbound_peers is not None:
                            f.write(f"max_outbound_peers={self.max_outbound_peers}\n")
                            max_outbound_peers_written = True
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

                # Add bind_ip if it wasn't in the config and we have a value
                if self.bind_ip is not None and not bind_ip_written:
                    f.write(f"\n# Bind IP address (added by test framework)\n")
                    f.write(f"bind_ip={self.bind_ip}\n")

                # Add max_inbound_peers if it wasn't in the config and we have a value
                if self.max_inbound_peers is not None and not max_inbound_peers_written:
                    f.write(f"\n# Max inbound peers (added by test framework)\n")
                    f.write(f"max_inbound_peers={self.max_inbound_peers}\n")

                # Add max_outbound_peers if it wasn't in the config and we have a value
                if self.max_outbound_peers is not None and not max_outbound_peers_written:
                    f.write(f"\n# Max outbound peers (added by test framework)\n")
                    f.write(f"max_outbound_peers={self.max_outbound_peers}\n")

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
            if self.bind_ip is not None:
                self.logger.info(f"Bind IP: {self.bind_ip}")
            if self.max_inbound_peers is not None:
                self.logger.info(f"Max inbound peers: {self.max_inbound_peers}")
            if self.max_outbound_peers is not None:
                self.logger.info(f"Max outbound peers: {self.max_outbound_peers}")
            return custom_config_path

        return self.config_file

    def _load_cookie_file(self):
        """
        Load .cookie file from node's data directory for RPC authentication.

        The .cookie file is generated by the node on startup at data/<network>/.cookie
        Format: __cookie__:<64 hex chars>

        This method waits for the file to be created and reads the credentials.
        """
        import time

        if not self.datadir:
            self.logger.warning("No datadir set, skipping .cookie loading")
            return

        # Cookie path: data/<network>/.cookie
        node_dir = Path(self.datadir)
        cookie_path = node_dir / "data" / "localnet" / ".cookie"

        # Wait up to 5 seconds for the .cookie file to be created by the node
        max_wait = 5
        start_time = time.time()
        while not cookie_path.exists():
            if time.time() - start_time > max_wait:
                self.logger.error(f"Timeout waiting for .cookie file: {cookie_path}")
                return
            time.sleep(0.1)

        # Read the .cookie file
        try:
            with open(cookie_path, 'r') as f:
                content = f.read().strip()
                # Format is: __cookie__:password
                if ':' in content:
                    username, password = content.split(':', 1)
                    self.cookie_credentials = (username, password)
                    self.logger.info(f"Loaded .cookie file: {cookie_path}")
                else:
                    self.logger.error(f"Invalid .cookie file format: {content}")
        except Exception as e:
            self.logger.error(f"Failed to read .cookie file: {e}")

    def get_cookie_credentials(self):
        """
        Get RPC authentication credentials from .cookie file.

        Returns:
            tuple or None: (username, password) tuple if credentials are loaded,
                          None if not available
        """
        if hasattr(self, 'cookie_credentials'):
            return self.cookie_credentials
        return None

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
                # Run from directory containing bweave.exe (e.g., build/Debug/)
                # This ensures DLLs in the same directory are found
                run_dir = str(self.bweave.parent)
            else:
                run_dir = str(self.project_root)  # Run from project root on Unix

            with open(stdout_log, 'w') as stdout_f, open(stderr_log, 'w') as stderr_f:
                # Platform-specific process creation
                if platform.system() == "Windows":
                    # Windows: Create new process group for graceful shutdown
                    # CREATE_NEW_PROCESS_GROUP (0x00000200) - Process is root of new process group
                    #
                    # Why we use this flag:
                    # 1. Makes the child process the leader of a new process group
                    # 2. The process group ID equals the child's PID
                    # 3. Allows us to send CTRL_BREAK_EVENT to this specific process group
                    # 4. Enables graceful shutdown via GenerateConsoleCtrlEvent()
                    #
                    # Why we do NOT use CREATE_NEW_CONSOLE (0x00000010):
                    # - GenerateConsoleCtrlEvent only works when processes share the same console
                    # - The child must inherit our console for the event to be delivered
                    # - Creating a new console would prevent console control events from working
                    #
                    # Why we do NOT use CREATE_NO_WINDOW (0x08000000):
                    # - The process needs a console for the ConsoleCtrlHandler to work
                    # - Without a console, CTRL_BREAK_EVENT cannot be received
                    #
                    # See: https://docs.microsoft.com/en-us/windows/console/generateconsolectrlevent
                    CREATE_NEW_PROCESS_GROUP = 0x00000200

                    self.process = subprocess.Popen(
                        cmd,
                        cwd=run_dir,
                        stdout=stdout_f,
                        stderr=stderr_f,
                        creationflags=CREATE_NEW_PROCESS_GROUP
                    )

                    self.logger.debug(
                        f"Created Windows process with CREATE_NEW_PROCESS_GROUP "
                        f"(process group ID = {self.process.pid})"
                    )
                else:
                    # Unix: Start new session to detach from terminal
                    # This makes the process a session leader and detaches it from the controlling terminal
                    self.process = subprocess.Popen(
                        cmd,
                        cwd=run_dir,
                        stdout=stdout_f,
                        stderr=stderr_f,
                        start_new_session=True
                    )

            self.logger.info(f"Started bweave process (PID: {self.process.pid})")
            self.logger.info(f"Stdout log: {stdout_log}")
            self.logger.info(f"Stderr log: {stderr_log}")

            # Wait for node to be ready
            start_time = time.time()
            while time.time() - start_time < timeout:
                if self.is_ready():
                    self.logger.info(f"Node started successfully (PID: {self.process.pid})")
                    # Load .cookie file generated by the node for RPC authentication
                    self._load_cookie_file()
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
        Stop the blockweave node gracefully.

        On Windows, sends Ctrl+Break event for graceful shutdown.
        On Unix, sends SIGTERM signal.

        Args:
            timeout: Maximum time to wait for node to stop (seconds)

        Returns:
            bool: True if node stopped successfully, False otherwise
        """
        if self.process is None:
            self.logger.info("Node process not found (already stopped or never started)")
            return True

        # Check if process is already terminated
        if self.process.poll() is not None:
            self.logger.info(f"Process already exited with code {self.process.returncode}")
            return True

        try:
            pid = self.process.pid
            self.logger.info(f"Initiating graceful shutdown of process {pid}")

            if platform.system() == "Windows":
                # Windows: Send Ctrl+Break event for graceful shutdown
                # This triggers the ConsoleCtrlHandler in the C++ code, allowing
                # proper cleanup of resources (unlike TerminateProcess which kills immediately)
                import ctypes

                # Load kernel32.dll
                try:
                    kernel32 = ctypes.windll.kernel32
                except Exception as e:
                    self.logger.error(f"Failed to load kernel32.dll: {e}")
                    self.logger.warning("Falling back to terminate()")
                    self.process.terminate()
                    self.process.wait(timeout=timeout)
                    return True

                # CTRL_BREAK_EVENT = 1 (can be sent to any process group)
                # Use process PID as process group ID (set by CREATE_NEW_PROCESS_GROUP)
                CTRL_BREAK_EVENT = 1

                self.logger.info(f"Sending CTRL_BREAK_EVENT to process group {pid}")

                # GenerateConsoleCtrlEvent returns non-zero on success
                success = kernel32.GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)

                if not success:
                    # Get the error code for diagnostics
                    error_code = kernel32.GetLastError()
                    error_messages = {
                        0: "ERROR_SUCCESS (should not happen)",
                        6: "ERROR_INVALID_HANDLE",
                        87: "ERROR_INVALID_PARAMETER (process group not found or not attached to console)",
                        5: "ERROR_ACCESS_DENIED"
                    }
                    error_msg = error_messages.get(error_code, f"Unknown error {error_code}")

                    self.logger.warning(
                        f"GenerateConsoleCtrlEvent failed: {error_msg}. "
                        f"This may happen if the process is not attached to a console. "
                        f"Falling back to terminate()"
                    )

                    # Fall back to terminate() which uses TerminateProcess
                    # This is less graceful but necessary if console control event fails
                    self.process.terminate()
                else:
                    self.logger.info("CTRL_BREAK_EVENT sent successfully")

            else:
                # Unix: Send SIGTERM for graceful shutdown
                import signal
                self.logger.info(f"Sending SIGTERM to process {pid}")
                self.process.terminate()

            # Wait for process to exit gracefully
            try:
                start_time = time.time()
                self.process.wait(timeout=timeout)
                elapsed = time.time() - start_time
                returncode = self.process.returncode

                self.logger.info(
                    f"Process terminated gracefully in {elapsed:.2f}s with exit code {returncode}"
                )
                return True

            except subprocess.TimeoutExpired:
                # Force kill if it doesn't stop gracefully
                self.logger.warning(
                    f"Process {pid} didn't stop gracefully within {timeout}s. "
                    f"Forcing termination with kill()..."
                )

                self.process.kill()

                try:
                    self.process.wait(timeout=5)
                    self.logger.info(f"Process force killed (exit code: {self.process.returncode})")
                    return True
                except subprocess.TimeoutExpired:
                    self.logger.error(f"Process {pid} did not respond to kill() after 5s!")
                    return False

        except Exception as e:
            self.logger.error(f"Unexpected error stopping node: {e}", exc_info=True)

            # Last resort: try to kill the process
            try:
                if self.process and self.process.poll() is None:
                    self.logger.warning("Attempting emergency kill...")
                    self.process.kill()
                    self.process.wait(timeout=5)
                    self.logger.info("Emergency kill successful")
            except Exception as kill_error:
                self.logger.error(f"Emergency kill failed: {kill_error}")

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

        # Use authentication for /rpc endpoints
        auth = None
        if endpoint.startswith('/rpc/') and hasattr(self, 'cookie_credentials') and self.cookie_credentials:
            auth = self.cookie_credentials

        return requests.get(url, auth=auth, timeout=timeout)

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

        # Use authentication for /rpc endpoints
        auth = None
        if endpoint.startswith('/rpc/') and hasattr(self, 'cookie_credentials') and self.cookie_credentials:
            auth = self.cookie_credentials

        return requests.post(url, data=data, json=json_data, auth=auth, timeout=timeout)

    def create_transaction(self, transaction, timeout=30):
        """
        Create and submit a transaction to the node.

        Args:
            transaction: Dictionary with transaction data containing:
                - from (str): Sender address
                - to (str): Recipient address
                - data (str): Transaction data/payload
                - fee (int, optional): Transaction fee (default: 0)
            timeout: Request timeout in seconds (default: 30)

        Returns:
            tuple: (success: bool, response_data: dict or None)
                - success: True if transaction was submitted successfully
                - response_data: JSON response from server (contains transaction_id on success)

        Raises:
            Exception: If transaction submission fails with detailed error information
        """
        try:
            self.logger.info(f"Submitting transaction to /transaction...")
            response = self.post("/transaction", json_data=transaction, timeout=timeout)

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

    def get_chain_info(self):
        """
        Get blockchain state information.

        Returns:
            dict: Chain information with keys:
                - status (str): "success" or "error"
                - height (int): Current blockchain height
                - mempool_size (int): Number of pending transactions
                - mining_enabled (bool): Whether mining is active
                - error (str): Error message if status is "error"
        """
        try:
            response = self.get("/chain")

            if response.status_code != 200:
                return {
                    "status": "error",
                    "error": f"HTTP {response.status_code}"
                }

            data = response.json()
            return {
                "status": "success",
                **data
            }

        except Exception as e:
            self.logger.error(f"Exception getting chain info: {e}")
            return {
                "status": "error",
                "error": str(e)
            }

    def get_peer_info(self):
        """
        Get peer connection information.

        Returns:
            dict: Peer information with keys:
                - status (str): "success" or "error"
                - total_peers (int): Total connected peers
                - outbound_peers (int): Number of outbound connections
                - inbound_peers (int): Number of inbound connections
                - peers (list): List of peer addresses
                - error (str): Error message if status is "error"
        """
        try:
            response = self.get("/rpc/getpeer")

            if response.status_code != 200:
                return {
                    "status": "error",
                    "error": f"HTTP {response.status_code}"
                }

            data = response.json()
            return data

        except Exception as e:
            self.logger.error(f"Exception getting peer info: {e}")
            return {
                "status": "error",
                "error": str(e)
            }

    # def connect_to_peer(self, peer_address, peer_port, wait=True):
    def set_mock_time(self, timestamp):
        """
        Set mock time for testing time-dependent logic (localnet only).

        Args:
            timestamp: UNIX timestamp in seconds, or 0 to disable mock time

        Returns:
            dict: Response with keys 'result', 'mocktime', 'enabled'
        """
        try:
            response = self.post("/rpc/setmocktime", json_data={"time": timestamp})

            if response.status_code == 200:
                data = response.json()
                self.logger.info(
                    f"Mock time set to {timestamp} " +
                    ("(disabled)" if timestamp == 0 else "")
                )
                return data
            else:
                self.logger.error(
                    f"Failed to set mock time: HTTP {response.status_code}, "
                    f"Body: {response.text}"
                )
                return {"error": f"HTTP {response.status_code}"}

        except Exception as e:
            self.logger.error(f"Exception setting mock time: {e}")
            return {"error": str(e)}

    def get_mock_time(self):
        """
        Get current mock time value.

        Returns:
            int: Current mock time (0 if not set)
        """
        try:
            # Query by setting time to 0 (which disables, but returns current value)
            response = self.post("/rpc/setmocktime", json_data={"time": 0})

            if response.status_code == 200:
                data = response.json()
                return data.get("mocktime", 0)
            else:
                self.logger.warning(f"Failed to get mock time: HTTP {response.status_code}")
                return 0

        except Exception as e:
            self.logger.error(f"Exception getting mock time: {e}")
            return 0

    def trigger_rotation(self):
        """
        Trigger peer rotation check immediately (localnet only).

        Forces the peer manager to check rotation logic immediately
        instead of waiting for the next periodic check (5 seconds).

        Returns:
            dict: Response with 'result' and 'message' keys
        """
        try:
            response = self.post("/rpc/triggerrotation")

            if response.status_code == 200:
                data = response.json()
                self.logger.info("Triggered rotation check")
                return data
            else:
                self.logger.error(
                    f"Failed to trigger rotation: HTTP {response.status_code}, "
                    f"Body: {response.text}"
                )
                return {"error": f"HTTP {response.status_code}"}

        except Exception as e:
            self.logger.error(f"Exception triggering rotation: {e}")
            return {"error": str(e)}

    def disconnect_peer(self, peer_node):
        """
        Disconnect a specific peer (localnet only).

        Args:
            peer_node: BlockweaveNode instance to disconnect from

        Returns:
            bool: True if peer was disconnected successfully, False otherwise
        """
        peer_address = "127.0.0.1"
        if peer_node.bind_ip is not None:
            peer_address = peer_node.bind_ip

        try:
            response = self.post("/rpc/disconnectpeer", json_data={
                "address": peer_address,
                "port": peer_node.p2p_port
            })

            if response.status_code == 200:
                data = response.json()
                self.logger.info(f"Disconnected peer: {peer_address}:{peer_node.p2p_port}")
                return True
            elif response.status_code == 404:
                self.logger.warning(f"Peer not found: {peer_address}:{peer_node.p2p_port}")
                return False
            else:
                self.logger.error(
                    f"Failed to disconnect peer: HTTP {response.status_code}, "
                    f"Body: {response.text}"
                )
                return False

        except Exception as e:
            self.logger.error(f"Exception disconnecting peer: {e}")
            return False

    def connect_to_peer(self, peer_node, wait=True):
        """
        Connect to another peer node using RPC.

        Args:
            peer_node: TestNode instance to connect to
            wait: Wait for connection to be established (default: True)

        Returns:
            bool: True if connection initiated successfully, False otherwise
        """

        peer_address = "127.0.0.1"
        if peer_node.bind_ip != None:
            peer_address = peer_node.bind_ip
        self.logger.info(
            f"Node{self.node_index} connecting to peer at {peer_address}:{peer_node.p2p_port}..."
        )

        try:
            response = self.post("/rpc/addpeer", json_data={
                "address": peer_address,
                "port": peer_node.p2p_port
            })

            if response.status_code != 200:
                self.logger.error(
                    f"Failed to connect to peer: HTTP {response.status_code}"
                )
                return False

            data = response.json()

            if data.get("status") == "success":
                self.logger.info(
                    f"Node{self.node_index} successfully initiated connection to {peer_address}:{peer_node.p2p_port}"
                )

                # Wait for connection to be established
                if wait:
                    import time
                    time.sleep(1)

                return True
            else:
                self.logger.warning(
                    f"Failed to connect to peer at {peer_address}:{peer_node.p2p_port}: "
                    f"{data.get('message', 'unknown error')}"
                )
                return False

        except Exception as e:
            self.logger.error(f"Exception connecting to peer: {e}")
            return False

    def count_outbound_peers(self):
        """
        Count number of outbound peer connections.

        Returns:
            int: Number of outbound connections
        """
        peer_info = self.get_peer_info()
        if peer_info.get("status") == "success":
            return peer_info.get("outbound_peers", 0)
        return 0

    def is_connected_to(self, peer_node):
        """
        Check if connected to a specific peer by port number.

        Args:
            peer_node: BlockweaveNode instance to check connection to

        Returns:
            bool: True if connected to the peer, False otherwise
        """
        peer_info = self.get_peer_info()
        if peer_info.get("status") != "success":
            return False

        # Check outbound peers for matching port
        for peer in peer_info.get("peers", []):
            if peer.get("port") == peer_node.p2p_port:
                return True

        return False

    def __enter__(self):
        """Context manager entry - start the node."""
        if not self.start():
            raise RuntimeError("Failed to start blockweave node")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit - stop the node."""
        self.stop()
