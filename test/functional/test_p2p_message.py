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
    VERACK = "verack"
    GETDATA = "getdata"
    UNKNOWN = "unknown"

    @staticmethod
    def is_valid(msg_type):
        """Check if a message type string is valid."""
        valid_types = {
            MessageType.PING, MessageType.PONG, MessageType.GET_PEERS,
            MessageType.PEERS, MessageType.TX_IDS, MessageType.TXS,
            MessageType.BLOCKS, MessageType.GET_CHAIN, MessageType.CHAIN_INFO,
            MessageType.INVENTORY, MessageType.VERSION, MessageType.VERACK,
            MessageType.GETDATA
        }
        return msg_type in valid_types


class P2PMessage:
    """
    Helper class for P2P message serialization/deserialization.

    Matches CPeerMessage format from peer_message.h:
    - 4 bytes: Magic bytes (uint32_t, network byte order)
    - 1 byte: Type string length (uint8_t)
    - N bytes: Type string (e.g., "ping", "get_peers")
    - 4 bytes: Payload length (uint32_t, network byte order)
    - M bytes: Payload data
    """

    MIN_HEADER_SIZE = 9  # 4 bytes magic + 1 byte type_length + 0 bytes type + 4 bytes payload_length
    LOCALNET_MAGIC = 0xACDE4892  # Must match LOCALNET_MAGIC in network.h

    def __init__(self, msg_type=MessageType.UNKNOWN, payload=b"", magic=None):
        """
        Create a P2P message.

        Args:
            msg_type: Message type string (from MessageType class)
            payload: Message payload (bytes or string)
            magic: Network magic bytes (default: LOCALNET_MAGIC)
        """
        self.msg_type = msg_type
        self.magic = magic if magic is not None else P2PMessage.LOCALNET_MAGIC
        if isinstance(payload, str):
            self.payload = payload.encode('utf-8')
        else:
            self.payload = payload

    def serialize(self):
        """
        Serialize message to bytes for network transmission.

        Returns:
            bytes: Serialized message [magic][type_length][type_string][payload_length][payload]
        """
        # Convert type to bytes
        type_bytes = self.msg_type.encode('utf-8')
        type_len = len(type_bytes)

        # Pack as: 4 bytes magic + 1 byte type_length + N bytes type + 4 bytes payload_length + M bytes payload
        result = struct.pack('!I', self.magic)  # Magic bytes (4 bytes, big-endian)
        result += struct.pack('B', type_len)    # Type length (1 byte)
        result += type_bytes                    # Type string (N bytes)
        result += struct.pack('!I', len(self.payload))  # Payload length (4 bytes, big-endian)
        result += self.payload                  # Payload data (M bytes)

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
        if len(data) < 4:
            return None

        offset = 0

        # 1. Parse magic bytes (4 bytes, big-endian)
        magic = struct.unpack('!I', data[offset:offset+4])[0]
        offset += 4

        # 2. Check if we have enough data for type length
        if len(data) < offset + 1:
            return None

        # 3. Parse type length (1 byte)
        type_len = struct.unpack('B', data[offset:offset+1])[0]
        offset += 1

        # 4. Check if we have enough data for type string
        if len(data) < offset + type_len:
            return None

        # 5. Parse type string (N bytes)
        msg_type = data[offset:offset+type_len].decode('utf-8')
        offset += type_len

        # 6. Check if we have enough data for payload length
        if len(data) < offset + 4:
            return None

        # 7. Parse payload length (4 bytes, big-endian)
        payload_len = struct.unpack('!I', data[offset:offset+4])[0]
        offset += 4

        # 8. Check if we have enough data for payload
        if len(data) < offset + payload_len:
            return None

        # 9. Extract payload
        payload = data[offset:offset+payload_len]

        return P2PMessage(msg_type, payload, magic)

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

    def _create_version_message(self):
        """
        Create VERSION message payload.

        VERSION payload format (76 bytes total):
        - protocol_version: 4 bytes (int32_t, big-endian)
        - timestamp: 8 bytes (int64_t, big-endian, UNIX time)
        - address_recv: 26 bytes (CNetworkAddress - peer we're connecting to)
          - services: 8 bytes (uint64_t, big-endian) - set to 0
          - ipv6: 16 bytes (IPv4-mapped: 10 bytes 0x00, 2 bytes 0xFF, 4 bytes IPv4)
          - port: 2 bytes (uint16_t, big-endian)
        - address_from: 26 bytes (CNetworkAddress - our address)
          - services: 8 bytes (uint64_t, big-endian) - NODE_NETWORK = 1
          - ipv6: 16 bytes (IPv4-mapped: 127.0.0.1)
          - port: 2 bytes (uint16_t, big-endian) - set to 0
        - nonce: 8 bytes (uint64_t, big-endian) - random value
        - chain_tip_height: 4 bytes (int32_t, big-endian) - set to 0

        Returns:
            bytes: VERSION message payload (76 bytes)
        """
        import random
        payload = b""

        # Protocol version (4 bytes)
        PROTOCOL_VERSION = 1
        payload += struct.pack('!i', PROTOCOL_VERSION)

        # Timestamp (8 bytes) - current UNIX time
        timestamp = int(time.time())
        payload += struct.pack('!q', timestamp)

        # address_recv (26 bytes) - peer we're connecting to
        # services: 0 (we don't know peer's services yet)
        payload += struct.pack('!Q', 0)
        # IPv6 address (16 bytes) - IPv4-mapped for 127.0.0.1
        ipv6_bytes = b'\x00' * 10 + b'\xff\xff' + b'\x7f\x00\x00\x01'
        payload += ipv6_bytes
        # port (2 bytes)
        payload += struct.pack('!H', self.port)

        # address_from (26 bytes) - our address
        # services: NODE_NETWORK = 1
        NODE_NETWORK = 1
        payload += struct.pack('!Q', NODE_NETWORK)
        # IPv6 address (16 bytes) - IPv4-mapped for 127.0.0.1
        payload += ipv6_bytes
        # port (2 bytes) - set to 0
        payload += struct.pack('!H', 0)

        # nonce (8 bytes) - random value
        nonce = random.randint(0, 2**64 - 1)
        payload += struct.pack('!Q', nonce)

        # chain_tip_height (4 bytes) - set to 0 (we don't know)
        payload += struct.pack('!i', 0)

        return payload

    def _do_handshake(self):
        """
        Perform VERSION/VERACK handshake.

        Handshake sequence:
        1. Send VERSION to peer
        2. Receive VERSION from peer
        3. Send VERACK to peer
        4. Receive VERACK from peer

        Returns:
            bool: True if handshake successful, False otherwise
        """
        try:
            # Step 1: Send VERSION
            version_payload = self._create_version_message()
            version_msg = P2PMessage(MessageType.VERSION, version_payload)
            if not self.send_message(version_msg):
                print("Handshake failed: Could not send VERSION")
                return False

            # Step 2: Receive VERSION from peer
            received_version = self.receive_message(timeout=5)
            if not received_version or received_version.msg_type != MessageType.VERSION:
                print(f"Handshake failed: Expected VERSION, got {received_version.msg_type if received_version else 'None'}")
                return False

            # Step 3: Send VERACK
            verack_msg = P2PMessage(MessageType.VERACK, b"")
            if not self.send_message(verack_msg):
                print("Handshake failed: Could not send VERACK")
                return False

            # Step 4: Receive VERACK from peer
            received_verack = self.receive_message(timeout=5)
            if not received_verack or received_verack.msg_type != MessageType.VERACK:
                print(f"Handshake failed: Expected VERACK, got {received_verack.msg_type if received_verack else 'None'}")
                return False

            return True

        except Exception as e:
            print(f"Handshake failed with exception: {e}")
            return False

    def connect(self):
        """
        Connect to the P2P node and perform VERSION/VERACK handshake.

        Returns:
            bool: True if connection and handshake successful, False otherwise
        """
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(self.timeout)
            self.socket.connect((self.host, self.port))

            # Perform VERSION/VERACK handshake
            if not self._do_handshake():
                self.close()
                return False

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

            # First, receive magic bytes (4 bytes)
            magic_bytes = self._receive_exactly(4)
            if not magic_bytes:
                return None

            magic = struct.unpack('!I', magic_bytes)[0]

            # Receive type_length (1 byte)
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

            return P2PMessage(msg_type, payload, magic)

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
