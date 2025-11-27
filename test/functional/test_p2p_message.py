#!/usr/bin/env python3
"""
P2P network functional test for Blockweave.

Tests peer-to-peer networking by starting multiple local nodes
and verifying they can establish connections.

This test suite covers:
- Basic P2P connectivity
- Peer message serialization/deserialization
- SendMessageToPeer functionality
- BroadcastMessage functionality
- Different message types (PING, PONG, GET_PEERS, TX, BLOCK, etc.)
"""

import sys
import os
import time
import unittest
import struct
from test_framework import TestFramework
from test_framework.p2p_utils import MessageType, P2PMessage, P2PConnection


# Removed duplicate MessageType, P2PMessage, and P2PConnection classes
# Now imported from test_framework.p2p_utils


class P2PTest(TestFramework):
    """Test P2P networking with multiple nodes."""

    # Set num_nodes as class attribute so it's available during setUpClass
    num_nodes = 1

    def setup(self):
        """Setup test environment - start a local node."""
        # Node is automatically started by framework via num_nodes = 1
        # Access it via self.nodes[0]
        self.node = self.nodes[0] if self.nodes else None
        if not self.node:
            raise RuntimeError("Failed to start blockweave node")

        # Set log file path - use the shared utility from test_framework
        self.log_file = self.get_node_log_file(self.node)

    def _get_log_position(self):
        """Get current position in log file for tracking new entries (wrapper for compatibility)."""
        return self.get_log_position(self.log_file)

    def _read_new_logs(self, log_position):
        """Read log entries added since the given position (wrapper for compatibility)."""
        return self.read_new_logs(self.log_file, log_position)

    def test_1_send_valid_inventory_message(self):
        """Test sending INVENTORY message to a node."""
        self.log_info("test_1_send_valid_inventory_message: Testing valid INVENTORY message")

        # Connect to Node 0
        p2p_port = 48333
        with P2PConnection("127.0.0.1", p2p_port, timeout=3) as conn:
            if not conn.socket:
                self.assert_true(False, "Failed to connect to P2P port")
                return

            # Create valid INVENTORY payload:
            # Format: [count:4][type:2][hash:32][type:2][hash:32]...
            # - count: uint32_t, network byte order (big-endian)
            # - type: uint16_t, network byte order (1=BLOCK, 2=TRANSACTION)
            # - hash: 32 bytes binary SHA-256 hash

            payload = b""

            # Count: 2 items (4 bytes, big-endian)
            count = 2
            payload += struct.pack('!I', count)

            # Item 1: TRANSACTION (type=2)
            obj_type_1 = 2  # TRANSACTION
            payload += struct.pack('!H', obj_type_1)  # 2 bytes, big-endian

            # Hash 1: 32-byte binary hash (using 'A' repeated for simplicity)
            hash_1 = b'A' * 32
            payload += hash_1

            # Item 2: BLOCK (type=1)
            obj_type_2 = 1  # BLOCK
            payload += struct.pack('!H', obj_type_2)  # 2 bytes, big-endian

            # Hash 2: 32-byte binary hash (using 'B' repeated for simplicity)
            hash_2 = b'B' * 32
            payload += hash_2

            # Total payload size should be: 4 + (2+32) + (2+32) = 72 bytes
            self.log_info(f"INVENTORY payload size: {len(payload)} bytes (expected: 72)")

            # Create and send INVENTORY message
            inv_msg = P2PMessage(MessageType.INVENTORY, payload)
            sent = conn.send_message(inv_msg)

            self.assert_true(sent, "INVENTORY message should be sent successfully")

            # Give node time to process
            time.sleep(0.5)

            # Wait for node to respond with GETDATA
            response = conn.receive_message(timeout=2)

            self.assert_true(response is not None, "Node should send a response for valid INVENTORY")
            self.assert_true(response.msg_type == MessageType.GETDATA, "Node should send GETDATA for valid INVENTORY")

        self.log_info("INVENTORY message test completed")

    def test_2_send_shortened_inventory_message(self):
        """Test that node rejects shortened INVENTORY message (count mismatch)."""
        self.log_info("test_2_send_shortened_inventory_message: Testing invalid INVENTORY (count mismatch)")

        # Get initial log position for tracking new entries
        log_pos = self._get_log_position()

        # Connect to Node 0
        p2p_port = 48333
        with P2PConnection("127.0.0.1", p2p_port, timeout=3) as conn:
            if not conn.socket:
                self.assert_true(False, "Failed to connect to P2P port")
                return

            # Create INVALID INVENTORY payload:
            # Claims 2 items but only provides 1
            # Format: [count:4][type:2][hash:32][type:2][hash:32]...
            payload = b""

            # Count: 2 items (CLAIMS 2, but will only provide 1)
            count = 2
            payload += struct.pack('!I', count)

            # Item 1: TRANSACTION (type=2)
            obj_type_1 = 2  # TRANSACTION
            payload += struct.pack('!H', obj_type_1)
            hash_1 = b'A' * 32
            payload += hash_1

            # MISSING Item 2! This makes the message invalid

            # Expected: 4 + 2*(2+32) = 72 bytes
            # Actual: 4 + 1*(2+32) = 38 bytes
            self.log_info(f"INVENTORY payload: {len(payload)} bytes (claims 2 items, has 1)")
            self.log_info(f"Expected: 72 bytes, Actual: {len(payload)} bytes")

            # Create and send invalid INVENTORY message
            inv_msg = P2PMessage(MessageType.INVENTORY, payload)
            sent = conn.send_message(inv_msg)
            self.assert_true(sent, "Should be able to send message")

            # Give node time to process
            time.sleep(0.5)

            # APPROACH 4: Verify node doesn't respond (negative test)
            # Node should NOT send GETDATA for invalid INVENTORY
            response = conn.receive_message(timeout=2)

            if response is None:
                self.log_info(" Node did not respond to invalid INVENTORY (expected)")
            elif response.msg_type == MessageType.GETDATA:
                self.assert_true(False, "Node should NOT send GETDATA for invalid INVENTORY")
            else:
                self.log_info(f"Node sent {response.msg_type} (unexpected but not GETDATA)")

        # APPROACH 1: Check logs for error message
        new_logs = self._read_new_logs(log_pos)

        # Assert that the node logged an "Invalid INVENTORY" error
        expected_error_string = "No enough payload data"
        self.assert_true(expected_error_string in new_logs,
                        f"Node should log {expected_error_string} error. Log content: {new_logs[:300]}")

        self.log_info(" Node logged 'Invalid INVENTORY' error as expected")

        # Verify error message includes details about expected vs actual bytes
        self.assert_true("expected" in new_logs and "bytes" in new_logs,
                        "Error message should include expected vs actual bytes information")

        self.log_info(" Error message includes expected vs actual bytes")
        self.log_info("Invalid INVENTORY (count mismatch) test completed")

    def test_3_send_invalid_type_inventory_message(self):
        """Test that node handles INVENTORY with invalid object type."""
        self.log_info("test_3_send_invalid_type_inventory_message: Testing invalid type")

        # Get initial log position for tracking new entries
        log_pos = self._get_log_position()

        # Connect to Node 0
        p2p_port = 48333
        with P2PConnection("127.0.0.1", p2p_port, timeout=3) as conn:
            if not conn.socket:
                self.assert_true(False, "Failed to connect to P2P port")
                return

            # Create INVENTORY with INVALID type (not BLOCK=1 or TRANSACTION=2)
            # Format: [count:4][type:2][hash:32]
            payload = b""

            # Count: 1 item
            count = 1
            payload += struct.pack('!I', count)

            # Item 1: INVALID TYPE (type=999)
            # Valid types: 1=BLOCK, 2=TRANSACTION
            obj_type_1 = 999  # INVALID!
            payload += struct.pack('!H', obj_type_1)

            # Hash 1: 32-byte binary hash
            hash_1 = b'X' * 32
            payload += hash_1

            # Message is well-formed (38 bytes) but has invalid type
            self.log_info(f"INVENTORY payload: {len(payload)} bytes (well-formed, but type=999 is invalid)")

            # Create and send INVENTORY message with invalid type
            inv_msg = P2PMessage(MessageType.INVENTORY, payload)
            sent = conn.send_message(inv_msg)
            self.assert_true(sent, "Should be able to send message")

            # Give node time to process
            time.sleep(0.5)

            # APPROACH 4: Verify node doesn't respond (negative test)
            # Node should either:
            # a) Ignore invalid type and not send GETDATA
            # b) Process it but log a warning
            response = conn.receive_message(timeout=2)

            if response is None:
                self.log_info(" Node did not respond to invalid type (expected)")
            elif response.msg_type == MessageType.GETDATA:
                # self.log_info(" Node sent GETDATA for invalid type (may need validation)")
                self.assert_true(False, "Node should NOT send GETDATA for invalid INVENTORY")

            else:
                self.log_info(f"Node sent {response.msg_type}")

        # APPROACH 1: Check logs for warnings/errors
        new_logs = self._read_new_logs(log_pos)

        # Assert that the node processed the message
        # For invalid types, the node should either:
        # 1. Log an error/warning about invalid type, OR
        # 2. Process silently (current implementation may not validate type yet)
        # This assertion verifies that at minimum, we got new log entries (node is alive)
        self.assert_true(new_logs is not None, "Should be able to read logs")

        # Check if node logged anything about invalid/unknown type
        if "invalid" in new_logs.lower() and "999" in new_logs.lower():
            self.assert_true(True, "Node logged message about invalid type")
        else:
            # This is acceptable - current implementation might not validate type yet
            self.log_info(" Node processed message (type validation may not be implemented yet)")
            self.assert_true(False, "Node didn't log message about invalid/unknown type")

        self.log_info("Invalid type INVENTORY test completed")

    def test_4_wrong_network_magic_rejection(self):
        """Test that node rejects P2P messages with wrong network magic bytes."""
        self.log_info("test_4_wrong_network_magic_rejection: Testing wrong magic rejection...")

        # Define magic bytes for different networks (from src/utils/network.h)
        MAINNET_MAGIC = 0x8AC65DF3
        TESTNET_MAGIC = 0xEA71B96E
        LOCALNET_MAGIC = 0xACDE4892  # Expected magic for test environment

        # Get initial log position for verification
        log_pos = self._get_log_position()

        # Test: Send message with TESTNET_MAGIC to LOCALNET node
        p2p_port = 48333  # LOCALNET P2P port
        with P2PConnection("127.0.0.1", p2p_port, timeout=3) as conn:
            if not conn.socket:
                self.assert_true(False, "Failed to connect to P2P port")
                return

            # Create GET_PEERS message with WRONG magic bytes
            wrong_magic_msg = P2PMessage(
                msg_type=MessageType.GET_PEERS,
                payload=b"",
                magic=TESTNET_MAGIC  # Wrong! Node expects LOCALNET_MAGIC
            )

            self.log_info(
                f"Sending GET_PEERS with TESTNET_MAGIC (0x{TESTNET_MAGIC:08X}) "
                f"to node expecting LOCALNET_MAGIC (0x{LOCALNET_MAGIC:08X})"
            )

            sent = conn.send_message(wrong_magic_msg)
            self.assert_true(sent, "Should be able to send message over socket")

            # Give node time to process
            time.sleep(0.5)

            # Node should NOT respond to message with wrong magic
            response = conn.receive_message(timeout=2)

            if response is None:
                self.log_info("Node correctly did not respond to wrong magic")
            else:
                self.assert_true(
                    False,
                    f"Node should NOT respond to message with wrong magic. "
                    f"Got response: {response.msg_type}"
                )

        # Verify node logged something about the rejection
        new_logs = self._read_new_logs(log_pos)

        # Current implementation logs "Incomplete message in buffer, waiting for more data"
        # because Deserialize() returns false but doesn't distinguish between
        # "wrong magic" and "incomplete data"
        if "magic mismatch" in new_logs:
            self.log_info("Node logged about magic mismatch message handling as expected")
        else:
            # If no specific log, that's acceptable - node simply ignored the message
            self.log_info("Node silently ignored wrong magic message (acceptable behavior)")

        self.log_info("Wrong network magic rejection test completed successfully")

    def test_5_reject_message_without_handshake(self):
        """Test that node rejects P2P messages sent without completing VERSION/VERACK handshake."""
        self.log_info("test_5_reject_message_without_handshake: Testing rejection without handshake...")

        # Get initial log position for verification
        log_pos = self._get_log_position()

        # Test: Connect to node but skip handshake and send INVENTORY directly
        p2p_port = 48333
        try:
            # Create raw socket connection without using P2PConnection.connect()
            # (which now does handshake automatically)
            raw_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            raw_socket.settimeout(5)
            raw_socket.connect(("127.0.0.1", p2p_port))

            self.log_info("TCP connection established (no handshake performed)")

            # Try to send INVENTORY message WITHOUT handshake
            # Node should reject it because VERSION/VERACK was not completed
            payload = b""
            count = 1
            payload += struct.pack('!I', count)
            obj_type = 1  # BLOCK type
            payload += struct.pack('!H', obj_type)
            hash_bytes = b'A' * 32
            payload += hash_bytes

            inv_msg = P2PMessage(MessageType.INVENTORY, payload)
            serialized = inv_msg.serialize()
            raw_socket.sendall(serialized)

            self.log_info("Sent INVENTORY message without handshake")

            # Give node time to process and reject
            time.sleep(0.5)

            # Node should NOT respond (connection might be closed)
            raw_socket.settimeout(2)
            try:
                data = raw_socket.recv(1024)
                if len(data) == 0:
                    self.log_info("Connection closed by node (expected - peer not in handshake state)")
                else:
                    # Try to deserialize response
                    msg = P2PMessage.deserialize(data)
                    if msg:
                        self.assert_true(
                            False,
                            f"Node should NOT respond to INVENTORY without handshake. Got: {msg.msg_type}"
                        )
                    else:
                        self.log_info("Node sent data but not a valid P2P message")
            except socket.timeout:
                self.log_info("Node did not respond (expected - message ignored)")

            raw_socket.close()

        except Exception as e:
            self.log_info(f"Connection handling: {e}")

        # Verify node behavior in logs
        new_logs = self._read_new_logs(log_pos)

        # Node should either:
        # 1. Drop the connection (log about peer disconnection)
        # 2. Ignore the message (log about unexpected message)
        # 3. Log about missing handshake
        if new_logs:
            self.log_info(f"Node logged activity (first 500 chars): {new_logs[:500]}")
            # Accept if node logged anything indicating rejection/disconnection
            # The actual log message format depends on implementation
            # self.assert_true(
            #     len(new_logs) > 0,
            #     "Node should log something about the rejected message or connection"
            # )
            expected_error_string = "Disconnected peer"
            self.assert_true(expected_error_string in new_logs,
                "Node should log something about the Disconnected peer"
            )
        else:
            self.log_info("No new logs found (node may have silently ignored)")

        self.log_info("Reject message without handshake test completed successfully")

if __name__ == "__main__":
    unittest.main()
