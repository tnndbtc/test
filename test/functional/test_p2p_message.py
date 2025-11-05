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
- Different message types (PING, PONG, GET_PEERS, TX_IDS, etc.)
"""

import sys
import os
import time
import unittest
import socket
import struct
from test_framework import TestFramework


class MessageType:
    """
    P2P message types - must match MessageType namespace in peer_message.h.

    Message format:
    - 1 byte: Type string length
    - N bytes: Type string (e.g., "ping", "get_peers")
    - 4 bytes: Payload length (network byte order / big-endian)
    - M bytes: Payload data
    """
    PING = "ping"
    PONG = "pong"
    GET_PEERS = "get_peers"
    PEERS = "peers"
    TX_IDS = "tx_ids"
    TXS = "txs"
    BLOCK = "blocks"
    GET_CHAIN = "get_chain"
    CHAIN_INFO = "chain_info"
    INVENTORY = "inv"
    VERSION = "version"
    GETDATA = "getdata"
    UNKNOWN = "unknown"

    @staticmethod
    def is_valid(msg_type):
        """Check if a message type string is valid."""
        valid_types = {
            MessageType.PING, MessageType.PONG, MessageType.GET_PEERS,
            MessageType.PEERS, MessageType.TX_IDS, MessageType.TXS,
            MessageType.BLOCKS, MessageType.GET_CHAIN, MessageType.CHAIN_INFO,
            MessageType.INVENTORY, MessageType.VERSION, MessageType.GETDATA
        }
        return msg_type in valid_types


class P2PMessage:
    """
    Helper class for P2P message serialization/deserialization.

    Matches CPeerMessage format from peer_message.h:
    - 1 byte: Type string length (uint8_t)
    - N bytes: Type string (e.g., "ping", "get_peers")
    - 4 bytes: Payload length (uint32_t, network byte order)
    - M bytes: Payload data
    """

    MIN_HEADER_SIZE = 5  # 1 byte type_length + 0 bytes type + 4 bytes payload_length

    def __init__(self, msg_type=MessageType.UNKNOWN, payload=b""):
        """
        Create a P2P message.

        Args:
            msg_type: Message type string (from MessageType class)
            payload: Message payload (bytes or string)
        """
        self.msg_type = msg_type
        if isinstance(payload, str):
            self.payload = payload.encode('utf-8')
        else:
            self.payload = payload

    def serialize(self):
        """
        Serialize message to bytes for network transmission.

        Returns:
            bytes: Serialized message [type_length][type_string][payload_length][payload]
        """
        # Convert type to bytes
        type_bytes = self.msg_type.encode('utf-8')
        type_len = len(type_bytes)

        # Pack as: 1 byte type_length + N bytes type + 4 bytes payload_length + M bytes payload
        result = struct.pack('B', type_len)  # Type length (1 byte)
        result += type_bytes                  # Type string (N bytes)
        result += struct.pack('!I', len(self.payload))  # Payload length (4 bytes, big-endian)
        result += self.payload                # Payload data (M bytes)

        return result

    @staticmethod
    def deserialize(data):
        """
        Deserialize message from bytes.

        Args:
            data: Raw bytes from network

        Returns:
            P2PMessage: Deserialized message, or None if invalid
        """
        if len(data) < 1:
            return None

        offset = 0

        # 1. Parse type length (1 byte)
        type_len = struct.unpack('B', data[offset:offset+1])[0]
        offset += 1

        # 2. Check if we have enough data for type string
        if len(data) < offset + type_len:
            return None

        # 3. Parse type string (N bytes)
        msg_type = data[offset:offset+type_len].decode('utf-8')
        offset += type_len

        # 4. Check if we have enough data for payload length
        if len(data) < offset + 4:
            return None

        # 5. Parse payload length (4 bytes, big-endian)
        payload_len = struct.unpack('!I', data[offset:offset+4])[0]
        offset += 4

        # 6. Check if we have enough data for payload
        if len(data) < offset + payload_len:
            return None

        # 7. Extract payload
        payload = data[offset:offset+payload_len]

        return P2PMessage(msg_type, payload)

    def get_type_string(self):
        """Get string representation of message type."""
        return self.msg_type.upper()

    def __str__(self):
        """String representation of message."""
        payload_preview = self.payload[:20] + b"..." if len(self.payload) > 20 else self.payload
        return f"P2PMessage({self.get_type_string()}, {len(self.payload)} bytes, payload={payload_preview})"


class P2PConnection:
    """
    Helper class for connecting to nodes via TCP P2P sockets.

    Provides methods to:
    - Connect to a node's P2P port
    - Send P2P messages
    - Receive P2P messages
    - Close connection
    """

    def __init__(self, host, port, timeout=5):
        """
        Create a P2P connection.

        Args:
            host: Hostname or IP address
            port: P2P port number
            timeout: Socket timeout in seconds
        """
        self.host = host
        self.port = port
        self.timeout = timeout
        self.socket = None

    def connect(self):
        """
        Connect to the P2P node.

        Returns:
            bool: True if connection successful, False otherwise
        """
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(self.timeout)
            self.socket.connect((self.host, self.port))
            return True
        except Exception as e:
            print(f"Failed to connect to {self.host}:{self.port}: {e}")
            return False

    def send_message(self, message):
        """
        Send a P2P message.

        Args:
            message: P2PMessage instance

        Returns:
            bool: True if send successful, False otherwise
        """
        if not self.socket:
            return False

        try:
            serialized = message.serialize()
            self.socket.sendall(serialized)
            return True
        except Exception as e:
            print(f"Failed to send message: {e}")
            return False

    def receive_message(self, timeout=None):
        """
        Receive a P2P message.

        Args:
            timeout: Optional timeout in seconds (overrides default)

        Returns:
            P2PMessage: Received message, or None if error/timeout
        """
        if not self.socket:
            return None

        try:
            if timeout is not None:
                old_timeout = self.socket.gettimeout()
                self.socket.settimeout(timeout)

            # First, receive type_length (1 byte)
            type_len_bytes = self._receive_exactly(1)
            if not type_len_bytes:
                return None

            type_len = struct.unpack('!B', type_len_bytes)[0]

            # Receive type string (type_len bytes)
            type_bytes = self._receive_exactly(type_len)
            if not type_bytes:
                return None

            msg_type = type_bytes.decode('utf-8')

            # Receive payload_length (4 bytes)
            payload_len_bytes = self._receive_exactly(4)
            if not payload_len_bytes:
                return None

            payload_len = struct.unpack('!I', payload_len_bytes)[0]

            # Receive payload
            payload = self._receive_exactly(payload_len) if payload_len > 0 else b""

            # Restore old timeout if changed
            if timeout is not None:
                self.socket.settimeout(old_timeout)

            return P2PMessage(msg_type, payload)

        except socket.timeout:
            return None
        except Exception as e:
            print(f"Failed to receive message: {e}")
            return None

    def _receive_exactly(self, n_bytes):
        """
        Receive exactly n bytes from socket.

        Args:
            n_bytes: Number of bytes to receive

        Returns:
            bytes: Received data, or None if connection closed
        """
        data = b""
        while len(data) < n_bytes:
            chunk = self.socket.recv(n_bytes - len(data))
            if not chunk:
                return None  # Connection closed
            data += chunk
        return data

    def close(self):
        """Close the connection."""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None

    def __enter__(self):
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()


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

        # Set log file path - node0's log is in tmpdir/node0/log/bweave.log
        self.log_file = self.tmpdir / "node0" / "log" / "bweave.log"

    def _get_log_position(self):
        """Get current position in log file for tracking new entries."""
        try:
            with open(self.log_file, 'r') as f:
                f.seek(0, 2)  # Seek to end
                return f.tell()
        except Exception as e:
            self.log_info(f"Could not get log position: {e}")
            return None

    def _read_new_logs(self, log_position):
        """Read log entries added since the given position."""
        if log_position is None:
            return ""
        try:
            platform_specific_behavior = 'r+' # for apple darwin
            with open(self.log_file, platform_specific_behavior) as f:
                # on Linux, log contents is still in OS buffer because node only called flush().  Force write to disk first
                os.fsync(f.fileno())
                f.seek(log_position)
                return f.read()
        except Exception as e:
            self.log_info(f"Could not read logs: {e}")
            return ""

    def test_1_send_valid_inventory_message(self):
        """Test sending INVENTORY message to a node."""
        self.log_info("test_1_send_valid_inventory_message: Testing valid INVENTORY message")

        # Connect to Node 0
        p2p_port = 28333
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

            # APPROACH 4: Verify node doesn't respond (negative test)
            # Node should NOT send GETDATA for invalid INVENTORY
            response = conn.receive_message(timeout=2)

            self.assert_true(response.msg_type == MessageType.GETDATA, "Node should send GETDATA for valid INVENTORY")

        self.log_info("INVENTORY message test completed")

    def test_2_send_shortened_inventory_message(self):
        """Test that node rejects shortened INVENTORY message (count mismatch)."""
        self.log_info("test_2_send_shortened_inventory_message: Testing invalid INVENTORY (count mismatch)")

        # Get initial log position for tracking new entries
        log_pos = self._get_log_position()

        # Connect to Node 0
        p2p_port = 28333
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
                self.log_info("✓ Node did not respond to invalid INVENTORY (expected)")
            elif response.msg_type == MessageType.GETDATA:
                self.assert_true(False, "Node should NOT send GETDATA for invalid INVENTORY")
            else:
                self.log_info(f"Node sent {response.msg_type} (unexpected but not GETDATA)")

        # APPROACH 1: Check logs for error message
        new_logs = self._read_new_logs(log_pos)

        # Assert that the node logged an "Invalid INVENTORY" error
        self.assert_true("Invalid INVENTORY" in new_logs,
                        f"Node should log 'Invalid INVENTORY' error. Log content: {new_logs[:300]}")

        self.log_info("✓ Node logged 'Invalid INVENTORY' error as expected")

        # Verify error message includes details about expected vs actual bytes
        self.assert_true("expected" in new_logs and "bytes" in new_logs,
                        "Error message should include expected vs actual bytes information")

        self.log_info("✓ Error message includes expected vs actual bytes")
        self.log_info("Invalid INVENTORY (count mismatch) test completed")

    def test_3_send_invalid_type_inventory_message(self):
        """Test that node handles INVENTORY with invalid object type."""
        self.log_info("test_3_send_invalid_type_inventory_message: Testing invalid type")

        # Get initial log position for tracking new entries
        log_pos = self._get_log_position()

        # Connect to Node 0
        p2p_port = 28333
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
                self.log_info("✓ Node did not respond to invalid type (expected)")
            elif response.msg_type == MessageType.GETDATA:
                # self.log_info("⚠ Node sent GETDATA for invalid type (may need validation)")
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
        if "invalid" in new_logs.lower() and "unknown" in new_logs.lower():
            self.assert_true(True, "Node logged message about invalid/unknown type")
        else:
            # This is acceptable - current implementation might not validate type yet
            self.log_info("ℹ Node processed message (type validation may not be implemented yet)")
            self.assert_true(False, "Node didn't log message about invalid/unknown type")

        self.log_info("Invalid type INVENTORY test completed")

if __name__ == "__main__":
    unittest.main()
