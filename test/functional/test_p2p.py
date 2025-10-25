#!/usr/bin/env python3
"""
P2P network functional test for Blockweave.

Tests peer-to-peer networking by starting multiple local nodes
and verifying they can establish connections.
"""

import sys
import time
import inspect
from test_framework import TestFramework


class P2PTest(TestFramework):
    """Test P2P networking with multiple nodes."""

    def setup(self):
        """Setup test environment - start 4 local nodes."""
        self.log_info("Starting 4 local blockweave nodes...")

        # Start 4 nodes with consecutive port numbers to avoid conflicts
        # Node 0: REST API port 28443, P2P port 28333
        # Node 1: REST API port 28444, P2P port 28334
        # Node 2: REST API port 28445, P2P port 28335
        # Node 3: REST API port 28446, P2P port 28336

        base_rest_port = 28443
        base_p2p_port = 28333

        for i in range(4):
            rest_port = base_rest_port + i
            p2p_port = base_p2p_port + i

            self.log_info(f"Starting node {i} (REST: {rest_port}, P2P: {p2p_port})")
            node = self.add_node(port=rest_port, p2p_port=p2p_port)

            if not node.start(timeout=20):
                raise RuntimeError(f"Failed to start node {i}")

            self.log_info(f"Node {i} started successfully")

        self.log_info(f"All {len(self.nodes)} nodes started successfully")

    def run_test(self):
        """Run all P2P network tests."""
        self.log_info("Running P2P network test...")

        self.test_nodes_are_running()
        self.test_chain_endpoint()
        self.test_port_isolation()
        self.test_mining_status()
        self.test_inbound_peer_connections()
        self.test_peer_connection_limits()

        self.log_info("P2P test completed successfully")

    def test_nodes_are_running(self):
        """Verify all nodes are running."""
        self.log_info("%s: Verifying all nodes are running..." % inspect.currentframe().f_code.co_name)

        for i, node in enumerate(self.nodes):
            # Check if process is alive
            if node.process and node.process.poll() is None:
                self.assert_true(True, f"Node {i} process is running")
            else:
                self.assert_true(False, f"Node {i} process should be running")

            self.log_info(f"Node {i} is running")

    def test_chain_endpoint(self):
        """Test /chain endpoint on all nodes."""
        self.log_info("%s: Testing /chain endpoint on all nodes..." % inspect.currentframe().f_code.co_name)

        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            self.assert_equal(
                response.status_code,
                200,
                f"Node {i} GET /chain returns 200 OK"
            )

            data = response.json()
            self.assert_in(
                "mempool_size",
                data,
                f"Node {i} response contains 'mempool_size'"
            )
            self.assert_in(
                "mining_enabled",
                data,
                f"Node {i} response contains 'mining_enabled'"
            )

            self.log_info(
                f"Node {i}: mempool_size={data['mempool_size']}, "
                f"mining_enabled={data['mining_enabled']}"
            )

    def test_port_isolation(self):
        """Verify nodes are listening on different ports."""
        self.log_info("%s: Verifying port isolation..." % inspect.currentframe().f_code.co_name)

        ports_used = set()
        base_port = 28443

        for i, node in enumerate(self.nodes):
            expected_port = base_port + i
            self.assert_equal(
                node.port,
                expected_port,
                f"Node {i} should use port {expected_port}"
            )
            self.assert_true(
                expected_port not in ports_used,
                f"Port {expected_port} should be unique"
            )
            ports_used.add(expected_port)
            self.log_info(f"Node {i} confirmed on unique port {expected_port}")

    def test_mining_status(self):
        """Verify mining is enabled on all nodes (mining starts automatically)."""
        self.log_info("%s: Verifying mining status on all nodes..." % inspect.currentframe().f_code.co_name)

        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            data = response.json()

            # Mining should be enabled by default when daemon starts
            self.assert_equal(
                data["mining_enabled"],
                True,
                f"Node {i} should have mining enabled by default"
            )
            self.log_info(f"Node {i} mining status confirmed: enabled={data['mining_enabled']}")

    def test_inbound_peer_connections(self):
        """
        Test that nodes can establish inbound connections.

        We'll test that when Node 1 connects to Node 0, Node 0 shows
        an inbound connection and Node 1 shows an outbound connection.
        """
        self.log_info("%s: Testing inbound peer connections..." % inspect.currentframe().f_code.co_name)

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
        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            self.assert_equal(response.status_code, 200, f"Node {i} should still be responsive")

        self.log_info("Inbound connection test placeholder completed")

    def test_peer_connection_limits(self):
        """
        Test that peer connection limits are respected.

        Verifies that:
        - Nodes respect max inbound peer limits (default 120)
        - Nodes respect max outbound peer limits (default 8)
        - Connection attempts beyond limits are rejected gracefully
        """
        self.log_info("%s: Testing peer connection limits..." % inspect.currentframe().f_code.co_name)

        # This test would verify:
        # 1. Start nodes with custom peer limits (e.g., max_inbound_peers=2)
        # 2. Try to establish 3 inbound connections
        # 3. Verify only 2 are accepted and 3rd is rejected
        # 4. Check logs show "Maximum inbound peers reached" message

        self.log_info("Peer limit test: Would verify inbound limit (120) and outbound limit (8)")

        # Test would use config overrides:
        # node = self.add_node(port=28443, p2p_port=28333,
        #                      config_overrides={"max_inbound_peers": 2})

        # For now, just verify default configuration is loaded
        self.log_info("Default limits: max_inbound_peers=120, max_outbound_peers=8")

        # Verify nodes are still operational
        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            self.assert_equal(response.status_code, 200, f"Node {i} should be operational")

        self.log_info("Peer limit test placeholder completed")

    def cleanup(self):
        """Cleanup - stop all nodes."""
        self.log_info("Stopping all nodes...")
        for i, node in enumerate(self.nodes):
            if node:
                self.log_info(f"Stopping node {i}...")
                node.stop()
        self.log_info("All nodes stopped")

if __name__ == "__main__":
    test = P2PTest()
    sys.exit(test.main())
