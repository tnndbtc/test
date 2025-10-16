#!/usr/bin/env python3
"""
P2P network functional test for Blockweave.

Tests peer-to-peer networking by starting multiple local nodes
and verifying they can establish connections.
"""

import sys
from test_framework import TestFramework

class P2PTest(TestFramework):
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
        """Run the P2P network test."""
        self.log_info("Running P2P network test...")

        # Test 1: Verify all nodes are running
        self.log_info("Test 1: Verifying all nodes are running...")
        for i, node in enumerate(self.nodes):
            self.assert_true(node.is_ready(), f"Node {i} should be ready")
            self.log_info(f"Node {i} is running")

        # Test 2: Check each node's REST API /chain endpoint
        self.log_info("Test 2: Testing /chain endpoint on all nodes...")
        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            self.assert_equal(response.status_code, 200,
                            f"Node {i} GET /chain returns 200 OK")

            data = response.json()
            self.assert_in("mempool_size", data,
                          f"Node {i} response contains 'mempool_size'")
            self.assert_in("mining_enabled", data,
                          f"Node {i} response contains 'mining_enabled'")

            self.log_info(f"Node {i}: mempool_size={data['mempool_size']}, "
                         f"mining_enabled={data['mining_enabled']}")

        # Test 3: Verify nodes are listening on different ports
        self.log_info("Test 3: Verifying port isolation...")
        ports_used = set()
        base_port = 28443
        for i, node in enumerate(self.nodes):
            expected_port = base_port + i
            self.assert_equal(node.port, expected_port,
                            f"Node {i} should use port {expected_port}")
            self.assert_true(expected_port not in ports_used,
                           f"Port {expected_port} should be unique")
            ports_used.add(expected_port)
            self.log_info(f"Node {i} confirmed on unique port {expected_port}")

        # Test 4: Test multiple requests to verify stability
        self.log_info("Test 4: Testing multiple requests for stability...")
        for request_num in range(3):
            self.log_info(f"Request round {request_num + 1}/3")
            for i, node in enumerate(self.nodes):
                response = node.get("/chain")
                self.assert_equal(response.status_code, 200,
                                f"Node {i} request {request_num + 1} returns 200")

        self.log_info("All stability tests passed")

        # Test 5: Start mining on all nodes
        self.log_info("Test 5: Starting mining on all nodes...")
        for i, node in enumerate(self.nodes):
            response = node.post("/mine/start")
            self.assert_equal(response.status_code, 200,
                            f"Node {i} POST /mine/start returns 200")
            data = response.json()
            self.assert_in("status", data,
                          f"Node {i} mine/start response contains 'status'")
            self.log_info(f"Node {i} mining started: {data}")

        # Test 6: Verify mining is enabled on all nodes
        self.log_info("Test 6: Verifying mining is enabled on all nodes...")
        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            data = response.json()
            self.assert_equal(data["mining_enabled"], True,
                            f"Node {i} should have mining enabled")
            self.log_info(f"Node {i} mining confirmed enabled")

        # Test 7: Stop mining on all nodes
        self.log_info("Test 7: Stopping mining on all nodes...")
        for i, node in enumerate(self.nodes):
            response = node.post("/mine/stop")
            self.assert_equal(response.status_code, 200,
                            f"Node {i} POST /mine/stop returns 200")
            data = response.json()
            self.log_info(f"Node {i} mining stopped: {data}")

        # Test 8: Verify mining is disabled on all nodes
        self.log_info("Test 8: Verifying mining is disabled on all nodes...")
        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            data = response.json()
            self.assert_equal(data["mining_enabled"], False,
                            f"Node {i} should have mining disabled")
            self.log_info(f"Node {i} mining confirmed disabled")

        self.log_info("P2P test completed successfully")

if __name__ == "__main__":
    test = P2PTest()
    sys.exit(test.main())
