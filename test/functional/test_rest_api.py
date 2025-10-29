#!/usr/bin/env python3
"""
Functional test for /chain endpoint.

Tests the basic functionality of the GET /chain endpoint, which returns
the current state of the blockchain including mempool size and mining status.
"""

import sys
import json
import unittest
from test_framework import TestFramework, BlockweaveNode


class RestTest(TestFramework):
    """Test the /chain REST API endpoint."""

    def setup(self):
        """Setup test environment - start a local node."""
        self.log_info("Starting local blockweave node...")
        self.node = self.add_node()

        if not self.node.start(timeout=15):
            raise RuntimeError("Failed to start blockweave node")

        self.log_info("Node started successfully")

    def test_chain_endpoint_full(self):
        """Test GET /chain endpoint - comprehensive test."""
        self.log_info("Testing GET /chain endpoint...")

        # Test 1: Check status code and get response
        response = self.node.get("/chain")
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
            self.fail(f"Response is not valid JSON: {e}")
            return

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

    def test_endpoint_stability(self):
        """Test that endpoint returns consistent structure on multiple requests."""
        self.log_info("Making second request to verify endpoint stability...")
        response = self.node.get("/chain")
        self.assert_equal(
            response.status_code,
            200,
            "Second GET /chain also returns 200 OK"
        )

        data = response.json()
        self.assert_true(
            "mempool_size" in data and "mining_enabled" in data,
            "Second response also contains required fields"
        )

    def test_invalid_endpoint(self):
        """Test that requesting an invalid endpoint returns 404 Not Found."""
        self.log_info("Testing GET /invalid_endpoint (should return 404)...")
        response = self.node.get("/invalid_endpoint")

        # Check status code is 404 Not Found
        self.assert_equal(
            response.status_code,
            404,
            "GET /invalid_endpoint returns 404 Not Found"
        )

        # Check Content-Type is still application/json
        content_type = response.headers.get("Content-Type", "")
        self.assert_in(
            "application/json",
            content_type,
            "Error response Content-Type is application/json"
        )

        # Parse JSON error response
        try:
            data = response.json()
            self.log_info(f"Error response data: {json.dumps(data, indent=2)}")
        except json.JSONDecodeError as e:
            self.fail(f"Error response is not valid JSON: {e}")
            return

        # Check that response contains error field
        self.assert_in(
            "error",
            data,
            "Error response contains 'error' field"
        )

        self.log_info(f"Error message: {data.get('error')}")

    def cleanup(self):
        """Cleanup - stop the node."""
        if self.node:
            self.log_info("Stopping blockweave node...")
            self.node.stop()
            self.log_info("Node stopped")


if __name__ == "__main__":
    unittest.main()
