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
        - Node 0: REST API port 28443, P2P port 28333
        - Node 1: REST API port 28444, P2P port 28334
        - Node 2: REST API port 28445, P2P port 28335
        """
        self.log_info("setup: Establishing relay topology...")

        node0 = self.test_nodes[0]
        node1 = self.test_nodes[1]
        node2 = self.test_nodes[2]

        # Connect node0 <-> node1
        self.log_info(f"setup: Node{node0.index} connecting to Node{node1.index}...")
        if node0.connect_to_peer(node1, wait=True):
            self.log_info(f"setup: Node{node0.index} <-> Node{node1.index} connected")
        else:
            self.log_error(f"setup: FAILED to connect Node{node0.index} to Node{node1.index}")

        # Connect node1 <-> node2
        self.log_info(f"setup: Node{node1.index} connecting to Node{node2.index}...")
        if node1.connect_to_peer(node2, wait=True):
            self.log_info(f"setup: Node{node1.index} <-> Node{node2.index} connected")
        else:
            self.log_error(f"setup: FAILED to connect Node{node1.index} to Node{node2.index}")

        # Wait for connections to stabilize
        time.sleep(2)

        # Debug: Log peer counts after setup
        for i, node in enumerate(self.test_nodes):
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

        node0 = self.test_nodes[0]
        node1 = self.test_nodes[1]
        node2 = self.test_nodes[2]

        # Verify initial block counts (should have 1 genesis block)
        self.log_info("Step 0: Verifying initial state...")
        for i, node in enumerate(self.test_nodes):
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

        response = node0.post("/transaction", json_data=tx_data)
        self.assert_equal(response.status_code, 200, "Transaction submission should succeed")

        tx_result = response.json()
        self.log_info(f"Transaction submitted: {tx_result}")

        # Wait for transaction to be added to mempool
        time.sleep(1)

        # Step 2: Wait for Node0 to mine a block
        # Note: Mining happens automatically when there are transactions in the mempool
        self.log_info("Step 2: Waiting for Node0 to mine block...")

        # Wait for block to be mined (poll /chain for blocks count)
        # Genesis block is at height 0, so we wait for blocks >= 2 (genesis + mined block)
        max_wait = 60  # Maximum 60 seconds
        start_time = time.time()
        block_mined = False

        while time.time() - start_time < max_wait:
            chain_info = node0.get_chain_info()
            blocks = chain_info.get('blocks', 0)

            if blocks >= 2:
                self.log_info(f"Node0 mined block! blocks={blocks}")
                block_mined = True
                break

            time.sleep(1)

        self.assert_true(block_mined, "Node0 should have mined a block within 60 seconds")

        # Step 3-8: Wait for block relay through the network
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
        # Check every 2 seconds for up to 20 seconds
        max_relay_wait = 20
        relay_start = time.time()

        while time.time() - relay_start < max_relay_wait:
            # Check if all nodes have the block
            node1_chain = node1.get_chain_info()
            node2_chain = node2.get_chain_info()

            node1_blocks = node1_chain.get('blocks', 0)
            node2_blocks = node2_chain.get('blocks', 0)

            self.log_info(f"Relay progress: Node1 blocks={node1_blocks}, Node2 blocks={node2_blocks}")

            if node1_blocks >= 2 and node2_blocks >= 2:
                self.log_info("Block successfully relayed to all nodes!")
                break

            time.sleep(2)

        # Step 9: Verify all nodes have blocks: 2 (genesis + mined block)
        self.log_info("Step 9: Verifying final block counts...")

        for i, node in enumerate(self.test_nodes):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', -1)

            self.log_info(f"Node{i} final blocks: {blocks}")
            self.assert_equal(blocks, 2, f"Node{i} should have 2 blocks after relay (genesis + mined block)")

        self.log_info("test_1_mine_block_broadcast: Block relay test completed successfully!")


if __name__ == "__main__":
    unittest.main()
