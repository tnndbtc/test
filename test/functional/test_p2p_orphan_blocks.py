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
import json
import os
from test_framework import TestFramework
from test_framework.block_utils import BlockUtils
from test_framework.p2p_utils import MessageType, P2PMessage, P2PConnection
from test_framework.transaction_utils import TransactionHelper


# Removed duplicate MessageType, P2PMessage, and P2PConnection classes
# Now imported from test_framework.p2p_utils


class OrphanBlocksTest(TestFramework):
    """Test orphan blocks mechanism."""

    # Set num_nodes as class attribute
    num_nodes = 1

    # Class-level attributes that persist across all test methods
    # These are shared by all test instances to maintain blockchain state
    next_block_height = None
    genesis = None
    latest_block = None

    def setup(self):
        """Setup test environment - single node."""
        self.log_info("Setup: Node started, ready for orphan blocks test")
        self.node = self.nodes[0]

        # Initialize transaction helper with keystore
        keystore_path = os.path.join(
            os.path.dirname(__file__),
            "test_1234.json"
        )

        if not os.path.exists(keystore_path):
            raise FileNotFoundError(f"Keystore not found: {keystore_path}")

        self.tx_helper = TransactionHelper(keystore_path, password="1234")
        self.log_info(f"Initialized with account: {self.tx_helper.account.address}")

        # Initialize class-level attributes only once (first time setup() is called)
        if OrphanBlocksTest.next_block_height is None:
            # Global block number counter to track the next block height
            # This ensures all test cases use correct block heights
            # Initialize to 1 (since genesis is at height 0)
            OrphanBlocksTest.next_block_height = 1

            # Create genesis block once in setup, so all test cases can reuse it
            OrphanBlocksTest.genesis = BlockUtils.create_genesis_block()
            self.log_info(f"Genesis block created in setup: {OrphanBlocksTest.genesis['hash_hex']}...")

            # Track the latest block in the blockchain
            # Test cases should create new blocks pointing to latest_block
            # and update latest_block at the end of each test case
            OrphanBlocksTest.latest_block = OrphanBlocksTest.genesis
        else:
            self.log_info(f"Reusing genesis block from previous test: {OrphanBlocksTest.genesis['hash_hex']}...")
            self.log_info(f"Current block height counter: {OrphanBlocksTest.next_block_height}")
            self.log_info(f"Current latest_block height: {OrphanBlocksTest.latest_block['height']}")

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
            f"orphan_blocks_size should be 0, got {orphan_blocks_size}"
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

        # Step 1: Use genesis block from setup (class-level attribute)
        self.log_info(f"Using genesis from setup: {OrphanBlocksTest.genesis['hash_hex']}...")

        # Step 2: Create parent block pointing to latest_block (class-level attribute)
        parent_height = OrphanBlocksTest.next_block_height
        self.log_info(f"Creating parent block (height {parent_height})...")
        parent_block = BlockUtils.create_block(
            prev_hash_hex=OrphanBlocksTest.latest_block['hash_hex'],
            height=parent_height,
            miner="test_miner",
            mine=True
        )
        self.log_info(f"Parent block hash: {parent_block['hash_hex']}...")
        self.log_info(f"Parent block nonce: {parent_block['nonce']}")
        OrphanBlocksTest.next_block_height += 1

        # Step 3: Create child block using global block height counter (class-level attribute)
        child_height = OrphanBlocksTest.next_block_height
        self.log_info(f"Creating child block (height {child_height})...")
        child_block = BlockUtils.create_block(
            prev_hash_hex=parent_block['hash_hex'],
            height=child_height,
            miner="test_miner",
            mine=True
        )
        self.log_info(f"Child block hash: {child_block['hash_hex']}...")
        self.log_info(f"Child block nonce: {child_block['nonce']}")
        OrphanBlocksTest.next_block_height += 1

        # Step 4: Serialize blocks
        parent_serialized = BlockUtils.serialize_block(parent_block)
        child_serialized = BlockUtils.serialize_block(child_block)
        self.log_info(f"Parent block size: {len(parent_serialized)} bytes")
        self.log_info(f"Child block size: {len(child_serialized)} bytes")

        # Step 5: Connect to node P2P port and send child block first
        self.log_info(f"Connecting to node P2P port {self.node.p2p_port}...")

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

        # Update class-level latest_block to point to the child block (last block in chain)
        OrphanBlocksTest.latest_block = child_block
        self.log_info(f"Updated latest_block to child block (height {child_block['height']}), next_block_height is {OrphanBlocksTest.next_block_height}")

    def test_3_chain_of_orphans(self):
        """
        Test chain of 3 orphan blocks arriving in reverse order.

        Steps:
        1. Create a chain of 4 blocks (parent → orphan1 → orphan2 → orphan3)
        2. Send orphan3, orphan2, orphan1 (all become orphans)
        3. Send parent → should process orphan1
        4. Processing orphan1 triggers processing orphan2
        5. Processing orphan2 triggers processing orphan3
        6. Verify all blocks are on disk and orphan pool is empty

        Note: Based on testing, orphan processing might happen one level at a time,
        so we verify intermediate states.
        """
        self.log_info("test_3_chain_of_orphans: Testing chain of orphan blocks...")

        # Use genesis block from setup (class-level attribute)
        self.log_info(f"Using genesis from setup: {OrphanBlocksTest.genesis['hash_hex']}... latest_block height: {OrphanBlocksTest.latest_block['height']} and next_block_height: {OrphanBlocksTest.next_block_height}")

        # Create block chain pointing to latest_block (class-level attribute)
        # Use global block height counter (class-level attribute)
        parent_height = OrphanBlocksTest.next_block_height
        self.log_info(f"Creating parent block (height {parent_height})...")
        parent_block = BlockUtils.create_block(
            prev_hash_hex=OrphanBlocksTest.latest_block['hash_hex'],
            height=parent_height,
            miner="test_3_test_miner_parent",
            mine=True
        )
        self.log_info(f"Parent block hash: {parent_block['hash_hex']}... miner: test_3_test_miner_parent")
        OrphanBlocksTest.next_block_height += 1

        orphan1_height = OrphanBlocksTest.next_block_height
        self.log_info(f"Creating orphan_block1 (height {orphan1_height})...")
        orphan_block1 = BlockUtils.create_block(
            prev_hash_hex=parent_block['hash_hex'],
            height=orphan1_height,
            miner="test_3_test_miner1",
            mine=True
        )
        self.log_info(f"Orphan1 block hash: {orphan_block1['hash_hex']}... miner: test_3_test_miner1")
        OrphanBlocksTest.next_block_height += 1

        orphan2_height = OrphanBlocksTest.next_block_height
        self.log_info(f"Creating orphan_block2 (height {orphan2_height})...")
        orphan_block2 = BlockUtils.create_block(
            prev_hash_hex=orphan_block1['hash_hex'],
            height=orphan2_height,
            miner="test_3_test_miner2",
            mine=True
        )
        self.log_info(f"Orphan2 block hash: {orphan_block2['hash_hex']}... miner: test_3_test_miner2")
        OrphanBlocksTest.next_block_height += 1

        orphan3_height = OrphanBlocksTest.next_block_height
        self.log_info(f"Creating orphan_block3 (height {orphan3_height})...")
        orphan_block3 = BlockUtils.create_block(
            prev_hash_hex=orphan_block2['hash_hex'],
            height=orphan3_height,
            miner="test_3_test_miner3",
            mine=True
        )
        self.log_info(f"Orphan3 block hash: {orphan_block3['hash_hex']}... miner: test_3_test_miner3")
        OrphanBlocksTest.next_block_height += 1

        # Serialize all blocks
        parent_serialized = BlockUtils.serialize_block(parent_block)
        orphan1_serialized = BlockUtils.serialize_block(orphan_block1)
        orphan2_serialized = BlockUtils.serialize_block(orphan_block2)
        orphan3_serialized = BlockUtils.serialize_block(orphan_block3)

        # Send blocks in reverse order via P2P
        self.log_info(f"Connecting to node P2P port {self.node.p2p_port}...")

        with P2PConnection("127.0.0.1", self.node.p2p_port, timeout=10) as conn:
            self.log_info("Connected successfully (handshake completed)")

            # Send orphan_block3 (missing parent: orphan_block2)
            self.log_info("Sending orphan_block3 (should become orphan)...")
            msg3 = P2PMessage(MessageType.BLOCK, orphan3_serialized)
            conn.send_message(msg3)
            time.sleep(1)

            chain_info = self.node.get_chain_info()
            orphan_size = chain_info.get("orphan_blocks_size", 0)
            self.log_info(f"Orphan blocks size after orphan3: {orphan_size}")
            self.assert_equal(orphan_size, 1, "Should have 1 orphan after sending orphan3")

            # Send orphan_block2 (missing parent: orphan_block1)
            self.log_info("Sending orphan_block2 (should become orphan)...")
            msg2 = P2PMessage(MessageType.BLOCK, orphan2_serialized)
            conn.send_message(msg2)
            time.sleep(1)

            chain_info = self.node.get_chain_info()
            orphan_size = chain_info.get("orphan_blocks_size", 0)
            self.log_info(f"Orphan blocks size after orphan2: {orphan_size}")
            self.assert_equal(orphan_size, 2, "Should have 2 orphans after sending orphan2")

            # Send orphan_block1 (missing parent: parent_block)
            self.log_info("Sending orphan_block1 (should become orphan)...")
            msg1 = P2PMessage(MessageType.BLOCK, orphan1_serialized)
            conn.send_message(msg1)
            time.sleep(1)

            chain_info = self.node.get_chain_info()
            orphan_size = chain_info.get("orphan_blocks_size", 0)
            self.log_info(f"Orphan blocks size after orphan1: {orphan_size}")
            self.assert_equal(orphan_size, 3, "Should have 3 orphans after sending orphan1")

            # Send parent_block (triggers recursive processing)
            self.log_info("Sending parent_block (should recursively process all orphans)...")
            msg_parent = P2PMessage(MessageType.BLOCK, parent_serialized)
            conn.send_message(msg_parent)

            # Check orphan processing
            time.sleep(1)

            # Verify all orphans processed
            chain_info = self.node.get_chain_info()
            orphan_size = chain_info.get("orphan_blocks_size", -1)
            blocks_count = chain_info.get("blocks", -1)
            # height = chain_info.get("height", 0)
            mempool_size = chain_info.get("mempool_size", -1)

            self.log_info(f"Final orphan blocks size: {orphan_size}")
            self.log_info(f"Blocks on disk: {blocks_count}")
            # self.log_info(f"Chain height: {height}")
            self.log_info(f"Mempool size: {mempool_size}")

            # Verify orphan pool decreased (at least one level processed)
            self.assert_true(orphan_size == 0 , f"Orphan pool should decrease to 0, got {orphan_size}")
            self.assert_true(blocks_count == 7, f"Should have at least 2 blocks processed, got {blocks_count}")

            self.log_info("test_3_chain_of_orphans completed successfully!")

        # Update class-level latest_block to point to orphan_block3 (last block in chain)
        OrphanBlocksTest.latest_block = orphan_block3
        self.log_info(f"Updated latest_block to orphan_block3 (height {orphan_block3['height']}), next_block_height is {OrphanBlocksTest.next_block_height}")

    def test_4_block_with_tx(self):
        """
        Test orphan blocks containing transactions.

        Verifies that:
        1. Transactions can be sent via P2P TX messages
        2. Transactions are added to mempool
        3. Blocks containing transactions can be created and mined
        4. Orphan blocks with transactions are properly queued
        5. When parent arrives, both blocks are processed and transactions removed from mempool
        6. Transactions are persisted in the blockchain
        """
        self.log_info("test_4_block_with_tx: Testing orphan blocks with transactions...")

        # Step 1: Create properly signed parent transaction using TransactionHelper
        parent_tx_details, parent_tx_serialized = self.tx_helper.create_transaction(
            data="parent_transaction_data",
            fee=1000
        )
        self.log_info(f"Created properly signed parent transaction from {self.tx_helper.account.address}")

        # Step 2: Create properly signed child transaction using TransactionHelper
        child_tx_details, child_tx_serialized = self.tx_helper.create_transaction(
            data="child_transaction_data",
            fee=500
        )
        self.log_info(f"Created properly signed child transaction from {self.tx_helper.account.address}")

        # Convert to dictionary format for BlockUtils (for block inclusion)
        parent_tx = {
            'version': parent_tx_details['version'],
            'nonce': parent_tx_details['nonce'],
            'from': self.tx_helper.account.address_bytes,
            'type': parent_tx_details['type'],
            'data': parent_tx_details['data'],
            'fee': parent_tx_details['fee'],
            'signature': bytes.fromhex(parent_tx_details['signature']),
            'metadata': ''
        }

        child_tx = {
            'version': child_tx_details['version'],
            'nonce': child_tx_details['nonce'],
            'from': self.tx_helper.account.address_bytes,
            'type': child_tx_details['type'],
            'data': child_tx_details['data'],
            'fee': child_tx_details['fee'],
            'signature': bytes.fromhex(child_tx_details['signature']),
            'metadata': ''
        }

        # Step 3: Use already serialized transactions for P2P TX messages
        # parent_tx_serialized and child_tx_serialized are already in binary format from TransactionHelper

        with P2PConnection("127.0.0.1", self.node.p2p_port, timeout=10) as conn:
            self.log_info("Connected to P2P port for sending transactions")

            # Send parent TX
            parent_tx_msg = P2PMessage(MessageType.TX, parent_tx_serialized)
            conn.send_message(parent_tx_msg)
            self.log_info("Sent parent transaction via P2P")

            # Send child TX
            child_tx_msg = P2PMessage(MessageType.TX, child_tx_serialized)
            conn.send_message(child_tx_msg)
            self.log_info("Sent child transaction via P2P")

            # Wait for processing
            time.sleep(1)

        # Step 4: Verify mempool size = 2
        chain_info = self.node.get_chain_info()
        mempool_size = chain_info.get("mempool_size", 0)
        self.log_info(f"Mempool size after sending transactions: {mempool_size}")
        self.assert_equal(mempool_size, 2, "Should have 2 transactions in mempool")

        # Step 5: Create parent block with parent transaction
        parent_block_height = OrphanBlocksTest.next_block_height
        parent_block = BlockUtils.create_block(
            prev_hash_hex=OrphanBlocksTest.latest_block['hash_hex'],
            height=parent_block_height,
            miner="test_4_parent_miner",
            transactions=[parent_tx],  # Include parent transaction
            mine=True
        )
        OrphanBlocksTest.next_block_height += 1
        self.log_info(f"Created parent block (height {parent_block_height}) with parent tx")

        # Step 6: Create child block with child transaction
        child_block_height = OrphanBlocksTest.next_block_height
        child_block = BlockUtils.create_block(
            prev_hash_hex=parent_block['hash_hex'],
            height=child_block_height,
            miner="test_4_child_miner",
            transactions=[child_tx],  # Include child transaction
            mine=True
        )
        OrphanBlocksTest.next_block_height += 1
        self.log_info(f"Created child block (height {child_block_height}) with child tx")

        # Step 7: Serialize blocks
        child_block_serialized = BlockUtils.serialize_block(child_block)
        parent_block_serialized = BlockUtils.serialize_block(parent_block)

        # Step 8: Send child block first (should become orphan)
        with P2PConnection("127.0.0.1", self.node.p2p_port, timeout=10) as conn:
            self.log_info("Sending CHILD block first (should become orphan)")
            child_block_msg = P2PMessage(MessageType.BLOCK, child_block_serialized)
            conn.send_message(child_block_msg)
            time.sleep(1)

            # Verify child block is orphaned
            chain_info = self.node.get_chain_info()
            orphan_size = chain_info.get("orphan_blocks_size", 0)
            self.log_info(f"Orphan blocks size after child: {orphan_size}")
            self.assert_equal(orphan_size, 1, "Child block should be in orphan pool")

            # Step 9: Send parent block (should process both blocks)
            self.log_info("Sending PARENT block (should process both blocks)")
            parent_block_msg = P2PMessage(MessageType.BLOCK, parent_block_serialized)
            conn.send_message(parent_block_msg)
            time.sleep(2)  # Allow recursive orphan processing

        # Step 10: Final verification
        chain_info = self.node.get_chain_info()
        orphan_size = chain_info.get("orphan_blocks_size", 0)
        mempool_size = chain_info.get("mempool_size", 0)
        blocks_count = chain_info.get("blocks", 0)

        self.log_info(f"Final state: orphan_size={orphan_size}, mempool_size={mempool_size}, blocks={blocks_count}")

        # Verify orphan pool is empty
        self.assert_equal(orphan_size, 0, "Orphan pool should be empty after processing")

        # Verify mempool is empty (all transactions consumed by blocks)
        self.assert_equal(mempool_size, 0, "Mempool should be empty after blocks processed")

        # Verify both blocks added to blockchain
        # Expected: 1 genesis + test_2 (2 blocks) + test_3 (4 blocks) + test_4 (2 blocks) = 9 blocks
        expected_blocks = 9
        self.assert_equal(blocks_count, expected_blocks, f"Should have {expected_blocks} blocks on disk")

        self.log_info("test_4_block_with_tx completed successfully!")

        # Step 11: Update class state
        OrphanBlocksTest.latest_block = child_block
        self.log_info(f"Updated latest_block to child block (height {child_block['height']})")

    def test_5_max_orphanpool_size(self):
        """
        Test orphan pool size limit and eviction.

        Steps:
        1. Create 201 orphan blocks (all with same non-existent parent)
        2. Send first 200 blocks → orphan pool should reach MAX_ORPHAN_BLOCKS (200)
        3. Send 201st block → should trigger eviction
        4. Verify orphan pool size remains at MAX_ORPHAN_BLOCKS (200)

        Note: Eviction is random (not FIFO) due to unordered_map implementation.
        """
        self.log_info("test_5_max_orphanpool_size: Testing orphan pool size limit...")

        MAX_ORPHAN_BLOCKS = 200  # Matches src/utils/settings.h

        # Create a non-existent parent hash
        fake_parent_hash = "0" * 64  # 64 hex zeros (non-existent block)
        self.log_info(f"Using fake parent hash: {fake_parent_hash}...")

        # Create 201 orphan blocks using global block height counter (class-level attribute)
        self.log_info("Creating 201 orphan blocks...")
        orphan_blocks = []
        for i in range(201):
            block_height = OrphanBlocksTest.next_block_height
            block = BlockUtils.create_block(
                prev_hash_hex=fake_parent_hash,
                height=block_height,
                miner=f"test_5_test_miner_{i}",
                timestamp=1762553229520435 + i * 1000000,  # Unique timestamps
                mine=True
            )
            orphan_blocks.append(block)
            OrphanBlocksTest.next_block_height += 1

        self.log_info(f"Created {len(orphan_blocks)} orphan blocks")

        # Serialize all blocks
        self.log_info("Serializing blocks...")
        serialized_blocks = []
        for block in orphan_blocks:
            serialized = BlockUtils.serialize_block(block)
            serialized_blocks.append(serialized)

        # Track first and last block hashes
        first_block_hash = orphan_blocks[0]['hash_hex']
        last_block_hash = orphan_blocks[200]['hash_hex']  # 201st block (index 200)

        self.log_info(f"First block hash: {first_block_hash}...")
        self.log_info(f"Last block hash: {last_block_hash}...")

        # Send blocks and monitor orphan pool size
        self.log_info(f"Connecting to node P2P port {self.node.p2p_port}...")

        with P2PConnection("127.0.0.1", self.node.p2p_port, timeout=10) as conn:
            self.log_info("Connected successfully (handshake completed)")

            # Send first 200 blocks
            self.log_info("Sending first 200 blocks...")
            for i in range(200):
                msg = P2PMessage(MessageType.BLOCK, serialized_blocks[i])
                conn.send_message(msg)

                # Check size periodically (every 50 blocks to reduce API calls)
                if (i + 1) % 50 == 0:
                    time.sleep(0.5)
                    chain_info = self.node.get_chain_info()
                    orphan_size = chain_info.get("orphan_blocks_size", 0)
                    self.log_info(f"After {i + 1} blocks: orphan_size = {orphan_size}")

            # Allow processing to complete
            time.sleep(2)

            # Verify pool is at MAX_ORPHAN_BLOCKS
            chain_info = self.node.get_chain_info()
            orphan_size = chain_info.get("orphan_blocks_size", 0)
            self.log_info(f"Orphan pool size after 200 blocks: {orphan_size}")
            self.assert_equal(orphan_size, MAX_ORPHAN_BLOCKS,
                            f"Should have {MAX_ORPHAN_BLOCKS} orphans after 200 blocks")

            # Send 201st block (should trigger eviction)
            self.log_info("Sending 201st block (should trigger eviction)...")
            msg = P2PMessage(MessageType.BLOCK, serialized_blocks[200])
            conn.send_message(msg)
            time.sleep(2)

            # Verify pool size still at MAX_ORPHAN_BLOCKS
            chain_info = self.node.get_chain_info()
            orphan_size_after = chain_info.get("orphan_blocks_size", 0)
            self.log_info(f"Orphan pool size after 201st block: {orphan_size_after}")
            self.assert_equal(orphan_size_after, MAX_ORPHAN_BLOCKS,
                            f"Should still have {MAX_ORPHAN_BLOCKS} orphans after eviction")

            # We can only verify size constraint is enforced
            # Cannot verify specific block eviction due to random eviction policy
            self.log_info("Eviction test passed: orphan pool size constraint enforced")
            self.log_info("Note: Eviction is random (unordered_map), cannot predict which block evicted")

            self.log_info("test_5_max_orphanpool_size completed successfully!")

if __name__ == "__main__":
    unittest.main()
