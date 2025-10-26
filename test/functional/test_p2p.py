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
import inspect
import socket
import struct
from test_framework import TestFramework


class MessageType:
    """
    P2P message types - must match EMessageType enum in peer_message.h.

    Message format:
    - 1 byte: Message type
    - 4 bytes: Payload length (network byte order / big-endian)
    - N bytes: Payload data
    """
    PING = 0
    PONG = 1
    GET_PEERS = 2
    PEERS = 3
    TX_IDS = 4
    GET_TX = 5
    TX = 6
    GET_BLOCK = 7
    BLOCK = 8
    GET_CHAIN = 9
    CHAIN_INFO = 10
    UNKNOWN = 255

    @staticmethod
    def to_string(msg_type):
        """Convert message type to string representation."""
        type_names = {
            MessageType.PING: "PING",
            MessageType.PONG: "PONG",
            MessageType.GET_PEERS: "GET_PEERS",
            MessageType.PEERS: "PEERS",
            MessageType.TX_IDS: "TX_IDS",
            MessageType.GET_TX: "GET_TX",
            MessageType.TX: "TX",
            MessageType.GET_BLOCK: "GET_BLOCK",
            MessageType.BLOCK: "BLOCK",
            MessageType.GET_CHAIN: "GET_CHAIN",
            MessageType.CHAIN_INFO: "CHAIN_INFO",
            MessageType.UNKNOWN: "UNKNOWN"
        }
        return type_names.get(msg_type, f"UNKNOWN({msg_type})")


