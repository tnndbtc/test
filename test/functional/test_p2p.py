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
    GET_TX = "get_tx"
    TX = "tx"
    GET_BLOCK = "get_block"
    BLOCK = "block"
    GET_CHAIN = "get_chain"
    CHAIN_INFO = "chain_info"
    UNKNOWN = "unknown"

    @staticmethod
    def is_valid(msg_type):
        """Check if a message type string is valid."""
        valid_types = {
            MessageType.PING, MessageType.PONG, MessageType.GET_PEERS,
            MessageType.PEERS, MessageType.TX_IDS, MessageType.GET_TX,
            MessageType.TX, MessageType.GET_BLOCK, MessageType.BLOCK,
            MessageType.GET_CHAIN, MessageType.CHAIN_INFO
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

            # First, receive header (5 bytes)
            header = self._receive_exactly(P2PMessage.HEADER_SIZE)
            if not header:
                return None

            # Parse header to get payload length
            msg_type, payload_len = struct.unpack('!BI', header)

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
    num_nodes = 4

    def setup(self):
        """Setup test environment - configure to start 4 local nodes."""
        # Nodes will be created as:
        # Node 0: REST API port 28443, P2P port 28333
        # Node 1: REST API port 28444, P2P port 28334
        # Node 2: REST API port 28445, P2P port 28335
        # Node 3: REST API port 28446, P2P port 28336
        #
        # Nodes are NOT automatically connected - test case will use:
        # self.test_nodes[0].connect_to_peer(self.test_nodes[1])
        pass

    def test_nodes_are_running(self):
        """Verify all nodes are running."""
        self.log_info("test_nodes_are_running: Verifying all nodes are running...")

        for test_node in self.test_nodes:
            # Check if process is alive
            if test_node.node.process and test_node.node.process.poll() is None:
                self.assert_true(True, f"Node {test_node.index} process is running")
            else:
                self.assert_true(False, f"Node {test_node.index} process should be running")

            self.log_info(f"Node {test_node.index} is running")

    def test_port_isolation(self):
        """Verify nodes are listening on different ports."""
        self.log_info("test_port_isolation: Verifying port isolation...")

        ports_used = set()
        base_port = 28443

        for test_node in self.test_nodes:
            expected_port = base_port + test_node.index
            self.assert_equal(
                test_node.port,
                expected_port,
                f"Node {test_node.index} should use port {expected_port}"
            )
            self.assert_true(
                expected_port not in ports_used,
                f"Port {expected_port} should be unique"
            )
            ports_used.add(expected_port)
            self.log_info(f"Node {test_node.index} confirmed on unique port {expected_port}")

    def test_mining_status(self):
        """Verify mining is enabled on all nodes (mining starts automatically)."""
        self.log_info("test_mining_status: Verifying mining status on all nodes...")

        for test_node in self.test_nodes:
            chain_info = test_node.get_chain_info()

            # Mining should be enabled by default when daemon starts
            self.assert_equal(
                chain_info.get("mining_enabled"),
                True,
                f"Node {test_node.index} should have mining enabled by default"
            )
            self.log_info(f"Node {test_node.index} mining status confirmed: enabled={chain_info['mining_enabled']}")

    def test_inbound_peer_connections(self):
        """
        Test that nodes can establish inbound connections.

        We'll test that when Node 1 connects to Node 0, Node 0 shows
        an inbound connection and Node 1 shows an outbound connection.
        """
        self.log_info("test_inbound peer connections: Testing inbound peer connections...")

        # For this test, we would need to:
        # 1. Have Node 1 connect to Node 0 (Node 1 makes outbound, Node 0 receives inbound)
        # 2. Query Node 0 to verify it has 1 inbound peer
        # 3. Query Node 1 to verify it has 1 outbound peer

        # Note: This requires implementing peer statistics endpoint in REST API
        # For now, we log that this test would verify inbound connections
        self.log_info("Inbound connection test: Would verify Node 0 accepts inbound from Node 1")

        # If REST API had /peer/stats endpoint, we would do:
        # Node 1 connects to Node 0's P2P port (28333)
        # response = self.nodes[0].get("/peer/stats")
        # self.assert_equal(response.json()["inbound_peers"], 1)

        # For now, just verify nodes are still responsive
        for test_node in self.test_nodes:
            self.assert_true(
                test_node.is_ready(),
                f"Node {test_node.index} should still be responsive"
            )

        self.log_info("Inbound connection test placeholder completed")

    def test_node0_outbound_connections(self):
        """
        Test that node0 can establish outbound connections to node1, node2, and node3 using TestNode.

        This test verifies:
        - TestNode.connect_to_peer() works correctly
        - Peer connections are properly established
        - Peer info can be queried via get_peer_info()
        - Connections remain stable during the test
        """
        self.log_info("test_node0 outbound connections via TestNode: Testing node0 outbound connections via TestNode...")

        # Node0 will connect to node1, node2, node3 using TestNode.connect_to_peer
        # Node 0: REST 28443, P2P port 28333
        # Node 1: REST 28444, P2P port 28334
        # Node 2: REST 28445, P2P port 28335
        # Node 3: REST 28446, P2P port 28336

        node0 = self.test_nodes[0]

        target_nodes = [
            self.test_nodes[1],
            self.test_nodes[2],
            self.test_nodes[3]
        ]

        successful_connections = 0

        # Use TestNode.connect_to_peer to establish connections
        for peer_node in target_nodes:
            self.log_info(
                f"Node{node0.index} connecting to Node{peer_node.index} "
                f"at 127.0.0.1:{peer_node.p2p_port}..."
            )

            # Use the high-level connect_to_peer method
            if node0.connect_to_peer(peer_node, wait=True):
                self.log_info(f"Node{node0.index} successfully connected to Node{peer_node.index}")
                successful_connections += 1
            else:
                self.log_info(f"WARNING: Failed to connect Node{node0.index} to Node{peer_node.index}")
                self.assert_true(False, f"Node{node0.index} failed to add peer to Node{peer_node.index}")

        # Verify we established at least one connection
        self.assert_true(
            successful_connections == len(target_nodes),
            "Node0 should establish at least one outbound connection"
        )

        # Query peer info using TestNode.get_peer_info()
        self.log_info("Querying Node0 peer info...")
        peer_info = node0.get_peer_info()

        self.assert_in("status", peer_info, "Response should contain status field")
        self.assert_equal(peer_info["status"], "success", "get_peer_info should return success")
        self.assert_in("total_peers", peer_info, "Response should contain total_peers")
        self.assert_in("outbound_peers", peer_info, "Response should contain outbound_peers")
        self.assert_in("inbound_peers", peer_info, "Response should contain inbound_peers")
        self.assert_in("peers", peer_info, "Response should contain peers list")

        total_peers = peer_info["total_peers"]
        outbound_peers = peer_info["outbound_peers"]
        self.assert_true(outbound_peers==successful_connections, f"Node{node0.index} should have {outbound_peers} total outbound peers")
        inbound_peers = peer_info["inbound_peers"]
        self.assert_true(inbound_peers==0, f"Node{node0.index} should have {inbound_peers} total inbound peers")
        peer_list = peer_info["peers"]

        self.log_info(
            f"Node{node0.index} peer status: total={total_peers}, "
            f"outbound={outbound_peers}, inbound={inbound_peers}"
        )
        self.log_info(f"Connected peers: {peer_list}")

        # Verify peer counts make sense
        self.assert_true(
            total_peers >= 0,
            "Total peers should be non-negative"
        )

        # Verify nodes are still responsive after peer connections
        self.log_info("Verifying all nodes remain responsive after peer connections...")
        for test_node in self.test_nodes:
            self.assert_true(
                test_node.is_ready(),
                f"Node{test_node.index} should still be responsive"
            )

        self.log_info(
            f"Node{node0.index} outbound connections test completed successfully "
            f"({successful_connections}/3 connections established)"
        )

    # ==================================================================
    # P2P Message Protocol Tests
    # ==================================================================

    def test_message_serialization(self):
        """Test P2P message serialization."""
        self.log_info("test_P2P message serialization: Testing P2P message serialization...")

        # Create a PING message
        ping_msg = P2PMessage(MessageType.PING)
        serialized = ping_msg.serialize()

        # Format: [1 byte type_length][4 bytes "ping"][4 bytes payload_length]
        # Total: 1 + 4 + 4 = 9 bytes
        self.assert_equal(len(serialized), 9, "PING message should be 9 bytes")
        self.assert_equal(serialized[0], 4, "First byte should be type length (4)")

        # Verify type string is "ping"
        type_str = serialized[1:5].decode('utf-8')
        self.assert_equal(type_str, "ping", "Type string should be 'ping'")

        # Verify payload length is 0 (bytes 5-8, big-endian)
        payload_len = struct.unpack('!I', serialized[5:9])[0]
        self.assert_equal(payload_len, 0, "PING message should have 0 payload length")

        self.log_info("Message serialization test completed")

    def test_message_deserialization(self):
        """Test P2P message deserialization."""
        self.log_info("test_P2P message deserialization: Testing P2P message deserialization...")

        # Create a raw PONG message manually
        # Format: [1 byte type_length][4 bytes "pong"][4 bytes payload_length]
        msg_type = MessageType.PONG
        type_bytes = msg_type.encode('utf-8')
        payload = b""

        raw_msg = struct.pack('B', len(type_bytes))  # Type length
        raw_msg += type_bytes                         # Type string
        raw_msg += struct.pack('!I', len(payload))   # Payload length
        raw_msg += payload                            # Payload data

        # Deserialize
        pong_msg = P2PMessage.deserialize(raw_msg)

        self.assert_true(pong_msg is not None, "Deserialization should succeed")
        self.assert_equal(pong_msg.msg_type, MessageType.PONG, "Message type should be PONG")
        self.assert_equal(len(pong_msg.payload), 0, "Payload should be empty")

        self.log_info("Message deserialization test completed")

    def test_message_round_trip(self):
        """Test message serialization followed by deserialization."""
        self.log_info("test_message round-trip serialization: Testing message round-trip serialization...")

        # Create a GET_PEERS message
        original = P2PMessage(MessageType.GET_PEERS)

        # Serialize and deserialize
        serialized = original.serialize()
        deserialized = P2PMessage.deserialize(serialized)

        self.assert_true(deserialized is not None, "Round-trip should succeed")
        self.assert_equal(deserialized.msg_type, original.msg_type, "Message type should match")
        self.assert_equal(deserialized.payload, original.payload, "Payload should match")

        self.log_info("Message round-trip test completed")

    def test_message_types(self):
        """Test all message types can be serialized/deserialized."""
        self.log_info("test_all message types: Testing all message types...")

        message_types = [
            MessageType.PING,
            MessageType.PONG,
            MessageType.GET_PEERS,
            MessageType.PEERS,
            MessageType.TX_IDS,
            MessageType.GET_TX,
            MessageType.TX,
            MessageType.GET_BLOCK,
            MessageType.BLOCK,
            MessageType.GET_CHAIN,
            MessageType.CHAIN_INFO
        ]

        for msg_type in message_types:
            # Create message
            msg = P2PMessage(msg_type)

            # Serialize and deserialize
            serialized = msg.serialize()
            deserialized = P2PMessage.deserialize(serialized)

            self.assert_true(
                deserialized is not None,
                f"{msg_type.upper()} should serialize/deserialize"
            )
            self.assert_equal(
                deserialized.msg_type,
                msg_type,
                f"{msg_type.upper()} type should match"
            )

        self.log_info("All message types test completed")

    def test_message_with_payload(self):
        """Test messages with payload data."""
        self.log_info("test_messages with payload: Testing messages with payload...")

        # Create TX_IDS message with payload
        tx_ids = "tx1_abc123,tx2_def456,tx3_ghi789"
        msg = P2PMessage(MessageType.TX_IDS, tx_ids)

        # Serialize and deserialize
        serialized = msg.serialize()
        deserialized = P2PMessage.deserialize(serialized)

        self.assert_true(deserialized is not None, "Deserialization should succeed")
        self.assert_equal(deserialized.msg_type, MessageType.TX_IDS, "Type should be TX_IDS")
        self.assert_equal(
            deserialized.payload.decode('utf-8'),
            tx_ids,
            "Payload should match original"
        )

        self.log_info("Message with payload test completed")

    def test_message_empty_payload(self):
        """Test messages with empty payload."""
        self.log_info("test_messages with empty payload: Testing messages with empty payload...")

        # Create message with empty string payload
        msg = P2PMessage(MessageType.GET_CHAIN, "")

        # Format: [1 byte type_length][9 bytes "get_chain"][4 bytes payload_length][0 bytes payload]
        # Total: 1 + 9 + 4 = 14 bytes
        serialized = msg.serialize()
        self.assert_equal(len(serialized), 14, "Empty payload message should be 14 bytes")

        deserialized = P2PMessage.deserialize(serialized)
        self.assert_true(deserialized is not None, "Deserialization should succeed")
        self.assert_equal(len(deserialized.payload), 0, "Payload should be empty")

        self.log_info("Empty payload test completed")

    def test_message_large_payload(self):
        """Test messages with large payload."""
        self.log_info("test_messages with large payload: Testing messages with large payload...")

        # Create message with 1000-byte payload
        large_payload = b"X" * 1000
        msg = P2PMessage(MessageType.BLOCK, large_payload)

        # Format: [1 byte type_length][5 bytes "block"][4 bytes payload_length][1000 bytes payload]
        # Total: 1 + 5 + 4 + 1000 = 1010 bytes
        serialized = msg.serialize()
        self.assert_equal(len(serialized), 1010, "Serialized size should be 1010 bytes")

        deserialized = P2PMessage.deserialize(serialized)
        self.assert_true(deserialized is not None, "Deserialization should succeed")
        self.assert_equal(len(deserialized.payload), 1000, "Payload size should be 1000")
        self.assert_equal(deserialized.payload, large_payload, "Payload should match")

        self.log_info("Large payload test completed")

    def test_send_ping_message(self):
        """Test sending PING message to a node."""
        self.log_info("test_sending PING message: Testing sending PING message...")

        # Connect to Node 0
        p2p_port = 28333
        with P2PConnection("127.0.0.1", p2p_port, timeout=3) as conn:
            if not conn.socket:
                self.assert_true(False, "Failed to connect to P2P port")
                return

            # Send PING message
            ping_msg = P2PMessage(MessageType.PING)
            sent = conn.send_message(ping_msg)

            self.assert_true(sent, "PING message should be sent successfully")

            # Try to receive PONG response (with timeout)
            # Note: This may timeout if node doesn't implement auto-PONG yet
            pong_msg = conn.receive_message(timeout=2)

            if pong_msg:
                self.log_info(f"Received response: {pong_msg.get_type_string()}")
                # Could be PONG or any other message the node sends
            else:
                self.log_info("No response received (node may not auto-respond to PING)")

        self.log_info("Send PING message test completed")

    def test_send_get_peers_message(self):
        """Test sending GET_PEERS message to a node."""
        self.log_info("test_sending GET_PEERS message: Testing sending GET_PEERS message...")

        # Connect to Node 1
        p2p_port = 28334
        with P2PConnection("127.0.0.1", p2p_port, timeout=3) as conn:
            if not conn.socket:
                self.assert_true(False, "Failed to connect to P2P port")
                return

            # Send GET_PEERS message
            get_peers_msg = P2PMessage(MessageType.GET_PEERS)
            sent = conn.send_message(get_peers_msg)

            self.assert_true(sent, "GET_PEERS message should be sent successfully")

            # Try to receive PEERS response
            peers_msg = conn.receive_message(timeout=2)

            if peers_msg:
                self.log_info(f"Received response: {peers_msg.get_type_string()}")
                if peers_msg.msg_type == MessageType.PEERS:
                    self.log_info(f"Peer list payload: {peers_msg.payload[:100]}")
            else:
                self.log_info("No PEERS response received")

        self.log_info("Send GET_PEERS message test completed")

    def test_send_tx_ids_message(self):
        """Test sending TX_IDS message with transaction IDs."""
        self.log_info("test_sending TX_IDS message: Testing sending TX_IDS message...")

        # Connect to Node 2
        p2p_port = 28335
        with P2PConnection("127.0.0.1", p2p_port, timeout=3) as conn:
            if not conn.socket:
                self.assert_true(False, "Failed to connect to P2P port")
                return

            # Send TX_IDS message with sample transaction IDs
            tx_ids = "tx_abc123,tx_def456,tx_ghi789"
            tx_ids_msg = P2PMessage(MessageType.TX_IDS, tx_ids)
            sent = conn.send_message(tx_ids_msg)

            self.assert_true(sent, "TX_IDS message should be sent successfully")

            # Verify message size
            # Format: [1 byte type_length][6 bytes "tx_ids"][4 bytes payload_length][payload]
            # Total: 1 + 6 + 4 + len(payload) = 11 + len(payload)
            serialized = tx_ids_msg.serialize()
            expected_size = 11 + len(tx_ids.encode('utf-8'))
            self.assert_equal(
                len(serialized),
                expected_size,
                f"Serialized TX_IDS should be {expected_size} bytes"
            )

        self.log_info("Send TX_IDS message test completed")

    def test_message_binary_payload(self):
        """Test messages with binary (non-text) payload."""
        self.log_info("test_messages with binary payload: Testing messages with binary payload...")

        # Create binary payload with null bytes
        binary_payload = bytes([0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00, 0xAA, 0xBB])
        msg = P2PMessage(MessageType.TX, binary_payload)

        # Serialize and deserialize
        serialized = msg.serialize()
        deserialized = P2PMessage.deserialize(serialized)

        self.assert_true(deserialized is not None, "Binary payload deserialization should succeed")
        self.assert_equal(
            deserialized.payload,
            binary_payload,
            "Binary payload should match exactly"
        )

        # Send to Node 3
        p2p_port = 28336
        with P2PConnection("127.0.0.1", p2p_port, timeout=3) as conn:
            if conn.socket:
                sent = conn.send_message(msg)
                self.assert_true(sent, "Binary payload message should be sent")

        self.log_info("Binary payload test completed")


if __name__ == "__main__":
    unittest.main()
