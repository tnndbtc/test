#!/usr/bin/env python3
"""
Functional test for /chain endpoint.

Tests the basic functionality of the GET /chain endpoint, which returns
the current state of the blockchain including mempool size and mining status.
"""

import sys
import json
from test_framework import TestFramework, BlockweaveNode


class ChainTest(TestFramework):
    """Test the /chain REST API endpoint."""

    def setup(self):
        """Setup test environment - start a local node."""
        self.log_info("Starting local blockweave node...")
        self.node = BlockweaveNode()

        if not self.node.start(timeout=15):
            raise RuntimeError("Failed to start blockweave node")

        self.log_info("Node started successfully")

    def run_test(self):
        """Run the chain endpoint test."""
        self.log_info("Testing GET /chain endpoint...")

        # Make GET request to /chain
        response = self.node.get("/chain")

        # Test 1: Check status code
        self.assert_equal(
            response.status_code,
            200,
            "GET /chain returns 200 OK"
        )

        # Test 2: Check Content-Type header
        content_type = response.headers.get("Content-Type", "")
        self.assert_in(
            "application/json",
            content_type,
            "Response Content-Type is application/json"
        )

        # Test 3: Parse JSON response
        try:
            data = response.json()
            self.log_info(f"Response data: {json.dumps(data, indent=2)}")
        except json.JSONDecodeError as e:
            self.assert_true(False, f"Response is valid JSON (failed: {e})")
            return

        self.assert_true(True, "Response is valid JSON")

        # Test 4: Check required fields exist
        self.assert_in(
            "mempool_size",
            data,
            "Response contains 'mempool_size' field"
        )

        self.assert_in(
            "mining_enabled",
            data,
            "Response contains 'mining_enabled' field"
        )

        # Test 5: Check field types
        mempool_size = data.get("mempool_size")
        self.assert_true(
            isinstance(mempool_size, int),
            f"mempool_size is an integer (got: {type(mempool_size).__name__})"
        )

        mining_enabled = data.get("mining_enabled")
        self.assert_true(
            isinstance(mining_enabled, bool),
            f"mining_enabled is a boolean (got: {type(mining_enabled).__name__})"
        )

        # Test 6: Check field values are reasonable
        self.assert_true(
            mempool_size >= 0,
            f"mempool_size is non-negative (got: {mempool_size})"
        )

        self.log_info(f"Mempool size: {mempool_size}")
        self.log_info(f"Mining enabled: {mining_enabled}")

        # Test 7: Make second request to ensure consistency
        self.log_info("Making second request to verify endpoint stability...")
        response2 = self.node.get("/chain")
        self.assert_equal(
            response2.status_code,
            200,
            "Second GET /chain also returns 200 OK"
        )

        data2 = response2.json()
        self.assert_true(
            "mempool_size" in data2 and "mining_enabled" in data2,
            "Second response also contains required fields"
        )

    def cleanup(self):
        """Cleanup - stop the node."""
        if self.node:
            self.log_info("Stopping blockweave node...")
            self.node.stop()
            self.log_info("Node stopped")


if __name__ == "__main__":
    test = ChainTest()
    sys.exit(test.main())
