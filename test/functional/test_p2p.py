#!/usr/bin/env python3
"""
P2P network functional test for Blockweave.

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
"""

import sys
import time
import unittest
from test_framework import TestFramework


class P2PTest(TestFramework):
    """Test P2P networking with multiple nodes."""

    # Set num_nodes as class attribute so it's available during setUpClass
    num_nodes = 4

    def setup(self):
        """Setup test environment - configure to start 4 local nodes and establish connections."""
        # Nodes will be created as:
        # Node 0: REST API port 28443, P2P port 28333
        # Node 1: REST API port 28444, P2P port 28334
        # Node 2: REST API port 28445, P2P port 28335
        # Node 3: REST API port 28446, P2P port 28336
        #
        # Node0 will connect to nodes 1, 2, 3 during setup
        self.log_info("setup: Establishing peer connections...")

        node0 = self.test_nodes[0]
        target_nodes = [
            self.test_nodes[1],
            self.test_nodes[2],
            self.test_nodes[3]
        ]

        self.successful_connections = 0

        # Connect node0 to nodes 1, 2, 3
        for peer_node in target_nodes:
            self.log_info(
                f"setup: Node{node0.index} connecting to Node{peer_node.index} "
                f"at 127.0.0.1:{peer_node.p2p_port}..."
            )

            if node0.connect_to_peer(peer_node, wait=True):
                self.log_info(f"setup: Node{node0.index} successfully connected to Node{peer_node.index}")
                self.successful_connections += 1
            else:
                self.log_info(f"setup: WARNING - Failed to connect Node{node0.index} to Node{peer_node.index}")

        # Wait for connections to stabilize
        time.sleep(2)

        self.log_info(f"setup: Completed - {self.successful_connections}/3 connections established")

        # Debug: Log peer counts after setup
        for i, node in enumerate(self.test_nodes):
            peer_info = node.get_peer_info()
            total = peer_info.get('total_peers', 0)
            outbound = peer_info.get('outbound_peers', 0)
            inbound = peer_info.get('inbound_peers', 0)
            self.log_info(f"setup: Node{i} peer counts: total={total}, outbound={outbound}, inbound={inbound}")

    def test_01_nodes_are_running(self):
        """Verify all nodes are running."""
        self.log_info("test_01_nodes_are_running: Verifying all nodes are running...")

        for test_node in self.test_nodes:
            # Check if process is alive
            if test_node.node.process and test_node.node.process.poll() is None:
                self.assert_true(True, f"Node {test_node.index} process is running")
            else:
                self.assert_true(False, f"Node {test_node.index} process should be running")

            self.log_info(f"Node {test_node.index} is running")

    def test_02_port_isolation(self):
        """Verify nodes are listening on different ports."""
        self.log_info("test_02_port_isolation: Verifying port isolation...")

        ports_used = set()
        base_port = 28443

        for test_node in self.test_nodes:
            expected_port = base_port + test_node.index
            self.assert_equal(
                test_node.port,
                expected_port,
                f"Node {test_node.index} should use port {expected_port}"
            )
            self.assert_true(
                expected_port not in ports_used,
                f"Port {expected_port} should be unique"
            )
            ports_used.add(expected_port)
            self.log_info(f"Node {test_node.index} confirmed on unique port {expected_port}")

    def test_03_mining_status(self):
        """Verify mining is enabled on all nodes (mining starts automatically)."""
        self.log_info("test_03_mining_status: Verifying mining status on all nodes...")

        for test_node in self.test_nodes:
            chain_info = test_node.get_chain_info()

            # Mining should be enabled by default when daemon starts
            self.assert_equal(
                chain_info.get("mining_enabled"),
                True,
                f"Node {test_node.index} should have mining enabled by default"
            )
            self.log_info(f"Node {test_node.index} mining status confirmed: enabled={chain_info['mining_enabled']}")

    def test_04_inbound_peer_connections(self):
        """
        Test that nodes can establish inbound connections.

        We'll test that when Node 1 connects to Node 0, Node 0 shows
        an inbound connection and Node 1 shows an outbound connection.
        """
        self.log_info("test_04_inbound peer connections: Testing inbound peer connections...")

        # For this test, we would need to:
        # 1. Have Node 1 connect to Node 0 (Node 1 makes outbound, Node 0 receives inbound)
        # 2. Query Node 0 to verify it has 1 inbound peer
        # 3. Query Node 1 to verify it has 1 outbound peer

        # Note: This requires implementing peer statistics endpoint in REST API
        # For now, we log that this test would verify inbound connections
        self.log_info("Inbound connection test: Would verify Node 0 accepts inbound from Node 1")

        # If REST API had /peer/stats endpoint, we would do:
        # Node 1 connects to Node 0's P2P port (28333)
        # response = self.nodes[0].get("/peer/stats")
        # self.assert_equal(response.json()["inbound_peers"], 1)

        # For now, just verify nodes are still responsive
        for test_node in self.test_nodes:
            self.assert_true(
                test_node.is_ready(),
                f"Node {test_node.index} should still be responsive"
            )

        self.log_info("Inbound connection test placeholder completed")

    def test_05_node0_outbound_connections(self):
        """
        Test that node0 has established outbound connections to node1, node2, and node3.

        This test verifies:
        - Peer connections were established in setup()
        - Peer info can be queried via get_peer_info()
        - Connections remain stable during the test
        - ping_roundtrip_time is 0 for all peers (no PING sent yet)
        """
        self.log_info("test_05_node0 outbound connections via TestNode: Testing node0 outbound connections via TestNode...")

        # Connections were established in setup()
        node0 = self.test_nodes[0]

        # Verify we established connections during setup
        self.assert_true(
            self.successful_connections == 3,
            f"Node0 should have established 3 connections in setup, got {self.successful_connections}"
        )

        # Query peer info using TestNode.get_peer_info()
        self.log_info("Querying Node0 peer info...")
        peer_info = node0.get_peer_info()

        self.assert_in("status", peer_info, "Response should contain status field")
        self.assert_equal(peer_info["status"], "success", "get_peer_info should return success")
        self.assert_in("total_peers", peer_info, "Response should contain total_peers")
        self.assert_in("outbound_peers", peer_info, "Response should contain outbound_peers")
        self.assert_in("inbound_peers", peer_info, "Response should contain inbound_peers")
        self.assert_in("peers", peer_info, "Response should contain peers list")

        total_peers = peer_info["total_peers"]
        outbound_peers = peer_info["outbound_peers"]
        self.assert_true(outbound_peers==self.successful_connections, f"Node{node0.index} should have {outbound_peers} total outbound peers")
        inbound_peers = peer_info["inbound_peers"]
        self.assert_true(inbound_peers==0, f"Node{node0.index} should have {inbound_peers} total inbound peers")
        peer_list = peer_info["peers"]

        self.log_info(
            f"Node{node0.index} peer status: total={total_peers}, "
            f"outbound={outbound_peers}, inbound={inbound_peers}"
        )
        self.log_info(f"Connected peers: {peer_list}")

        # Verify peer counts make sense
        self.assert_true(
            total_peers >= 0,
            "Total peers should be non-negative"
        )

        # Verify ping_roundtrip_time is 0 for all peers (no PING sent yet)
        self.log_info("Verifying ping_roundtrip_time is 0 for all peers (no PING sent yet)...")
        for i, peer in enumerate(peer_list):
            ping_roundtrip_time = peer.get("ping_roundtrip_time", -1)
            self.assert_equal(
                ping_roundtrip_time,
                0,
                f"Peer {i} should have ping_roundtrip_time=0 (no PING sent yet), got {ping_roundtrip_time}"
            )
            self.log_info(f"Peer {i}: ping_roundtrip_time={ping_roundtrip_time} (correct)")

        # Verify nodes are still responsive after peer connections
        self.log_info("Verifying all nodes remain responsive after peer connections...")
        for test_node in self.test_nodes:
            self.assert_true(
                test_node.is_ready(),
                f"Node{test_node.index} should still be responsive"
            )

        self.log_info(
            f"Node{node0.index} outbound connections test completed successfully "
            f"({self.successful_connections}/3 connections established)"
        )

    def test_06_peerinfo_after_addpeer(self):
        """
        Test that connection_time is set after addpeer for all peers.

        This test verifies:
        - Node0 has connections to nodes 1, 2, and 3 (established in setup)
        - Node0's getpeer shows 3 peers with connection_time set
        - Each of nodes 1, 2, 3 show connection_time is set for their connection
        """
        self.log_info("test_06_peerinfo_after_addpeer: Testing connection_time after addpeer...")

        # Connections were established in setup()
        node0 = self.test_nodes[0]
        target_nodes = [
            self.test_nodes[1],
            self.test_nodes[2],
            self.test_nodes[3]
        ]

        # Verify connections were established in setup
        self.assert_true(
            self.successful_connections == 3,
            f"Node0 should have established 3 connections in setup, got {self.successful_connections}"
        )

        # Step 1: Node0 calls getpeer and verifies 3 peers with connection_time set
        self.log_info("Step 1: Node0 querying peer info...")
        node0_peer_info = node0.get_peer_info()

        self.assert_equal(
            node0_peer_info.get("status"),
            "success",
            "Node0 getpeer should return success"
        )

        total_peers = node0_peer_info.get("total_peers", 0)
        self.assert_equal(
            total_peers,
            3,
            f"Node0 should have exactly 3 peers, got {total_peers}"
        )

        peers_list = node0_peer_info.get("peers", [])
        self.assert_equal(
            len(peers_list),
            3,
            f"Node0 peers list should have 3 entries, got {len(peers_list)}"
        )

        # Verify each peer in node0's list has connection_time set
        self.log_info("Verifying Node0's peers have connection_time set...")
        for i, peer in enumerate(peers_list):
            self.assert_in(
                "connection_time",
                peer,
                f"Peer {i} should have connection_time field"
            )

            connection_time = peer.get("connection_time", 0)
            self.assert_true(
                connection_time > 0,
                f"Peer {i} connection_time should be > 0, got {connection_time}"
            )

            self.log_info(
                f"Node0 peer {i}: address={peer.get('address')}, "
                f"port={peer.get('port')}, connection_time={connection_time}"
            )

        # Step 2: Each of nodes 1, 2, 3 call getpeer and verify connection_time is set
        self.log_info("Step 2: Nodes 1, 2, 3 querying their peer info...")
        for peer_node in target_nodes:
            self.log_info(f"Querying Node{peer_node.index} peer info...")
            peer_info = peer_node.get_peer_info()

            self.assert_equal(
                peer_info.get("status"),
                "success",
                f"Node{peer_node.index} getpeer should return success"
            )

            total_peers = peer_info.get("total_peers", 0)
            self.assert_true(
                total_peers == 1,
                f"Node{peer_node.index} should have at least 1 peer (Node0), got {total_peers}"
            )

            peers_list = peer_info.get("peers", [])

            # Debug logging - show all peers with their connection_time
            self.log_info(f"Node{peer_node.index} has {len(peers_list)} peer(s):")
            for idx, peer in enumerate(peers_list):
                self.log_info(
                    f"  Peer {idx}: address={peer.get('address')}, port={peer.get('port')}, "
                    f"connection_time={peer.get('connection_time', 0)}"
                )

            self.assert_true(
                len(peers_list) == 1,
                f"Node{peer_node.index} should have exactly 1 peer in list, got {len(peers_list)}"
            )

            # Find the connection to Node0 (127.0.0.1:port)
            # note that port is not 28333 because it's a peer port, not listner port
            node0_peer = peers_list[0]

            self.assert_true(
                node0_peer is not None,
                f"Node{peer_node.index} should have Node0 (port {node0.p2p_port}) in its peer list"
            )

            # Verify connection_time is set
            self.assert_in(
                "connection_time",
                node0_peer,
                f"Node{peer_node.index}'s connection to Node0 should have connection_time field"
            )

            connection_time = node0_peer.get("connection_time", 0)
            self.assert_true(
                connection_time > 0,
                f"Node{peer_node.index}'s connection to Node0 should have connection_time > 0, got {connection_time}"
            )

            self.log_info(
                f"Node{peer_node.index} connected to Node0: "
                f"address={node0_peer.get('address')}, "
                f"port={node0_peer.get('port')}, "
                f"connection_time={connection_time}"
            )

        self.log_info("test_06_peerinfo_after_addpeer completed successfully")

    def test_07_ping_after_addpeer(self):
        """
        Test that ping_roundtrip_time is set after sending PING to all peers.

        This test verifies:
        - Send /rpc/ping to Node0 to trigger PING messages to all peers
        - Wait 1 second for PONG responses
        - Query Node0's peer info
        - Verify ping_roundtrip_time is NOT 0 for all 3 peers
        """
        self.log_info("test_07_ping_after_addpeer: Testing ping_roundtrip_time after PING...")

        node0 = self.test_nodes[0]

        # Verify connections exist
        self.assert_true(
            self.successful_connections == 3,
            f"Node0 should have 3 connections established, got {self.successful_connections}"
        )

        # Send PING to all peers via /rpc/ping endpoint
        self.log_info("Sending /rpc/ping to Node0...")
        response = node0.post("/rpc/ping")

        self.assert_equal(
            response.status_code,
            200,
            f"/rpc/ping should return 200, got {response.status_code}"
        )

        ping_result = response.json()
        self.assert_equal(
            ping_result.get("status"),
            "success",
            "/rpc/ping should return success status"
        )

        peers_count = ping_result.get("peers_count", 0)
        self.log_info(f"PING sent to {peers_count} peer(s)")
        self.assert_equal(
            peers_count,
            3,
            f"PING should be sent to 3 peers, got {peers_count}"
        )

        # Wait for PONG responses
        self.log_info("Waiting 1 second for PONG responses...")
        time.sleep(1)

        # Query peer info again
        self.log_info("Querying Node0 peer info after PING...")
        peer_info = node0.get_peer_info()

        self.assert_equal(
            peer_info.get("status"),
            "success",
            "get_peer_info should return success"
        )

        peers_list = peer_info.get("peers", [])
        self.assert_equal(
            len(peers_list),
            3,
            f"Node0 should have 3 peers, got {len(peers_list)}"
        )

        # Verify ping_roundtrip_time is NOT 0 for all peers
        self.log_info("Verifying ping_roundtrip_time is NOT 0 for all peers...")
        for i, peer in enumerate(peers_list):
            ping_roundtrip_time = peer.get("ping_roundtrip_time", 0)
            self.assert_true(
                ping_roundtrip_time > 0,
                f"Peer {i} should have ping_roundtrip_time > 0 after PING, got {ping_roundtrip_time}"
            )
            self.log_info(
                f"Peer {i}: address={peer.get('address')}, port={peer.get('port')}, "
                f"ping_roundtrip_time={ping_roundtrip_time} ms"
            )

        self.log_info("test_07_ping_after_addpeer completed successfully")


if __name__ == "__main__":
    unittest.main()
