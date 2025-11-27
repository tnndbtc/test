#!/usr/bin/env python3
"""
P2P network functional test

Tests peer-to-peer networking by starting multiple local nodes
and verifying they can establish connections via the RPC API.

This test suite covers:
- Node startup and isolation
- Port allocation and uniqueness
- Mining status verification
- P2P connections via /rpc/addpeer endpoint
- Peer info queries via /rpc/getpeer endpoint
- Connection time tracking
- Inbound and outbound peer connections
needs to set env var: PYTHONPATH=../functional
"""

import sys
import time
import unittest
from test_framework import TestFramework


class P2PTest(TestFramework):
    """Test P2P networking with multiple nodes."""

    # Set num_nodes as class attribute so it's available during setUpClass
    num_nodes = 5

    # Network interface IP mappings (Docker multi-network configuration)
    # In Docker, each network interface gets a different IP address
    # eth0: First Docker network (e.g., 10.10.0.0/16)
    # eth1: Second Docker network (e.g., 81.10.0.0/16)
    # eth2: Third Docker network (e.g., 8.8.0.0/16)
    # eth3: Fourth Docker network (e.g., 203.10.0.0/16)
    #
    # Example IPs for demonstration:
    # - node0 binds to eth0 IP: 10.10.0.2
    # - node1 binds to eth1 IP: 81.10.0.2
    # - node2+ bind to eth2 IP: 8.8.0.2, etc.
    interface_ips = {
        'eth0': '10.10.0.2',  # IP on first Docker network
        'eth1': '81.10.0.2',  # IP on second Docker network
        'eth2': '8.8.0.2',  # IP on third Docker network (base IP)
        'eth3': '203.10.0.2',  # IP on third Docker network (base IP)
    }

    def setup_nodes(self):
        """
        Override setup_nodes to assign bind_ip based on interface mapping.

        - node0 binds to eth0 IP
        - node1 binds to eth1 IP
        - node2 and rest bind to eth2 IP
        """
        if self.num_nodes <= 0:
            return

        self.log_info(f"Starting {self.num_nodes} local blockweave nodes with interface bindings...")

        # Calculate port numbers (using localnet ports: 48443 REST, 48333 P2P)
        base_rest_port = 48443
        base_p2p_port = 48333

        # Create and start nodes with specific bind_ip assignments
        for i in range(self.num_nodes):
            rest_port = base_rest_port + i
            p2p_port = base_p2p_port + i

            # Determine which IP to bind based on node index
            if i == 0:
                bind_ip = self.interface_ips['eth0']
                interface = 'eth0'
            elif i == 1:
                bind_ip = self.interface_ips['eth1']
                interface = 'eth1'
            elif i == 4:
                bind_ip = self.interface_ips['eth3']
                interface = 'eth3'
            else:
                bind_ip = self.interface_ips['eth2']
                interface = 'eth2'

            self.log_info(
                f"Starting node {i} (REST: {rest_port}, P2P: {p2p_port}, "
                f"bind_ip: {bind_ip} on {interface}, "
                f"max_inbound_peers=2, max_outbound_peers=1)"
            )
            blockweave_node = self.add_node(
                port=rest_port,
                p2p_port=p2p_port,
                bind_ip=bind_ip,
                max_inbound_peers=2,
                max_outbound_peers=1
            )

            if not blockweave_node.start(timeout=20):
                raise RuntimeError(f"Failed to start node {i}")

            self.log_info(f"Node {i} started successfully on {interface}")

        self.log_info(f"All {len(self.nodes)} nodes started successfully")

    def setup(self):
        """Setup test environment - configure to start 4 local nodes and establish connections."""
        # Nodes will be created as:
        # Node 0: REST API port 48443, P2P port 48333, bind_ip=eth0, max_inbound=2, max_outbound=1
        # Node 1: REST API port 48444, P2P port 48334, bind_ip=eth1, max_inbound=2, max_outbound=1
        # Node 2: REST API port 48445, P2P port 48335, bind_ip=eth2, max_inbound=2, max_outbound=1
        # Node 3: REST API port 48446, P2P port 48336, bind_ip=eth2, max_inbound=2, max_outbound=1
        # Node 4: REST API port 48447, P2P port 48337, bind_ip=eth2, max_inbound=2, max_outbound=1
        #

        # Debug: Log peer counts after setup
        for i, node in enumerate(self.nodes):
            peer_info = node.get_peer_info()
            total = peer_info.get('total_peers', 0)
            outbound = peer_info.get('outbound_peers', 0)
            inbound = peer_info.get('inbound_peers', 0)
            self.log_info(f"setup: Node{i} peer counts: total={total}, outbound={outbound}, inbound={inbound}")

    def help_connect(self, node_from, node_to):
        if node_from.connect_to_peer(node_to, wait=True):
            self.log_info(f"setup: node_from({node_from.bind_ip}:{node_from.p2p_port}) successfully connected to node_to({node_to.bind_ip}:{node_to.p2p_port})")
            return True
        else:
            self.log_info(f"setup: WARNING - Failed to connect node_from({node_from.bind_ip}:{node_from.p2p_port}) to node_to({node_to.bind_ip}:{node_to.p2p_port})")
        return False

    def test_1_nodes_are_running(self):
        """Verify all nodes are running."""
        self.log_info("test_01_nodes_are_running: Verifying all nodes are running...")

        for node in self.nodes:
            # Check if process is alive
            if node.process and node.process.poll() is None:
                self.assert_true(True, f"Node {node.node_index} process is running")
            else:
                self.assert_true(False, f"Node {node.node_index} process should be running")

            self.log_info(f"Node {node.node_index} is running")

        # node0 connect to node1
        self.log_info("setup: Establishing peer connections...")

        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node2 = self.nodes[2]
        node3 = self.nodes[3]
        node4 = self.nodes[4]

        self.help_connect(node1, node0)
        node0_peer_info = node0.get_peer_info()
        total_peers = node0_peer_info.get('total_peers', -1)
        total_outbound = node0_peer_info.get('outbound_peers', -1)
        total_inbound = node0_peer_info.get('inbound_peers', -1)
        self.assert_equal(total_peers, 1, f"{total_peers} (expect to be 1)")
        self.assert_equal(total_inbound, 1, f"{total_inbound} (expect to be 1)")
        self.assert_equal(total_outbound, 0, f"{total_outbound} (expect to be 0)")
        peers_list = node0_peer_info.get('peers', [])
        self.log_info(f"after node1 connect to node0: node0 peers_list: " + str(peers_list))
        self.assert_equal(len(peers_list), 1, f"peers_list length expect to be 1")
        peer = peers_list[0]
        self.assert_equal(peer["address"], "81.10.0.2", f"peer address should be 81.10.0.2")
        self.assert_true(peer["inbound"], f"peer is inbound")
        # peer port is randomly select by OS, so do not assert here

        self.help_connect(node2, node0)
        node0_peer_info = node0.get_peer_info()
        total_peers = node0_peer_info.get('total_peers', -1)
        total_outbound = node0_peer_info.get('outbound_peers', -1)
        total_inbound = node0_peer_info.get('inbound_peers', -1)
        self.assert_equal(total_peers, 2, f"{total_peers} (expect to be 2)")
        self.assert_equal(total_inbound, 2, f"{total_inbound} (expect to be 2)")
        self.assert_equal(total_outbound, 0, f"{total_outbound} (expect to be 0)")
        peers_list = node0_peer_info.get('peers', [])
        self.log_info(f"after node2 connect to node0: node0 peers_list: " + str(peers_list))
        self.assert_equal(len(peers_list), 2, f"peers_list length expect to be 2")
        peer0 = peers_list[0]
        self.assert_equal(peer0["address"], "81.10.0.2", f"peer address should be 81.10.0.2")
        self.assert_true(peer0["inbound"], f"peer {peer0["address"]} is inbound")
        peer1 = peers_list[1]
        self.assert_equal(peer1["address"], "8.8.0.2", f"peer address should be 8.8.0.2")
        self.assert_true(peer1["inbound"], f"peer {peer1["address"]} is inbound")
        peer1_p2p_port_prev = peer1["port"]

        # eviction on same subnet
        self.help_connect(node3, node0)
        node0_peer_info = node0.get_peer_info()
        total_peers = node0_peer_info.get('total_peers', -1)
        total_outbound = node0_peer_info.get('outbound_peers', -1)
        total_inbound = node0_peer_info.get('inbound_peers', -1)
        self.assert_equal(total_peers, 2, f"{total_peers} (expect to be 2)")
        self.assert_equal(total_inbound, 2, f"{total_inbound} (expect to be 2)")
        peers_list = node0_peer_info.get('peers', [])
        self.log_info(f"after node3 connect to node0: node0 peers_list: " + str(peers_list))
        self.assert_equal(len(peers_list), 2, f"peers_list length expect to be 2")
        peer0 = peers_list[0]
        self.assert_equal(peer0["address"], "81.10.0.2", f"peer address should be 81.10.0.2")
        self.assert_true(peer0["inbound"], f"peer {peer0["address"]} is inbound")
        peer1 = peers_list[1]
        self.assert_equal(peer1["address"], "8.8.0.2", f"peer address should be 8.8.0.2")
        self.assert_true(peer1["inbound"], f"peer {peer1["address"]} is inbound")
        self.assert_true(peer1_p2p_port_prev != peer1["port"], f"peer {peer1["address"]} p2p port {peer1["port"]} is a different from previous peer port {peer1_p2p_port_prev} because of same subnet eviction")

        # Verify log file contains the expected eviction message
        # Dropping peer 8.8.0.2:48595 from same subnet
        eviction_msg1 = "Dropping peer 8.8.0.2"
        eviction_msg2 = "from same subnet"
        found_eviction1 = self.check_log_for_message(node0, eviction_msg1)
        found_eviction2 = self.check_log_for_message(node0, eviction_msg2)

        self.assert_true(found_eviction1 and found_eviction2,
            f"Log file should contain peer eviction message from same subnet: '{eviction_msg1} and {eviction_msg2}'")

        if found_eviction1 and found_eviction2:
            self.log_info(f"Found expected eviction message in log: '{eviction_msg1} and {eviction_msg2}'")

        # eviction on random peer
        self.help_connect(node4, node0)
        node0_peer_info = node0.get_peer_info()
        total_peers = node0_peer_info.get('total_peers', -1)
        total_outbound = node0_peer_info.get('outbound_peers', -1)
        total_inbound = node0_peer_info.get('inbound_peers', -1)
        self.assert_equal(total_peers, 2, f"{total_peers} (expect to be 2)")
        self.assert_equal(total_inbound, 2, f"{total_inbound} (expect to be 2)")
        peers_list = node0_peer_info.get('peers', [])
        self.log_info(f"after node4 connect to node0: node0 peers_list: " + str(peers_list))
        self.assert_equal(len(peers_list), 2, f"peers_list length expect to be 2")
        # no need to check peer0, because it's randomly evicted, so not sure which one is dropped
        peer1 = peers_list[1]
        self.assert_equal(peer1["address"], "203.10.0.2", f"peer address should be 203.10.0.2")
        self.assert_true(peer1["inbound"], f"peer {peer1["address"]} is inbound")

        # Verify log file contains the expected random peer eviction message
        eviction_msg = "Dropping random peer for network diversity"
        found_eviction = self.check_log_for_message(node0, eviction_msg)

        # eviction on outbound peer
        self.help_connect(node4, node1)
        node4_peer_info = node4.get_peer_info()
        total_peers = node4_peer_info.get('total_peers', -1)
        total_outbound = node4_peer_info.get('outbound_peers', -1)
        total_inbound = node4_peer_info.get('inbound_peers', -1)
        self.assert_equal(total_peers, 1, f"node4 total peers: {total_peers} (expect to be 1)")
        self.assert_equal(total_inbound, 0, f"node4 total inbound: {total_inbound} (expect to be 0)")
        self.assert_equal(total_outbound, 1, f"node4 total outbound: {total_outbound} (expect to be 1)")
        peers_list = node4_peer_info.get('peers', [])
        self.log_info(f"after node4 connect to node1: node4 peers_list: " + str(peers_list))
        self.assert_equal(len(peers_list), 1, f"peers_list length expect to be 1")
        self.assert_true(found_eviction,
            f"Log file should contain random peer eviction message: '{eviction_msg}'")

        if found_eviction:
            self.log_info(f"Found expected eviction message in log: '{eviction_msg}'")

        self.log_info(f"setup: Completed - peer eviction happened as expected (verified in logs)")

if __name__ == "__main__":
    unittest.main()
