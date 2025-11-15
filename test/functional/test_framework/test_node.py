#!/usr/bin/env python3
"""
Test node wrapper for Blockweave functional tests.

This module provides a higher-level wrapper around BlockweaveNode that adds
peer management functionality and convenience methods for testing P2P networks.
"""

import time
import logging
from pathlib import Path


class TestNode:
    """
    High-level wrapper for a blockweave node in functional tests.

    Provides convenience methods for:
    - Starting/stopping nodes
    - Managing peer connections
    - Querying node state
    - Mining control
    """

    def __init__(self, blockweave_node):
        """
        Initialize a test node wrapper.

        Args:
            blockweave_node: BlockweaveNode instance to wrap
        """
        self.node = blockweave_node
        self.logger = logging.getLogger(f"testnode{blockweave_node.node_index}")
        self.peers = []  # List of connected TestNode instances

    @property
    def index(self):
        """Get node index."""
        return self.node.node_index

    @property
    def port(self):
        """Get REST API port."""
        return self.node.port

    @property
    def p2p_port(self):
        """Get P2P port."""
        return self.node.p2p_port

    @property
    def base_url(self):
        """Get base URL."""
        return self.node.base_url

    def start(self, timeout=20):
        """
        Start the node.

        Args:
            timeout: Maximum time to wait for node to start (seconds)

        Returns:
            bool: True if started successfully, False otherwise
        """
        self.logger.info(f"Starting node{self.index}...")
        success = self.node.start(timeout=timeout)
        if success:
            self.logger.info(f"Node{self.index} started on REST port {self.port}, P2P port {self.p2p_port}")
        else:
            self.logger.error(f"Failed to start node{self.index}")
        return success

    def stop(self, timeout=10):
        """
        Stop the node.

        Args:
            timeout: Maximum time to wait for node to stop (seconds)

        Returns:
            bool: True if stopped successfully, False otherwise
        """
        self.logger.info(f"Stopping node{self.index}...")
        return self.node.stop(timeout=timeout)

    def is_ready(self):
        """
        Check if node is ready to accept requests.

        Returns:
            bool: True if ready, False otherwise
        """
        return self.node.is_ready()

    def get(self, endpoint, timeout=5):
        """
        Make GET request to this node.

        Args:
            endpoint: API endpoint (e.g., "/chain")
            timeout: Request timeout in seconds

        Returns:
            requests.Response: The response object
        """
        return self.node.get(endpoint, timeout=timeout)

    def post(self, endpoint, data=None, json_data=None, timeout=5):
        """
        Make POST request to this node.

        Args:
            endpoint: API endpoint (e.g., "/transaction")
            data: Request body data
            json_data: JSON data to send
            timeout: Request timeout in seconds

        Returns:
            requests.Response: The response object
        """
        return self.node.post(endpoint, data=data, json_data=json_data, timeout=timeout)

    def connect_to_peer(self, peer_node, wait=True):
        """
        Connect to another peer node using RPC.

        Args:
            peer_node: TestNode instance to connect to
            wait: Wait for connection to be established (default: True)

        Returns:
            bool: True if connection initiated successfully, False otherwise
        """
        if not isinstance(peer_node, TestNode):
            self.logger.error("peer_node must be a TestNode instance")
            return False

        if peer_node.p2p_port is None:
            self.logger.error(f"Peer node{peer_node.index} has no P2P port configured")
            return False

        self.logger.info(
            f"Node{self.index} connecting to Node{peer_node.index} "
            f"at 127.0.0.1:{peer_node.p2p_port}..."
        )

        try:
            response = self.post("/rpc/addpeer", json_data={
                "address": "127.0.0.1",
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
                    f"Node{self.index} successfully initiated connection to Node{peer_node.index}"
                )
                self.peers.append(peer_node)

                # Wait for connection to be established
                if wait:
                    time.sleep(1)

                return True
            else:
                self.logger.warning(
                    f"Failed to connect to Node{peer_node.index}: "
                    f"{data.get('message', 'unknown error')}"
                )
                return False

        except Exception as e:
            self.logger.error(f"Exception connecting to peer: {e}")
            return False

    def disconnect_from_peer(self, peer_node):
        """
        Disconnect from a peer node.

        Note: This is a placeholder - the current implementation doesn't
        support explicit disconnection via RPC.

        Args:
            peer_node: TestNode instance to disconnect from

        Returns:
            bool: Always returns False (not implemented)
        """
        self.logger.warning("Explicit peer disconnection not yet implemented")
        if peer_node in self.peers:
            self.peers.remove(peer_node)
        return False

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

    def start_mining(self):
        """
        Start mining on this node.

        Returns:
            bool: True if mining started successfully, False otherwise
        """
        try:
            response = self.post("/mine/start")

            if response.status_code == 200:
                self.logger.info(f"Node{self.index} mining started")
                return True
            else:
                self.logger.error(
                    f"Failed to start mining: HTTP {response.status_code}"
                )
                return False

        except Exception as e:
            self.logger.error(f"Exception starting mining: {e}")
            return False

    def stop_mining(self):
        """
        Stop mining on this node.

        Returns:
            bool: True if mining stopped successfully, False otherwise
        """
        try:
            response = self.post("/mine/stop")

            if response.status_code == 200:
                self.logger.info(f"Node{self.index} mining stopped")
                return True
            else:
                self.logger.error(
                    f"Failed to stop mining: HTTP {response.status_code}"
                )
                return False

        except Exception as e:
            self.logger.error(f"Exception stopping mining: {e}")
            return False

    def trigger_mining(self):
        """
        Trigger mining of one block immediately (localnet only).

        This method delegates to the underlying BlockweaveNode.trigger_mining().

        Returns:
            bool: True if block was mined successfully, False otherwise
        """
        return self.node.trigger_mining()

    def wait_for_block(self, target_height, timeout=30, check_interval=1):
        """
        Wait for blockchain to reach a specific height.

        Args:
            target_height: Target blockchain height to wait for
            timeout: Maximum time to wait (seconds)
            check_interval: How often to check (seconds)

        Returns:
            bool: True if target height reached, False if timeout
        """
        start_time = time.time()

        self.logger.info(
            f"Node{self.index} waiting for block height {target_height}..."
        )

        while time.time() - start_time < timeout:
            chain_info = self.get_chain_info()

            if chain_info.get("status") == "success":
                current_height = chain_info.get("height", 0)

                if current_height >= target_height:
                    self.logger.info(
                        f"Node{self.index} reached height {current_height}"
                    )
                    return True

            time.sleep(check_interval)

        self.logger.error(
            f"Node{self.index} timeout waiting for height {target_height}"
        )
        return False

    def wait_for_peer_count(self, target_count, timeout=10, check_interval=0.5):
        """
        Wait for peer count to reach a specific value.

        Args:
            target_count: Target number of connected peers
            timeout: Maximum time to wait (seconds)
            check_interval: How often to check (seconds)

        Returns:
            bool: True if target count reached, False if timeout
        """
        start_time = time.time()

        self.logger.info(
            f"Node{self.index} waiting for {target_count} peer(s)..."
        )

        while time.time() - start_time < timeout:
            peer_info = self.get_peer_info()

            if peer_info.get("status") == "success":
                total_peers = peer_info.get("total_peers", 0)

                if total_peers >= target_count:
                    self.logger.info(
                        f"Node{self.index} has {total_peers} peer(s) connected"
                    )
                    return True

            time.sleep(check_interval)

        self.logger.error(
            f"Node{self.index} timeout waiting for {target_count} peer(s)"
        )
        return False

    def __repr__(self):
        """String representation of the test node."""
        return (
            f"TestNode(index={self.index}, "
            f"rest_port={self.port}, "
            f"p2p_port={self.p2p_port})"
        )


class PeerNetwork:
    """
    Manages a network of test nodes with convenient peer connection setup.

    Provides methods to:
    - Create network topologies (star, mesh, chain)
    - Connect/disconnect peers
    - Query network state
    """

    def __init__(self):
        """Initialize an empty peer network."""
        self.nodes = []
        self.logger = logging.getLogger("peer_network")

    def add_node(self, node):
        """
        Add a node to the network.

        Args:
            node: TestNode instance to add
        """
        if not isinstance(node, TestNode):
            raise TypeError("node must be a TestNode instance")

        self.nodes.append(node)
        self.logger.info(f"Added Node{node.index} to network")

    def connect_star(self, hub_node, wait=True):
        """
        Connect all nodes to a central hub node (star topology).

        Args:
            hub_node: TestNode to use as central hub
            wait: Wait for connections to be established

        Returns:
            int: Number of successful connections
        """
        if hub_node not in self.nodes:
            self.logger.error("Hub node not in network")
            return 0

        self.logger.info(f"Creating star topology with Node{hub_node.index} as hub")

        success_count = 0
        for node in self.nodes:
            if node != hub_node:
                if node.connect_to_peer(hub_node, wait=wait):
                    success_count += 1

        self.logger.info(
            f"Star topology created: {success_count}/{len(self.nodes)-1} connections"
        )
        return success_count

    def connect_mesh(self, wait=True):
        """
        Connect all nodes to each other (full mesh topology).

        Args:
            wait: Wait for connections to be established

        Returns:
            int: Number of successful connections
        """
        self.logger.info("Creating full mesh topology")

        success_count = 0
        for i, node_a in enumerate(self.nodes):
            for node_b in self.nodes[i+1:]:
                if node_a.connect_to_peer(node_b, wait=wait):
                    success_count += 1

        total_possible = len(self.nodes) * (len(self.nodes) - 1) // 2
        self.logger.info(
            f"Mesh topology created: {success_count}/{total_possible} connections"
        )
        return success_count

    def connect_chain(self, wait=True):
        """
        Connect nodes in a chain (linear topology).

        Node0 -> Node1 -> Node2 -> ... -> NodeN

        Args:
            wait: Wait for connections to be established

        Returns:
            int: Number of successful connections
        """
        if len(self.nodes) < 2:
            self.logger.warning("Need at least 2 nodes for chain topology")
            return 0

        self.logger.info("Creating chain topology")

        success_count = 0
        for i in range(len(self.nodes) - 1):
            if self.nodes[i].connect_to_peer(self.nodes[i+1], wait=wait):
                success_count += 1

        self.logger.info(
            f"Chain topology created: {success_count}/{len(self.nodes)-1} connections"
        )
        return success_count

    def get_network_state(self):
        """
        Get state of all nodes in the network.

        Returns:
            list: List of dicts with node state information
        """
        states = []

        for node in self.nodes:
            chain_info = node.get_chain_info()
            peer_info = node.get_peer_info()

            states.append({
                "node_index": node.index,
                "rest_port": node.port,
                "p2p_port": node.p2p_port,
                "ready": node.is_ready(),
                "height": chain_info.get("height", -1),
                "mempool_size": chain_info.get("mempool_size", -1),
                "mining_enabled": chain_info.get("mining_enabled", False),
                "total_peers": peer_info.get("total_peers", -1),
                "outbound_peers": peer_info.get("outbound_peers", -1),
                "inbound_peers": peer_info.get("inbound_peers", -1)
            })

        return states

    def wait_for_sync(self, timeout=30, check_interval=1):
        """
        Wait for all nodes to reach the same blockchain height.

        Args:
            timeout: Maximum time to wait (seconds)
            check_interval: How often to check (seconds)

        Returns:
            bool: True if all nodes synced, False if timeout
        """
        start_time = time.time()

        self.logger.info("Waiting for network to sync...")

        while time.time() - start_time < timeout:
            heights = []

            for node in self.nodes:
                chain_info = node.get_chain_info()
                if chain_info.get("status") == "success":
                    heights.append(chain_info.get("height", 0))

            # Check if all heights are the same and greater than 0
            if heights and len(set(heights)) == 1 and heights[0] > 0:
                self.logger.info(f"Network synced at height {heights[0]}")
                return True

            time.sleep(check_interval)

        self.logger.error("Timeout waiting for network to sync")
        return False

    def stop_all(self):
        """Stop all nodes in the network."""
        self.logger.info("Stopping all nodes in network...")

        for node in self.nodes:
            try:
                node.stop()
            except Exception as e:
                self.logger.error(f"Failed to stop Node{node.index}: {e}")

    def __len__(self):
        """Return number of nodes in network."""
        return len(self.nodes)

    def __repr__(self):
        """String representation of the network."""
        return f"PeerNetwork(nodes={len(self.nodes)})"
