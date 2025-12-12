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

    num_nodes = 1

    def setup(self):
        """Setup test environment - start a local node."""
        # Node is automatically started by framework via num_nodes = 1
        # Access it via self.nodes[0]
        self.node = self.nodes[0] if self.nodes else None
        if not self.node:
            raise RuntimeError("Failed to start blockweave node")

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

    def test_get_transaction_endpoint(self):
        """Test GET /transaction endpoint - retrieve transaction by hash."""
        self.log_info("Testing GET /transaction endpoint...")

        # Step 1: Create a transaction
        tx_data = {
            "from": "test_sender",
            "to": "test_receiver",
            "data": "test_transaction_data",
            "fee": 100
        }

        success, tx_response = self.node.create_transaction(tx_data)
        self.assert_true(
            success,
            "Transaction creation should succeed"
        )
        self.assert_in(
            "transaction_id",
            tx_response,
            "Transaction response contains transaction_id"
        )
        tx_hash = tx_response["transaction_id"]
        self.log_info(f"Created transaction with hash: {tx_hash}")

        # Step 2: Retrieve the transaction using GET /transaction
        success, data = self.node.get_transaction(tx_hash)
        self.assert_true(
            success,
            "Transaction retrieval should succeed"
        )

        # Step 3: Validate response structure
        self.log_info(f"Transaction data: {json.dumps(data, indent=2)}")

        # Check for status field
        self.assert_in(
            "status",
            data,
            "Response contains 'status' field"
        )
        self.assert_equal(
            data["status"],
            "success",
            "Status is 'success'"
        )

        # Check for transaction fields
        self.assert_in(
            "transaction_id",
            data,
            "Response contains 'transaction_id' field"
        )
        self.assert_in(
            "owner",
            data,
            "Response contains 'owner' field"
        )
        self.assert_in(
            "target",
            data,
            "Response contains 'target' field"
        )
        self.assert_in(
            "data_hex",
            data,
            "Response contains 'data_hex' field"
        )
        self.assert_in(
            "data_size",
            data,
            "Response contains 'data_size' field"
        )
        self.assert_in(
            "reward",
            data,
            "Response contains 'reward' field"
        )
        self.assert_in(
            "timestamp",
            data,
            "Response contains 'timestamp' field"
        )
        self.assert_in(
            "type",
            data,
            "Response contains 'type' field"
        )

        # Validate transaction hash matches
        self.assert_equal(
            data["transaction_id"],
            tx_hash,
            "Transaction ID matches requested hash"
        )

        self.log_info("GET /transaction test passed!")

    def test_get_transaction_not_found(self):
        """Test GET /transaction with non-existent transaction hash."""
        self.log_info("Testing GET /transaction with non-existent hash...")

        # Use a valid hex string but non-existent transaction
        fake_hash = "0" * 64
        success, data = self.node.get_transaction(fake_hash)

        self.assert_true(
            not success,
            "Transaction retrieval should fail for non-existent transaction"
        )
        self.assert_true(
            data is None,
            "Response data should be None for non-existent transaction"
        )

    def test_get_block_endpoint(self):
        """Test GET /block endpoint - retrieve block by hash."""
        self.log_info("Testing GET /block endpoint...")

        # Step 1: Get chain info to find genesis block
        chain_response = self.node.get("/chain")
        self.assert_equal(
            chain_response.status_code,
            200,
            "GET /chain returns 200 OK"
        )

        chain_data = chain_response.json()
        self.log_info(f"Chain data: {json.dumps(chain_data, indent=2)}")

        # Try to get the genesis block hash or create a block
        # For now, let's create a transaction and mine a block
        tx_data = {
            "from": "block_test_sender",
            "to": "block_test_receiver",
            "data": "block_test_data",
            "fee": 200
        }

        success, tx_response = self.node.create_transaction(tx_data)
        self.assert_true(
            success,
            "Transaction creation should succeed"
        )

        # Trigger mining to create a block
        # Note: Using post() directly here because we need the block_hash from response
        # trigger_mining() only returns bool, not the block hash
        mine_response = self.node.post("/rpc/minetrigger")
        self.assert_equal(
            mine_response.status_code,
            200,
            "POST /rpc/minetrigger returns 200 OK"
        )

        mine_data = mine_response.json()
        if "block_hash" in mine_data:
            block_hash = mine_data["block_hash"]
            self.log_info(f"Mined block with hash: {block_hash}")

            # Step 2: Retrieve the block using GET /block
            response = self.node.get(f"/block?hash={block_hash}")
            self.assert_equal(
                response.status_code,
                200,
                "GET /block returns 200 OK"
            )

            # Step 3: Validate response structure
            data = response.json()
            self.log_info(f"Block data: {json.dumps(data, indent=2)}")

            # Check for status field
            self.assert_in(
                "status",
                data,
                "Response contains 'status' field"
            )
            self.assert_equal(
                data["status"],
                "success",
                "Status is 'success'"
            )

            # Check for block fields
            self.assert_in(
                "block_hash",
                data,
                "Response contains 'block_hash' field"
            )
            self.assert_in(
                "height",
                data,
                "Response contains 'height' field"
            )
            self.assert_in(
                "timestamp",
                data,
                "Response contains 'timestamp' field"
            )
            self.assert_in(
                "nonce",
                data,
                "Response contains 'nonce' field"
            )
            self.assert_in(
                "miner",
                data,
                "Response contains 'miner' field"
            )
            self.assert_in(
                "difficulty",
                data,
                "Response contains 'difficulty' field"
            )
            self.assert_in(
                "cumulative_data_size",
                data,
                "Response contains 'cumulative_data_size' field"
            )
            self.assert_in(
                "previous_block",
                data,
                "Response contains 'previous_block' field"
            )
            self.assert_in(
                "transactions",
                data,
                "Response contains 'transactions' field"
            )

            # Validate block hash matches
            self.assert_equal(
                data["block_hash"],
                block_hash,
                "Block hash matches requested hash"
            )

            # Validate transactions is an array
            self.assert_true(
                isinstance(data["transactions"], list),
                "Transactions field is an array"
            )

            self.log_info("GET /block test passed!")
        else:
            self.log_info("Mining did not return block_hash, skipping block retrieval test")

    def test_get_block_not_found(self):
        """Test GET /block with non-existent block hash."""
        self.log_info("Testing GET /block with non-existent hash...")

        # Use a valid hex string but non-existent block
        fake_hash = "f" * 64
        response = self.node.get(f"/block?hash={fake_hash}")

        self.assert_equal(
            response.status_code,
            404,
            "GET /block returns 404 for non-existent block"
        )

        data = response.json()
        self.assert_in(
            "error",
            data,
            "Error response contains 'error' field"
        )

    def cleanup(self):
        """Cleanup - stop the node."""
        if self.node:
            self.log_info("Stopping blockweave node...")
            self.node.stop()
            self.log_info("Node stopped")


if __name__ == "__main__":
    unittest.main()
