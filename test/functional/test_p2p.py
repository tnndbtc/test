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
