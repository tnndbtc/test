#!/usr/bin/env python3
"""
Blockcore functional test for Blockweave.

Tests the core blockchain functionality including transaction creation,
mempool management, and block mining.
"""

import sys
import time
import unittest
from test_framework import TestFramework


class BlockcoreTest(TestFramework):
    """Test blockcore functionality including transactions and mining."""

    num_nodes = 1

    def setup(self):
        """Setup test environment - start a local node."""
        # Node is automatically started by framework via num_nodes = 1
        # Access it via self.nodes[0]
        self.node = self.nodes[0] if self.nodes else None
        if not self.node:
            raise RuntimeError("Failed to start blockweave node")

    def test_transaction_and_mining(self):
        """Create a transaction and verify a block is mined."""
        self.log_info("test_transaction_and_mining: Testing transaction creation and mining...")

        # Step 1: Get initial chain state
        self.log_info("Step 1: Getting initial chain state...")
        initial_state = self.node.get_chain_info()
        self.assert_equal(initial_state.get("status"), "success", "get_chain_info() returns success")

        initial_mempool_size = initial_state.get("mempool_size", -1)
        initial_mining_enabled = initial_state.get("mining_enabled", False)
        initial_blocks = initial_state.get('blocks', -1)


        self.log_info(f"Initial state - mempool: {initial_mempool_size}, mining: {initial_mining_enabled}")

        # Step 2: Create and submit a transaction
        self.log_info("Step 2: Creating transaction...")

        # Transaction data - simple text data for testing
        # The /transaction endpoint requires 'from', 'to', and 'data' fields
        # 'fee' is optional (defaults to 0)
        transaction_data = {
            "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
            "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
            "data": "Test transaction for blockcore mining verification",
            "fee": 1000
        }

        # Submit transaction
        tx_id = None
        try:
            success = self.node.create_transaction(transaction_data)
            self.assert_true(success, "Transaction submission should succeed")

            # Extract transaction ID
            # tx_id = tx_response.get("transaction_id")
            # self.log_info(f"Transaction ID: {tx_id}")

        except Exception as e:
            self.assert_true(False, f"Error submitting transaction: {e}")

        # Step 3: Verify mempool size increased
        self.log_info("Step 3: Verifying transaction is in mempool...")
        state_after_tx = self.node.get_chain_info()
        mempool_size_after_tx = state_after_tx.get("mempool_size", 0)

        self.log_info(f"Mempool size after transaction: {mempool_size_after_tx}")

        if mempool_size_after_tx == initial_mempool_size + 1 :
            self.assert_true(
                True,
                f"Transaction added to mempool (size: {initial_mempool_size} -> {mempool_size_after_tx})"
            )
        else:
            self.log_info(
                f"Warning: Mempool size did not increase, or mining happened too quick"
                f"(before: {initial_mempool_size}, after: {mempool_size_after_tx})"
            )

        # Step 4: mine immediately
        self.log_info("Step 4: Mine immediately")
        self.assert_true(self.node.trigger_mining(), f"Block mining triggered successfully")

        # Step 5: Verify mining result
        final_state = self.node.get_chain_info()
        final_blocks = final_state.get('blocks', -1)
        self.assert_equal(final_blocks, initial_blocks + 1, f"blocks size should increase by 1")
        final_mempool_size = final_state.get("mempool_size", -1)
        self.assert_true(final_mempool_size == 0, f"mempool size should be 0")

        # Optionally verify transaction was included in a block
        if tx_id:
            self.log_info(f"Verifying transaction {tx_id} was mined...")

    def test_invalid_transaction(self):
        """Test that submitting transaction with only data field returns bad request."""
        self.log_info("test_invalid_transaction: Testing invalid transaction submission...")

        # Transaction with only 'data' field (missing required 'from' and 'to')
        invalid_transaction_data = {
            "data": "Test transaction with missing fields"
        }

        endpoint = "/rpc/transaction"
        self.log_info(f"Attempting to submit invalid transaction to {endpoint}...")
        response = self.node.post(endpoint, json_data=invalid_transaction_data)

        # Should return 400 Bad Request
        self.assert_equal(
            response.status_code,
            400,
            "POST /rpc/transaction with missing 'from' and 'to' returns 400 Bad Request"
        )

        # Parse error response
        try:
            error_data = response.json()
            self.log_info(f"Error response: {error_data}")
        except Exception as e:
            self.assert_true(False, f"Error response is valid JSON (failed: {e})")
            return

        # Verify error message mentions missing field
        error_message = error_data.get("message", "")
        self.assert_true(
            "from" in error_message.lower() or "to" in error_message.lower(),
            f"Error message mentions missing 'from' or 'to' field: {error_message}"
        )

        self.log_info("Invalid transaction correctly rejected")

    def cleanup(self):
        """Cleanup - stop the node."""
        if self.node:
            self.log_info("Stopping blockweave node...")
            self.node.stop()
            self.log_info("Node stopped")


if __name__ == "__main__":
    unittest.main()
