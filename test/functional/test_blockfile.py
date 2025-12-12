#!/usr/bin/env python3
"""
Blockfile functional test for Blockweave.

Tests the persistent block storage functionality including:
- Block files are created after mining
- Blocks can be loaded after daemon restart
- Multiple blocks are persisted correctly
- Data directory structure is created properly
"""

import sys
import time
import os
import unittest
from pathlib import Path
from test_framework import TestFramework


class BlockfileTest(TestFramework):
    """Test blockfile persistence functionality."""

    # Note: With setUpClass, these tests share blockchain state
    # The mining tests may not always show file size increases if
    # blocks were already mined in previous tests
    # This is acceptable as the restart test (which runs last) still validates persistence

    num_nodes = 1

    def setup(self):
        """Setup test environment - start a local node."""
        # Node is automatically started by framework via num_nodes = 1
        # Access it via self.nodes[0]
        self.node = self.nodes[0] if self.nodes else None
        if not self.node:
            raise RuntimeError("Failed to start blockweave node")

        # Get the data directory from the node's config (datadir is a string)
        self.data_dir = Path(self.node.datadir) / "data"
        self.blocks_dir = self.data_dir / "localnet/blocks"
        self.log_info(f"Data directory: {self.data_dir}")
        self.log_info(f"Blocks directory: {self.blocks_dir}")

        # Wait a bit for directories to be created by the daemon
        max_wait = 5
        start_time = time.time()
        while time.time() - start_time < max_wait:
            if self.blocks_dir.exists():
                self.log_info("Block directory created by daemon")
                break
            time.sleep(0.1)

        # Give a bit more time for genesis block to be saved
        time.sleep(1)

    def test_1_data_directory_creation(self):
        """Test that data directories are created on startup."""
        self.log_info("test_1_data_directory_creation: Testing data directory creation...")

        # Verify data directory exists
        self.assert_true(self.data_dir.exists(), f"Data directory should exist: {self.data_dir}")
        self.assert_true(self.data_dir.is_dir(), f"Data path should be a directory: {self.data_dir}")

        # Verify blocks subdirectory exists
        self.assert_true(self.blocks_dir.exists(), f"Blocks directory should exist: {self.blocks_dir}")
        self.assert_true(self.blocks_dir.is_dir(), f"Blocks path should be a directory: {self.blocks_dir}")

        # Verify block index file is created
        index_file = self.blocks_dir / "block_index.dat"
        self.assert_true(index_file.exists(), f"Block index file should exist: {index_file}")

        self.log_info("Data directories created successfully")

    def test_2_genesis_block_saved(self):
        """Test that genesis block is saved to disk on startup."""
        self.log_info("test_2_genesis_block_saved: Testing genesis block persistence...")

        # List all blk*.dat files in blocks directory (excluding index)
        block_files = list(self.blocks_dir.glob("blk*.dat"))
        self.log_info(f"Found {len(block_files)} block file(s) in {self.blocks_dir}")

        # Should have at least one block file (blk00000.dat) with genesis block
        self.assert_true(len(block_files) >= 1, "At least one block file (blk00000.dat) should exist")

        # Verify blk00000.dat exists
        blk00000 = self.blocks_dir / "blk00000.dat"
        self.assert_true(blk00000.exists(), "blk00000.dat should exist")

        # Log the block files
        for block_file in sorted(block_files):
            file_size = block_file.stat().st_size
            self.log_info(f"Block file: {block_file.name} (size: {file_size} bytes)")
            self.assert_true(file_size > 0, f"Block file {block_file.name} should have non-zero size")

        self.log_info("Genesis block saved successfully")

    def test_3_block_persistence_after_mining(self):
        """Test that blocks are saved to disk after mining."""
        self.log_info("test_3_block_persistence_after_mining: Testing block persistence after mining...")

        # Get initial blockchain state
        initial_state = self.node.get_chain_info()
        self.assert_equal(initial_state.get("status"), "success", "get_chain_info() should succeed")
        initial_height = initial_state.get("blocks", 0)
        initial_mempool_size = initial_state.get("mempool_size", 0)
        self.log_info(f"Initial state - height: {initial_height}, mempool: {initial_mempool_size}")

        # Check initial blk00000.dat size
        blk00000 = self.blocks_dir / "blk00000.dat"
        initial_size = blk00000.stat().st_size if blk00000.exists() else 0
        self.log_info(f"Initial blk00000.dat size: {initial_size} bytes")

        # Create a transaction
        transaction_data = {
            "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
            "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
            "data": "Test transaction for blockfile persistence",
            "fee": 1000
        }

        self.log_info("Submitting transaction...")
        try:
            success, _ = self.node.create_transaction(transaction_data)
            self.assert_equal(success, True, "Transaction submission successful")
        except Exception as e:
            self.assert_true(False, f"Transaction submission failed: {e}")

        # Verify transaction is in mempool
        time.sleep(0.5)
        state_after_tx = self.node.get_chain_info()
        mempool_after_tx = state_after_tx.get("mempool_size", 0)
        self.log_info(f"Mempool size after transaction: {mempool_after_tx}")
        self.assert_true(mempool_after_tx > initial_mempool_size,
                        f"Transaction should be in mempool (before: {initial_mempool_size}, after: {mempool_after_tx})")

        # Trigger mining
        self.log_info("Triggering block mining...")
        self.assert_true(self.node.trigger_mining(), "Block mining triggered successfully")

        # Wait a moment for block to be written to disk
        time.sleep(0.5)

        # Verify blockchain height increased
        final_state = self.node.get_chain_info()
        final_height = final_state.get("blocks", 0)
        final_mempool_size = final_state.get("mempool_size", 0)
        self.log_info(f"Final state - height: {final_height}, mempool: {final_mempool_size}")

        # Block should have been mined (height increased, mempool cleared)
        self.assert_equal(final_height, initial_height + 1,
                         f"Blockchain height should increase by 1 (initial: {initial_height}, final: {final_height})")
        self.assert_equal(final_mempool_size, 0, "Mempool should be empty after mining")

        # Verify block file exists and has content
        final_size = blk00000.stat().st_size if blk00000.exists() else 0
        self.log_info(f"Final blk00000.dat size: {final_size} bytes (initial: {initial_size})")
        self.assert_true(final_size > initial_size,
                        f"Block file should increase size (initial: {initial_size}, final: {final_size} bytes)")

        # Log all block files
        block_files = sorted(self.blocks_dir.glob("blk*.dat"))
        for block_file in block_files:
            file_size = block_file.stat().st_size
            self.log_info(f"Block file: {block_file.name} (size: {file_size} bytes)")

        self.log_info("Block persistence after mining verified")

    def test_4_multiple_blocks_persistence(self):
        """Test that multiple blocks are saved correctly (appended to same file)."""
        self.log_info("test_4_multiple_blocks_persistence: Testing multiple blocks persistence...")

        # Check initial blk00000.dat size
        blk00000 = self.blocks_dir / "blk00000.dat"
        initial_size = blk00000.stat().st_size if blk00000.exists() else 0
        self.log_info(f"Initial blk00000.dat size: {initial_size} bytes")

        # Submit multiple transactions
        num_transactions = 3
        for i in range(num_transactions):
            transaction_data = {
                "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
                "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
                "data": f"Test transaction {i+1} for multiple blocks persistence",
                "fee": 1000
            }
            self.log_info(f"Submitting transaction {i+1}/{num_transactions}...")
            try:
                success, _ = self.node.create_transaction(transaction_data)
                self.assert_equal(success, True, f"Transaction {i+1} submission successful")
            except Exception as e:
                self.assert_true(False, f"Transaction {i+1} submission failed: {e}")
            # Small delay between transactions to ensure they're processed separately
            time.sleep(0.2)

        self.assert_true(self.node.trigger_mining(), f"Block mining triggered successfully")

        # Verify file size increased significantly
        final_size = blk00000.stat().st_size
        self.log_info(f"Final blk00000.dat size: {final_size} bytes")

        # Note: With shared node state in setUpClass, file size may not always increase
        # Check that file exists and has reasonable content.
        self.assert_true(final_size > initial_size,
                        f"Block file should increase the size: {final_size} bytes, compare to initial {initial_size} bytes)")

        # Log all block files
        # block_files = sorted(self.blocks_dir.glob("blk*.dat"))
        # for block_file in block_files:
        #     file_size = block_file.stat().st_size
        #     self.assert_true(file_size > 0, f"Block file {block_file.name} should have non-zero size")
        #     self.log_info(f"Block file: {block_file.name} (size: {file_size} bytes)")

        self.log_info("Multiple blocks persistence verified")

    def test_5_block_loading_after_restart(self):
        """Test that blocks can be loaded after daemon restart (from blk*.dat files and index)."""
        self.log_info("test_5_block_loading_after_restart: Testing block loading after restart...")

        # Mine a block first
        self.log_info("Mining a block before restart...")

        transaction_data = {
            "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
            "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
            "data": "Test transaction before restart",
            "fee": 1000
        }

        try:
            success, _ = self.node.create_transaction(transaction_data)
            self.assert_equal(success, True, "Transaction submission successful")
        except Exception as e:
            self.assert_true(False, f"Transaction submission failed: {e}")

        self.assert_true(self.node.trigger_mining(), f"Block mining triggered successfully")

        # Record state before restart
        blk_files_before = sorted(self.blocks_dir.glob("blk*.dat"))
        sizes_before = {f.name: f.stat().st_size for f in blk_files_before}
        self.log_info(f"Block files before restart: {list(sizes_before.keys())}")

        # Check index file exists
        index_file = self.blocks_dir / "block_index.dat"
        index_exists_before = index_file.exists()
        index_size_before = index_file.stat().st_size if index_exists_before else 0
        self.log_info(f"Block index before restart: exists={index_exists_before}, size={index_size_before}")

        # Get current blocks
        before_shutdown_state = self.node.get_chain_info()
        before_shutdown_blocks = before_shutdown_state.get("blocks", 0)
        before_shutdown_mempool = before_shutdown_state.get("mempool_size", 0)
        self.log_info(f"Before shutdown blocks: {before_shutdown_blocks}, mempool: {before_shutdown_mempool}")

        # Stop the node
        self.log_info("Stopping node...")
        self.node.stop()
        time.sleep(3)

        # Restart the node
        self.log_info("Restarting node...")
        if not self.node.start(timeout=15):
            raise RuntimeError("Failed to restart blockweave node")

        self.log_info("Node restarted successfully")

        # Wait for directories to be ready again
        time.sleep(2)

        # Get current blocks
        after_restart_state = self.node.get_chain_info()
        after_restart_blocks = after_restart_state.get("blocks", 0)
        after_restart_mempool = after_restart_state.get("mempool_size", 0)
        self.log_info(f"After restart blocks: {after_restart_blocks}, mempool: {after_restart_mempool}")
        self.assert_equal(before_shutdown_blocks, after_restart_blocks, f"blocks should be equal after restart")
        self.assert_equal(before_shutdown_mempool, after_restart_mempool, f"mempool should be equal after restart")

        # Verify block files still exist after restart with same sizes
        blk_files_after = sorted(self.blocks_dir.glob("blk*.dat"))
        sizes_after = {f.name: f.stat().st_size for f in blk_files_after}
        self.log_info(f"Block files after restart: {list(sizes_after.keys())}")

        # Block files should be identical
        self.assert_equal(sorted(sizes_before.keys()), sorted(sizes_after.keys()),
                         "Same block files should exist after restart")

        for filename in sizes_before:
            self.assert_equal(sizes_before[filename], sizes_after[filename],
                            f"File {filename} should have same size after restart")

        # Verify index file still exists
        index_exists_after = index_file.exists()
        self.assert_true(index_exists_after, "Block index should exist after restart")

        # Verify node is still functional
        chain_state = self.node.get_chain_info()
        self.assert_equal(chain_state.get("status"), "success", "get_chain_info() returns success after restart")
        self.log_info(f"Chain state after restart: {chain_state}")

        self.log_info("Block loading after restart verified")

    def cleanup(self):
        """Cleanup test environment."""
        if self.node:
            self.node.stop()


if __name__ == "__main__":
    unittest.main()
