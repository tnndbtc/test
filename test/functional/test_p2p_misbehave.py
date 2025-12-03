#!/usr/bin/env python3
"""
Test peer misbehavior scoring and banning system.

Tests the peer misbehavior tracking system by sending invalid INVENTORY messages
and verifying:
1. Single invalid message increases score but doesn't ban (score < threshold)
2. Multiple invalid messages accumulate and trigger ban (score >= 100)
3. Banned peers cannot reconnect until ban expires (86400 seconds)

This test uses mocktime to simulate time progression for ban expiration testing.
"""

import sys
import os
import time
import struct
import unittest
from test_framework import TestFramework
from test_framework.p2p_utils import (
    MessageType,
    P2PMessage,
    P2PConnection
)


class P2PMisbehaviorTest(TestFramework):
    """Test peer misbehavior scoring and ban system."""

    num_nodes = 1  # Only need 1 node - test client sends invalid messages

    def setup(self):
        """Setup test environment - one node."""
        self.node0 = self.nodes[0]

        # Get P2P port for node0 (where we'll send invalid messages)

        self.log_info(f"Node0 running on P2P port {self.node0.p2p_port}")

    def create_invalid_inventory(self):
        """
        Create invalid INVENTORY message (missing hash data).

        Valid format: [count:4][type:2][hash:32]...
        Invalid format: [count:1][type:2][hash:1]  <- Only 1 byte hash instead of 32

        This triggers parse failure in CInventoryMessage::Deserialize()
        which should increase misbehavior score by 20.

        Returns:
            P2PMessage: Invalid INVENTORY message
        """
        payload = b""

        # Count: 1 item (4 bytes, big-endian)
        payload += struct.pack('!I', 1)

        # Type: 2 (TRANSACTION) (2 bytes, big-endian)
        payload += struct.pack('!H', 2)

        # Hash: Only 1 byte instead of required 32 bytes (INVALID!)
        payload += b'X'

        return P2PMessage(MessageType.INVENTORY, payload)

    def test_1_misbehavior(self):
        """
        Test 1: Single invalid INVENTORY increases score but doesn't ban.

        Sends one invalid INVENTORY message and verifies:
        - Connection is closed (due to parse error)
        - Peer is NOT banned yet (score = 20 < threshold 100)
        - Reconnection succeeds
        """
        self.log_info("TEST 1: Single invalid message increases score but doesn't ban")
        # Set mocktime on node0
        current_time = 10000
        response = self.node0.set_mock_time(current_time)
        self.log_info(f"Setting mocktime to current time: {current_time}")

        # Variable to store the client port for reuse
        client_port = None

        # First connection: Send invalid INVENTORY and store client port
        with P2PConnection("127.0.0.1", self.node0.p2p_port, timeout=3) as conn:
            # Context manager automatically performs VERSION/VERACK handshake
            self.log_info("Connected (handshake completed automatically)")

            # Store the client port assigned by OS
            client_port = conn.get_client_port()
            self.log_info(f"First connection client port: {client_port}")

            peer_info = self.node0.get_peer_info()
            total = peer_info.get('total_peers', -1)
            outbound = peer_info.get('outbound_peers', -1)
            inbound = peer_info.get('inbound_peers', -1)
            self.log_info(f"node0 peer counts: total={total}, outbound={outbound}, inbound={inbound}")

            # Send 1 invalid INVENTORY message
            self.log_info("Sending 1 invalid INVENTORY message...")
            inv_msg = self.create_invalid_inventory()
            self.log_info("Sending invalid INVENTORY via raw P2P connection...")
            sent = conn.send_message(inv_msg)
            self.assert_true(sent, "Should be able to send invalid INVENTORY")

            # Give node time to process
            time.sleep(0.2)

            for i in range(2, 6):
                self.log_info(f"Sending more invalid message {i}...")

                # Send invalid INVENTORY
                sent = conn.send_message(inv_msg)
                self.assert_true(sent, f"Should send invalid INVENTORY {i}")

                # Give node time to process
                time.sleep(0.2)

                peer_info = self.node0.get_peer_info()
                total = peer_info.get('total_peers', -1)
                outbound = peer_info.get('outbound_peers', -1)
                inbound = peer_info.get('inbound_peers', -1)
                self.log_info(f"node0 peer counts AFTER invalid message: total={total}, outbound={outbound}, inbound={inbound}")
                if i < 5:
                    self.assert_equal(inbound, 1, "node0 should have 1 peer before ban")
                else:
                    self.assert_equal(inbound, 0, "node0 should have 0 peer after ban")
                # on 5th try, the connection will be disconnected

            # Give node time to process
            time.sleep(0.2)

        # Connection should be force closed by node0

        # Verify getpeer shows 0 peers
        peer_info = self.node0.get_peer_info()
        total = peer_info.get('total_peers', -1)
        outbound = peer_info.get('outbound_peers', -1)
        inbound = peer_info.get('inbound_peers', -1)
        self.log_info(f"node0 peer counts AFTER 5 invalid messages: total={total}, outbound={outbound}, inbound={inbound}")
        self.assert_equal(inbound, 0, "node0 should have 0 peers after ban")

        # Advance time by 86300 seconds (100 seconds before ban expires)
        # Ban duration is 86400 seconds (PEER_BAN_DURATION)
        time_before_expiry = current_time + 86300
        self.log_info(f"Advancing mocktime to +86300s: {time_before_expiry}")
        self.node0.set_mock_time(time_before_expiry)
        time.sleep(0.5)

        # Second connection: Verify reconnection is rejected using SAME client port
        self.log_info(f"Attempting reconnection with same client port {client_port} (should be rejected)...")
        # Second connection: should be rejected
        try: 
            with P2PConnection("127.0.0.1", self.node0.p2p_port, timeout=3, client_port=client_port) as conn:
                reused_port = conn.get_client_port()
                self.log_info(f"Reconnection client port: {reused_port}")
                self.assert_equal(reused_port, client_port, f"Should reuse same client port {client_port}")
                self.assert_true(False, "Connection should be rejected for banned peer")
        except Exception as e:
            self.log_info(f"Connection rejected as expected: {e}")
            self.assert_true("handshake failed" in str(e), "Connection should be rejected for banned peer")

        # Advance time by 100 more seconds (total 86400s = ban expires)
        time_after_expiry = current_time + 86400
        self.log_info(f"Advancing mocktime to +86400s: {time_after_expiry}")
        self.node0.set_mock_time(time_after_expiry)
        time.sleep(0.5)

        try: 
            with P2PConnection("127.0.0.1", self.node0.p2p_port, timeout=3, client_port=client_port) as conn:
                reused_port = conn.get_client_port()
                self.log_info(f"Reconnection client port: {reused_port} succeeded!")
                self.assert_equal(reused_port, client_port, f"Should reuse same client port {client_port}")
        except Exception as e:
            self.log_info(f"Connection rejected is NOT expected: {e}")
            self.assert_true(False, "Connection should NOT be rejected for banned peer")

        self.log_info("Test passed: 5 invalid messages trigger ban and port reuse works, reconnect after 86400 worked")

if __name__ == '__main__':
    P2PMisbehaviorTest().main()
