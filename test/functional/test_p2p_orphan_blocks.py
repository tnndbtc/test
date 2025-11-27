#!/usr/bin/env python3
"""
P2P Orphan Blocks Functional Test for Blockweave.

Tests the orphan blocks mechanism by:
1. Creating a node
2. Manually crafting parent and child blocks
3. Sending child block first (should go to orphan pool)
4. Verifying orphan pool size increases
5. Sending parent block (should process both blocks)
6. Verifying orphan pool size returns to zero

This test verifies that blocks received out-of-order are properly
queued in the orphan pool and processed when their parent arrives.
"""

import sys
import time
import unittest
from test_framework import TestFramework
from test_framework.block_utils import BlockUtils
from test_framework.p2p_utils import MessageType, P2PMessage, P2PConnection


# Removed duplicate MessageType, P2PMessage, and P2PConnection classes
# Now imported from test_framework.p2p_utils


class OrphanBlocksTest(TestFramework):
    """Test orphan blocks mechanism."""

    # Set num_nodes as class attribute
    num_nodes = 1

    def setup(self):
        """Setup test environment - single node."""
        self.log_info("Setup: Node started, ready for orphan blocks test")
        self.node = self.nodes[0]

    def test_1_verify_orphan_size_tracking(self):
        """
        Verify that orphan_blocks_size field is properly tracked and exposed.

        This is a simplified test that verifies:
        1. The orphan_blocks_size field exists in /chain response
        2. It starts at 0
        3. The field is an integer >= 0
        """
        self.log_info("test_1_verify_orphan_size_tracking: Verifying orphan_blocks_size tracking...")

        # Verify orphan_blocks_size is present and valid
        chain_info = self.node.get_chain_info()

        self.assert_in(
            "orphan_blocks_size",
            chain_info,
            "orphan_blocks_size field should be present in /chain response"
        )

        orphan_blocks_size = chain_info["orphan_blocks_size"]

        self.assert_true(
            isinstance(orphan_blocks_size, int),
            f"orphan_blocks_size should be an integer, got {type(orphan_blocks_size)}"
        )

        self.log_info(f"orphan_blocks_size: {orphan_blocks_size}")
        self.assert_true(
            orphan_blocks_size == 0,
            f"orphan_blocks_size should be >= 0, got {orphan_blocks_size}"
        )

        self.log_info("test_1_verify_orphan_size_tracking completed successfully")

    def test_2_orphan_block_via_p2p(self):
        """
        Test orphan blocks mechanism by sending child block before parent via P2P.

        Steps:
        1. Get genesis block hash from node
        2. Create parent block (height 1, references genesis)
        3. Create child block (height 2, references parent)
        4. Connect to node P2P port
        5. Send child block first → should become orphan
        6. Verify orphan_blocks_size = 1
        7. Send parent block → should process both
        8. Verify orphan_blocks_size = 0
        """
        self.log_info("test_2_orphan_block_via_p2p: Testing orphan blocks via P2P...")

        # Step 1: Get genesis block hash
        # For simplicity, use a known genesis hash or create one
        genesis = BlockUtils.create_genesis_block()
        genesis_hash_hex = genesis['hash_hex']
        self.log_info(f"Genesis hash: {genesis_hash_hex[:16]}...")

        # Step 2: Create parent block (height 1)
        self.log_info("Creating parent block (height 1)...")
        parent_block = BlockUtils.create_block(
            prev_hash_hex=genesis_hash_hex,
            height=1,
            miner="test_miner",
            mine=True
        )
        self.log_info(f"Parent block hash: {parent_block['hash_hex'][:16]}...")
        self.log_info(f"Parent block nonce: {parent_block['nonce']}")

        # Step 3: Create child block (height 2)
        self.log_info("Creating child block (height 2)...")
        child_block = BlockUtils.create_block(
            prev_hash_hex=parent_block['hash_hex'],
            height=2,
            miner="test_miner",
            mine=True
        )
        self.log_info(f"Child block hash: {child_block['hash_hex'][:16]}...")
        self.log_info(f"Child block nonce: {child_block['nonce']}")

        # Step 4: Serialize blocks
        parent_serialized = BlockUtils.serialize_block(parent_block)
        child_serialized = BlockUtils.serialize_block(child_block)
        self.log_info(f"Parent block size: {len(parent_serialized)} bytes")
        self.log_info(f"Child block size: {len(child_serialized)} bytes")

        # Step 5: Connect to node P2P port and send child block first
        self.log_info(f"Connecting to node P2P port {self.node.p2p_port}...")

        try:
            with P2PConnection("127.0.0.1", self.node.p2p_port, timeout=10) as conn:
                # Context manager automatically performs VERSION/VERACK handshake
                self.log_info("Connected successfully (handshake completed)")

                # Send child block first (should become orphan)
                self.log_info("Sending CHILD block (should become orphan)...")
                child_msg = P2PMessage(MessageType.BLOCK, child_serialized)
                conn.send_message(child_msg)

                # Wait for node to process
                time.sleep(1)

                # Step 6: Check orphan_blocks_size (should be 1)
                chain_info = self.node.get_chain_info()
                orphan_size_after_child = chain_info.get("orphan_blocks_size", 0)
                self.log_info(f"Orphan blocks size after child: {orphan_size_after_child}")

                self.assert_equal(
                    orphan_size_after_child,
                    1,
                    "Should have 1 orphan block after sending child"
                )

                # Step 7: Send parent block (should process both)
                self.log_info("Sending PARENT block (should process child too)...")
                parent_msg = P2PMessage(MessageType.BLOCK, parent_serialized)
                conn.send_message(parent_msg)

                # Wait for node to process
                time.sleep(1)

                # Step 8: Check orphan_blocks_size (should be 0)
                chain_info = self.node.get_chain_info()
                orphan_size_after_parent = chain_info.get("orphan_blocks_size", 0)
                self.log_info(f"Orphan blocks size after parent: {orphan_size_after_parent}")

                self.assert_equal(
                    orphan_size_after_parent,
                    0,
                    "Should have 0 orphan blocks after sending parent (child should be processed)"
                )

                self.log_info("test_2_orphan_block_via_p2p completed successfully!")

        except AssertionError:
            # Re-raise assertion errors so test failures are properly reported
            raise
        except Exception as e:
            self.log_error(f"P2P connection error: {e}")
            self.log_info("Note: This test requires P2P connection to work.")
            self.log_info("      The orphan blocks mechanism is implemented and exposed via API.")
            # Re-raise the exception so the test fails properly
            raise


if __name__ == "__main__":
    unittest.main()
