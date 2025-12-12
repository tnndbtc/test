#!/usr/bin/env python3
"""
P2P Block Relay Functional Test for Blockweave.

Tests block propagation through a 3-node relay network:
- Node0 mines a block and broadcasts INVENTORY
- Node1 receives INVENTORY, requests with GETDATA, receives BLOCKS
- Node1 relays INVENTORY to Node2
- Node2 requests with GETDATA and receives BLOCKS
- All nodes verify they have received the block
"""

import sys
import time
import unittest
from test_framework import TestFramework


class P2PRelayTest(TestFramework):
    """Test P2P block relay through multiple nodes."""

    # Set num_nodes as class attribute for setUpClass
    num_nodes = 3

    def setup(self):
        """
        Setup test environment - configure 3-node relay topology.

        Topology:
        Node0 <-> Node1 <-> Node2

        Nodes:
        - Node 0: REST API port 48443, P2P port 48333
        - Node 1: REST API port 48444, P2P port 48334
        - Node 2: REST API port 48445, P2P port 48335
        """
        self.log_info("setup: Establishing relay topology...")

        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node2 = self.nodes[2]

        # Connect node0 <-> node1
        self.log_info(f"setup: Node{node0.node_index} connecting to Node{node1.node_index}...")
        if node0.connect_to_peer(node1, wait=True):
            self.log_info(f"setup: Node{node0.node_index} <-> Node{node1.node_index} connected")
        else:
            self.log_error(f"setup: FAILED to connect Node{node0.node_index} to Node{node1.node_index}")

        # Connect node1 <-> node2
        self.log_info(f"setup: Node{node1.node_index} connecting to Node{node2.node_index}...")
        if node1.connect_to_peer(node2, wait=True):
            self.log_info(f"setup: Node{node1.node_index} <-> Node{node2.node_index} connected")
        else:
            self.log_error(f"setup: FAILED to connect Node{node1.node_index} to Node{node2.node_index}")

        # Wait for connections to stabilize
        time.sleep(2)

        # Debug: Log peer counts after setup
        for i, node in enumerate(self.nodes):
            peer_info = node.get_peer_info()
            total = peer_info.get('total_peers', 0)
            outbound = peer_info.get('outbound_peers', 0)
            inbound = peer_info.get('inbound_peers', 0)
            self.log_info(f"setup: Node{i} peer counts: total={total}, outbound={outbound}, inbound={inbound}")

        self.log_info("setup: Relay topology established")

    def test_1_mine_block_broadcast(self):
        """
        Test block propagation through relay network.

        Steps:
        1. Send transaction to node0
        2. Node0 mines a block
        3. Node0 broadcasts INVENTORY to node1
        4. Node1 requests block with GETDATA
        5. Node0 sends BLOCKS to node1
        6. Node1 broadcasts INVENTORY to node2
        7. Node2 requests block with GETDATA
        8. Node1 sends BLOCKS to node2
        9. Verify all nodes have blocks: 2 via /chain endpoint
        """
        self.log_info("test_1_mine_block_broadcast: Starting block relay test...")

        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node2 = self.nodes[2]

        # Verify initial block counts (should have 1 genesis block)
        self.log_info("Step 0: Verifying initial state...")
        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', -1)
            self.log_info(f"Node{i} initial blocks: {blocks}")
            self.assert_equal(blocks, 1, f"Node{i} should start with 1 genesis block")

        # Step 1: Send transaction to node0
        self.log_info("Step 1: Sending transaction to node0...")
        tx_data = {
            "from": "wallet_node0",
            "to": "wallet_receiver",
            "data": "test_relay_data"
        }

        try:
            success, _ = node0.create_transaction(tx_data)
            self.assert_equal(success, True, "Transaction submission should succeed")
            self.log_info(f"Transaction submitted successfully")
        except Exception as e:
            self.assert_true(False, f"Transaction submission failed: {e}")

        # Wait for transaction to be added to mempool and propagate to node1 and node2
        time.sleep(0.5)
        node1_chain_info = node1.get_chain_info()
        node1_mempool = node1_chain_info.get('mempool_size', -1)
        self.assert_equal(node1_mempool, 1, f"Transaction propagated to node1")
        node2_chain_info = node2.get_chain_info()
        node2_mempool = node2_chain_info.get('mempool_size', -1)
        self.assert_equal(node2_mempool, 1, f"Transaction propagated to node2")

        # Step 2: Trigger Node0 to mine a block
        self.assert_true(node0.trigger_mining(), "Block mining triggered successfully")

        # Note: Mining happens automatically when there are transactions in the mempool
        self.log_info("Step 2: Trigger Node0 to mine a block")

        # Next step: Wait for block relay through the network
        # The P2P protocol should automatically:
        # - Node0 broadcasts INVENTORY to node1
        # - Node1 sends GETDATA to node0
        # - Node0 responds with BLOCKS to node1
        # - Node1 broadcasts INVENTORY to node2
        # - Node2 sends GETDATA to node1
        # - Node1 responds with BLOCKS to node2

        self.log_info("Steps 3-8: Waiting for automatic block relay through P2P network...")
        self.log_info("  (Node0 -> Node1 -> Node2 via INVENTORY/GETDATA/BLOCKS)")

        # Wait for blocks to propagate
        time.sleep(1)

        # Step 9: Verify all nodes have blocks: 2 (genesis + mined block)
        self.log_info("Step 3: Verifying final block counts...")

        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', -1)

            self.log_info(f"Node{i} final blocks: {blocks}")
            self.assert_equal(blocks, 2, f"Node{i} should have 2 blocks after relay (genesis + mined block)")

        self.log_info("test_1_mine_block_broadcast: Block relay test completed successfully!")

    def test_2_block_transaction_broadcast(self):
        """
        Test block and transaction broadcast through relay network.

        Steps:
        1. Send one transaction to node0
        2. Node0 mines it into a block
        3. Node0 stops mining
        4. Send another transaction to node0 (stays in mempool)
        5. Node0 broadcasts INVENTORY for the new block and transaction to node1
        6. Node1 responds with GETDATA
        7. Node0 responds with BLOCK and TX
        8. Node1 broadcasts INVENTORY to node2
        9. Node2 responds with GETDATA
        10. Node1 responds with BLOCK
        11. Verify all nodes have blocks: 2 and mempool size: 1
        """
        self.log_info("test_2_block_transaction_broadcast: Starting block+tx relay test...")

        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node2 = self.nodes[2]

        # Step 1: Send first transaction to node0
        self.log_info("Step 1: Sending first transaction to node0...")
        tx1_data = {
            "from": "wallet_node0_tx1",
            "to": "wallet_receiver_tx1",
            "data": "test_block_tx_data_1"
        }

        try:
            success, _ = node0.create_transaction(tx1_data)
            self.assert_equal(success, True, "First transaction submission should succeed")
            # self.log_info(f"First transaction submitted: {tx1_result}")
        except Exception as e:
            self.assert_true(False, f"First transaction submission failed: {e}")
        time.sleep(1)

        # Step 2: Trigger Node0 to mine a block
        self.assert_true(node0.trigger_mining(), "Block mining triggered successfully")
        # Step 2: Wait for Node0 to mine the first transaction
        self.log_info("Step 2: Trigger for Node0 to mine first block...")

        # Step 3: Send second transaction to node0 (will stay in mempool)
        self.log_info("Step 3: Sending second transaction to node0 (will stay in mempool)...")
        tx2_data = {
            "from": "wallet_node0_tx2",
            "to": "wallet_receiver_tx2",
            "data": "test_block_tx_data_2"
        }

        success, _ = node0.create_transaction(tx2_data)
        self.assert_true(success, "Second transaction submission should succeed")
        self.log_info("Second transaction submitted successfully")

        # Verify node0 has transaction in mempool
        chain_info = node0.get_chain_info()
        mempool_size = chain_info.get('mempool_size', -1)
        self.log_info(f"Node0 mempool size: {mempool_size}")
        self.assert_equal(mempool_size, 1, "Node0 should have 1 transaction in mempool")

        # Steps 5-10: Wait for block and transaction relay through the network
        # The P2P protocol should automatically:
        # - Node0 broadcasts INVENTORY (block + tx) to node1
        # - Node1 sends GETDATA to node0
        # - Node0 responds with BLOCK and TX to node1
        # - Node1 broadcasts INVENTORY to node2
        # - Node2 sends GETDATA to node1
        # - Node1 responds with BLOCK to node2

        self.log_info("Next steps: Waiting for automatic block+tx relay through P2P network...")
        self.log_info("  (Node0 -> Node1 -> Node2 via INVENTORY/GETDATA/BLOCK/TX)")

        # Wait for blocks and transactions to propagate
        # Check every 1 seconds for up to 2 seconds
        max_relay_wait = 2
        relay_start = time.time()

        while time.time() - relay_start < max_relay_wait:
            # Check if all nodes have the block
            node2_chain = node2.get_chain_info()

            mempool_size = node2_chain.get('mempool_size', -1)

            self.log_info(f"Relay progress: Node2 mempool={mempool_size}")

            if mempool_size > 0:
                self.log_info("Transactions successfully relayed to all nodes!")
                break

            time.sleep(1)

        # have to sleep here in Linux, because the calling of get_chain_info() may arrive at out of order, so the call below may return 0 txs on node2 in mempool
        time.sleep(1)

        for i, node in enumerate(self.nodes, start=1):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', -1)
            mempool = chain_info.get('mempool_size', -1)

            self.log_info(f"Node{i} blocks: {blocks}, mempool: {mempool}")
            self.assert_equal(blocks, 3, f"Node{i} should have 3 blocks after relay (genesis block)")
            self.assert_equal(mempool, 1, f"Node{i} should have 1 transaction in mempool after relay")

        # # Next: trigger mining on node3 Verify all nodes have blocks: 4 and mempool_size: 0
        # self.log_info("Step 11: Verifying final block counts and mempool sizes...")
        #
        self.log_info(f"trigger mining on node2")
        self.assert_true(node2.trigger_mining(), "Block mining triggered successfully on node2")

        # Wait for blocks and transactions to propagate
        # Check every 1 seconds for up to 2 seconds
        max_relay_wait = 2
        relay_start = time.time()

        while time.time() - relay_start < max_relay_wait:
            # Check if all nodes have the block, since node0 is the last one receive the broadcast, check node0 should be good
            chain = node0.get_chain_info()

            mempool_size = chain.get('mempool_size', -1)

            self.log_info(f"Relay progress: Node0 mempool={mempool_size}")

            if mempool_size == 0:
                self.log_info("transactions in mempool should be deleted after node2 mined it and relayed to all nodes!")
                break

            time.sleep(1)

        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', -1)
            mempool = chain_info.get('mempool_size', -1)

            self.log_info(f"Node{i} blocks: {blocks}, mempool: {mempool}")
            self.assert_equal(blocks, 4, f"Node{i} should have 4 blocks after relay (genesis block)")
            self.assert_equal(mempool, 0, f"Node{i} should have 0 transaction in mempool after relay")
        self.log_info("test_2_block_transaction_broadcast: Block+tx relay test completed successfully!")


if __name__ == "__main__":
    unittest.main()