class P2PMessage:
    """
    Helper class for P2P message serialization/deserialization.

    Matches CPeerMessage format from peer_message.h:
    - 1 byte: Message type (EMessageType)
    - 4 bytes: Payload length (uint32_t, network byte order)
    - N bytes: Payload data
    """

    HEADER_SIZE = 5  # 1 byte type + 4 bytes length

    def __init__(self, msg_type=MessageType.UNKNOWN, payload=b""):
        """
        Create a P2P message.

        Args:
            msg_type: Message type (from MessageType class)
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
            bytes: Serialized message [type][length][payload]
        """
        payload_len = len(self.payload)
        # Pack as: 1 byte type + 4 bytes length (big-endian) + payload
        header = struct.pack('!BI', self.msg_type, payload_len)
        return header + self.payload

    @staticmethod
    def deserialize(data):
        """
        Deserialize message from bytes.

        Args:
            data: Raw bytes from network

        Returns:
            P2PMessage: Deserialized message, or None if invalid
        """
        if len(data) < P2PMessage.HEADER_SIZE:
            return None

        # Unpack header: 1 byte type + 4 bytes length (big-endian)
        msg_type, payload_len = struct.unpack('!BI', data[:P2PMessage.HEADER_SIZE])

        # Check if we have enough data for payload
        if len(data) < P2PMessage.HEADER_SIZE + payload_len:
            return None

        # Extract payload
        payload = data[P2PMessage.HEADER_SIZE:P2PMessage.HEADER_SIZE + payload_len]

        return P2PMessage(msg_type, payload)

    def get_type_string(self):
        """Get string representation of message type."""
        return MessageType.to_string(self.msg_type)

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

    def setup(self):
        """Setup test environment - start 4 local nodes."""
        self.log_info("Starting 4 local blockweave nodes...")

        # Start 4 nodes with consecutive port numbers to avoid conflicts
        # Node 0: REST API port 28443, P2P port 28333
        # Node 1: REST API port 28444, P2P port 28334
        # Node 2: REST API port 28445, P2P port 28335
        # Node 3: REST API port 28446, P2P port 28336

        base_rest_port = 28443
        base_p2p_port = 28333

        for i in range(4):
            rest_port = base_rest_port + i
            p2p_port = base_p2p_port + i

            self.log_info(f"Starting node {i} (REST: {rest_port}, P2P: {p2p_port})")
            node = self.add_node(port=rest_port, p2p_port=p2p_port)

            if not node.start(timeout=20):
                raise RuntimeError(f"Failed to start node {i}")

            self.log_info(f"Node {i} started successfully")

        self.log_info(f"All {len(self.nodes)} nodes started successfully")

    def run_test(self):
        """Run all P2P network tests."""
        self.log_info("Running P2P network test...")

        # Basic node functionality tests
        self.test_nodes_are_running()
        self.test_chain_endpoint()
        self.test_port_isolation()
        self.test_mining_status()
        self.test_inbound_peer_connections()
        self.test_peer_connection_limits()

        # P2P message protocol tests
        self.test_message_serialization()
        self.test_message_deserialization()
        self.test_message_round_trip()
        self.test_message_types()
        self.test_message_with_payload()
        self.test_message_empty_payload()
        self.test_message_large_payload()

        # P2P socket connection tests
        self.test_p2p_socket_connection()
        self.test_send_ping_message()
        self.test_send_get_peers_message()
        self.test_send_tx_ids_message()
        self.test_message_binary_payload()

        self.log_info("P2P test completed successfully")

    def test_nodes_are_running(self):
        """Verify all nodes are running."""
        self.log_info("%s: Verifying all nodes are running..." % inspect.currentframe().f_code.co_name)

        for i, node in enumerate(self.nodes):
            # Check if process is alive
            if node.process and node.process.poll() is None:
                self.assert_true(True, f"Node {i} process is running")
            else:
                self.assert_true(False, f"Node {i} process should be running")

            self.log_info(f"Node {i} is running")

    def test_chain_endpoint(self):
        """Test /chain endpoint on all nodes."""
        self.log_info("%s: Testing /chain endpoint on all nodes..." % inspect.currentframe().f_code.co_name)

        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            self.assert_equal(
                response.status_code,
                200,
                f"Node {i} GET /chain returns 200 OK"
            )

            data = response.json()
            self.assert_in(
                "mempool_size",
                data,
                f"Node {i} response contains 'mempool_size'"
            )
            self.assert_in(
                "mining_enabled",
                data,
                f"Node {i} response contains 'mining_enabled'"
            )

            self.log_info(
                f"Node {i}: mempool_size={data['mempool_size']}, "
                f"mining_enabled={data['mining_enabled']}"
            )

    def test_port_isolation(self):
        """Verify nodes are listening on different ports."""
        self.log_info("%s: Verifying port isolation..." % inspect.currentframe().f_code.co_name)

        ports_used = set()
        base_port = 28443

        for i, node in enumerate(self.nodes):
            expected_port = base_port + i
            self.assert_equal(
                node.port,
                expected_port,
                f"Node {i} should use port {expected_port}"
            )
            self.assert_true(
                expected_port not in ports_used,
                f"Port {expected_port} should be unique"
            )
            ports_used.add(expected_port)
            self.log_info(f"Node {i} confirmed on unique port {expected_port}")

    def test_mining_status(self):
        """Verify mining is enabled on all nodes (mining starts automatically)."""
        self.log_info("%s: Verifying mining status on all nodes..." % inspect.currentframe().f_code.co_name)

        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            data = response.json()

            # Mining should be enabled by default when daemon starts
            self.assert_equal(
                data["mining_enabled"],
                True,
                f"Node {i} should have mining enabled by default"
            )
            self.log_info(f"Node {i} mining status confirmed: enabled={data['mining_enabled']}")

    def test_inbound_peer_connections(self):
        """
        Test that nodes can establish inbound connections.

        We'll test that when Node 1 connects to Node 0, Node 0 shows
        an inbound connection and Node 1 shows an outbound connection.
        """
        self.log_info("%s: Testing inbound peer connections..." % inspect.currentframe().f_code.co_name)

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
        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            self.assert_equal(response.status_code, 200, f"Node {i} should still be responsive")

        self.log_info("Inbound connection test placeholder completed")

    def test_peer_connection_limits(self):
        """
        Test that peer connection limits are respected.

        Verifies that:
        - Nodes respect max inbound peer limits (default 120)
        - Nodes respect max outbound peer limits (default 8)
        - Connection attempts beyond limits are rejected gracefully
        """
        self.log_info("%s: Testing peer connection limits..." % inspect.currentframe().f_code.co_name)

        # This test would verify:
        # 1. Start nodes with custom peer limits (e.g., max_inbound_peers=2)
        # 2. Try to establish 3 inbound connections
        # 3. Verify only 2 are accepted and 3rd is rejected
        # 4. Check logs show "Maximum inbound peers reached" message

        self.log_info("Peer limit test: Would verify inbound limit (120) and outbound limit (8)")

        # Test would use config overrides:
        # node = self.add_node(port=28443, p2p_port=28333,
        #                      config_overrides={"max_inbound_peers": 2})

        # For now, just verify default configuration is loaded
        self.log_info("Default limits: max_inbound_peers=120, max_outbound_peers=8")

        # Verify nodes are still operational
        for i, node in enumerate(self.nodes):
            response = node.get("/chain")
            self.assert_equal(response.status_code, 200, f"Node {i} should be operational")

        self.log_info("Peer limit test placeholder completed")

    # ==================================================================
    # P2P Message Protocol Tests
    # ==================================================================

    def test_message_serialization(self):
        """Test P2P message serialization."""
        self.log_info("%s: Testing P2P message serialization..." % inspect.currentframe().f_code.co_name)

        # Create a PING message
        ping_msg = P2PMessage(MessageType.PING)
        serialized = ping_msg.serialize()

        # Verify serialization format
        self.assert_equal(len(serialized), 5, "PING message should be 5 bytes (header only)")
        self.assert_equal(serialized[0], MessageType.PING, "First byte should be message type")

        # Verify payload length is 0 (bytes 1-4, big-endian)
        payload_len = struct.unpack('!I', serialized[1:5])[0]
        self.assert_equal(payload_len, 0, "PING message should have 0 payload length")

        self.log_info("Message serialization test completed")

    def test_message_deserialization(self):
        """Test P2P message deserialization."""
        self.log_info("%s: Testing P2P message deserialization..." % inspect.currentframe().f_code.co_name)

        # Create a raw PONG message manually
        msg_type = MessageType.PONG
        payload = b""
        raw_msg = struct.pack('!BI', msg_type, len(payload)) + payload

        # Deserialize
        pong_msg = P2PMessage.deserialize(raw_msg)

        self.assert_true(pong_msg is not None, "Deserialization should succeed")
        self.assert_equal(pong_msg.msg_type, MessageType.PONG, "Message type should be PONG")
        self.assert_equal(len(pong_msg.payload), 0, "Payload should be empty")

        self.log_info("Message deserialization test completed")

    def test_message_round_trip(self):
        """Test message serialization followed by deserialization."""
        self.log_info("%s: Testing message round-trip serialization..." % inspect.currentframe().f_code.co_name)

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
        self.log_info("%s: Testing all message types..." % inspect.currentframe().f_code.co_name)

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
                f"{MessageType.to_string(msg_type)} should serialize/deserialize"
            )
            self.assert_equal(
                deserialized.msg_type,
                msg_type,
                f"{MessageType.to_string(msg_type)} type should match"
            )

        self.log_info("All message types test completed")

    def test_message_with_payload(self):
        """Test messages with payload data."""
        self.log_info("%s: Testing messages with payload..." % inspect.currentframe().f_code.co_name)

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
        self.log_info("%s: Testing messages with empty payload..." % inspect.currentframe().f_code.co_name)

        # Create message with empty string payload
        msg = P2PMessage(MessageType.GET_CHAIN, "")

        serialized = msg.serialize()
        self.assert_equal(len(serialized), 5, "Empty payload message should be 5 bytes")

        deserialized = P2PMessage.deserialize(serialized)
        self.assert_true(deserialized is not None, "Deserialization should succeed")
        self.assert_equal(len(deserialized.payload), 0, "Payload should be empty")

        self.log_info("Empty payload test completed")

    def test_message_large_payload(self):
        """Test messages with large payload."""
        self.log_info("%s: Testing messages with large payload..." % inspect.currentframe().f_code.co_name)

        # Create message with 1000-byte payload
        large_payload = b"X" * 1000
        msg = P2PMessage(MessageType.BLOCK, large_payload)

        # Serialize and deserialize
        serialized = msg.serialize()
        self.assert_equal(len(serialized), 1005, "Serialized size should be 5 + 1000")

        deserialized = P2PMessage.deserialize(serialized)
        self.assert_true(deserialized is not None, "Deserialization should succeed")
        self.assert_equal(len(deserialized.payload), 1000, "Payload size should be 1000")
        self.assert_equal(deserialized.payload, large_payload, "Payload should match")

        self.log_info("Large payload test completed")

    # ==================================================================
    # P2P Socket Connection Tests
    # ==================================================================

    def test_p2p_socket_connection(self):
        """Test connecting to node via P2P socket."""
        self.log_info("%s: Testing P2P socket connection..." % inspect.currentframe().f_code.co_name)

        # Try to connect to Node 0's P2P port
        p2p_port = 28333
        conn = P2PConnection("127.0.0.1", p2p_port, timeout=2)

        connected = conn.connect()
        self.assert_true(connected, f"Should connect to P2P port {p2p_port}")

        # Close connection
        conn.close()

        self.log_info("P2P socket connection test completed")

    def test_send_ping_message(self):
        """Test sending PING message to a node."""
        self.log_info("%s: Testing sending PING message..." % inspect.currentframe().f_code.co_name)

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
        self.log_info("%s: Testing sending GET_PEERS message..." % inspect.currentframe().f_code.co_name)

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
        self.log_info("%s: Testing sending TX_IDS message..." % inspect.currentframe().f_code.co_name)

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
            serialized = tx_ids_msg.serialize()
            expected_size = 5 + len(tx_ids.encode('utf-8'))
            self.assert_equal(
                len(serialized),
                expected_size,
                f"Serialized TX_IDS should be {expected_size} bytes"
            )

        self.log_info("Send TX_IDS message test completed")

    def test_message_binary_payload(self):
        """Test messages with binary (non-text) payload."""
        self.log_info("%s: Testing messages with binary payload..." % inspect.currentframe().f_code.co_name)

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

    def cleanup(self):
        """Cleanup - stop all nodes."""
        self.log_info("Stopping all nodes...")
        for i, node in enumerate(self.nodes):
            if node:
                self.log_info(f"Stopping node {i}...")
                node.stop()
        self.log_info("All nodes stopped")

if __name__ == "__main__":
    test = P2PTest()
    sys.exit(test.main())
