#!/usr/bin/env python3
"""
P2P utility functions and classes for Blockweave functional tests.

Provides helper classes for P2P message serialization and connection management.
Shared by test_p2p_message.py, test_p2p_orphan_blocks.py, and other P2P tests.
"""

import socket
import struct
import time
import random
import logging

# Create module-level logger
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


def create_version_payload(peer_port, our_address="127.0.0.1"):
    """
    Create VERSION message payload for P2P handshake.

    This is a standalone helper function that can be used by any test
    to create a VERSION message payload without needing a P2PConnection instance.

    VERSION payload format (76 bytes total):
    - protocol_version: 4 bytes (int32_t, big-endian)
    - timestamp: 8 bytes (int64_t, big-endian, UNIX time)
    - address_recv: 26 bytes (CNetworkAddress - peer we're connecting to)
      - services: 8 bytes (uint64_t, big-endian) - set to 0
      - ipv6: 16 bytes (IPv4-mapped: 10 bytes 0x00, 2 bytes 0xFF, 4 bytes IPv4)
      - port: 2 bytes (uint16_t, big-endian)
    - address_from: 26 bytes (CNetworkAddress - our address)
      - services: 8 bytes (uint64_t, big-endian) - NODE_NETWORK = 1
      - ipv6: 16 bytes (IPv4-mapped: our_address)
      - port: 2 bytes (uint16_t, big-endian) - set to 0
    - nonce: 8 bytes (uint64_t, big-endian) - random value
    - chain_tip_height: 4 bytes (int32_t, big-endian) - set to 0

    Args:
        peer_port: Port number of the peer we're connecting to
        our_address: Our IP address (default: "127.0.0.1")

    Returns:
        bytes: VERSION message payload (76 bytes)
    """
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
    payload += struct.pack('!H', peer_port)

    # address_from (26 bytes) - our address
    # services: NODE_NETWORK = 1
    NODE_NETWORK = 1
    payload += struct.pack('!Q', NODE_NETWORK)
    # IPv6 address (16 bytes) - IPv4-mapped for our_address
    payload += ipv6_bytes
    # port (2 bytes) - set to 0
    payload += struct.pack('!H', 0)

    # nonce (8 bytes) - random value
    nonce = random.randint(0, 2**64 - 1)
    payload += struct.pack('!Q', nonce)

    # chain_tip_height (4 bytes) - set to 0 (we don't know)
    payload += struct.pack('!i', 0)

    return payload


def perform_version_verack_handshake(connection):
    """
    Perform VERSION/VERACK handshake on an established connection.

    This is a standalone helper function that can be used to perform
    the handshake on any connection object that has send_message() and
    receive_message() methods.

    Handshake sequence:
    1. Send VERSION to peer
    2. Receive VERSION from peer
    3. Send VERACK to peer
    4. Receive VERACK from peer

    Args:
        connection: Connection object with the following methods:
            - send_message(message) -> bool
            - receive_message(timeout=N) -> P2PMessage or None
            And the following attributes:
            - port: Peer port number

    Returns:
        bool: True if handshake successful, False otherwise

    Example:
        conn = P2PConnection("127.0.0.1", 48333)
        # Connect socket manually
        conn.socket = socket.socket(...)
        conn.socket.connect((conn.host, conn.port))
        # Perform handshake
        if perform_version_verack_handshake(conn):
            # Ready to send/receive messages
            pass
    """
    try:
        # Step 1: Send VERSION
        version_payload = create_version_payload(connection.port)
        version_msg = P2PMessage(MessageType.VERSION, version_payload)
        if not connection.send_message(version_msg):
            client_port_info = f" (client port: {connection.assigned_client_port})" if hasattr(connection, 'assigned_client_port') and connection.assigned_client_port else ""
            logger.error(f"Handshake failed: Could not send VERSION to {connection.host}:{connection.port} client_port:{client_port_info}")
            return False

        # Step 2: Receive VERSION from peer
        received_version = connection.receive_message(timeout=5)
        if not received_version or received_version.msg_type != MessageType.VERSION:
            client_port_info = f" (client port: {connection.assigned_client_port})" if hasattr(connection, 'assigned_client_port') and connection.assigned_client_port else ""
            logger.error(f"Handshake failed: Expected VERSION, got {received_version.msg_type if received_version else 'None'} from {connection.host}:{connection.port} client_port:{client_port_info}")
            return False

        # Step 3: Send VERACK
        verack_msg = P2PMessage(MessageType.VERACK, b"")
        if not connection.send_message(verack_msg):
            client_port_info = f" (client port: {connection.assigned_client_port})" if hasattr(connection, 'assigned_client_port') and connection.assigned_client_port else ""
            logger.error(f"Handshake failed: Could not send VERACK to {connection.host}:{connection.port} client_port:{client_port_info}")
            return False

        # Step 4: Receive VERACK from peer
        received_verack = connection.receive_message(timeout=5)
        if not received_verack or received_verack.msg_type != MessageType.VERACK:
            client_port_info = f" (client port: {connection.assigned_client_port})" if hasattr(connection, 'assigned_client_port') and connection.assigned_client_port else ""
            logger.error(f"Handshake failed: Expected VERACK, got {received_verack.msg_type if received_verack else 'None'} from {connection.host}:{connection.port} client_port:{client_port_info}")
            return False
        # this sleep is critical on windows to ensure handshake finishes before sending other p2p messages
        time.sleep(1)

        return True

    except Exception as e:
        client_port_info = f" (client port: {connection.assigned_client_port})" if hasattr(connection, 'assigned_client_port') and connection.assigned_client_port else ""
        logger.error(f"Handshake failed with exception to {connection.host}:{connection.port} client_port:{client_port_info}: {e}")
        return False


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
    TX = "tx"
    BLOCK = "block"
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
            MessageType.PEERS, MessageType.TX, MessageType.BLOCK,
            MessageType.GET_CHAIN, MessageType.CHAIN_INFO,
            MessageType.INVENTORY, MessageType.VERSION, MessageType.VERACK,
            MessageType.GETDATA, MessageType.UNKNOWN
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
    - Perform VERSION/VERACK handshake
    - Send P2P messages
    - Receive P2P messages
    - Close connection
    """

    def __init__(self, host, port, timeout=5, client_port=None):
        """
        Create a P2P connection.

        Args:
            host: Hostname or IP address
            port: P2P port number
            timeout: Socket timeout in seconds
            client_port: Optional client-side port to bind to (for port reuse)
        """
        self.host = host
        self.port = port
        self.timeout = timeout
        self.client_port = client_port
        self.socket = None
        self.assigned_client_port = None

    def connect(self, do_handshake=True):
        """
        Connect to the P2P node and optionally perform VERSION/VERACK handshake.

        Args:
            do_handshake: Whether to perform VERSION/VERACK handshake (default: True)
                         Set to False if you want to manually control the handshake

        Raises:
            ConnectionError: If connection to server fails
            RuntimeError: If VERSION/VERACK handshake fails

        Example without handshake:
            conn = P2PConnection("127.0.0.1", 48333)
            conn.connect(do_handshake=False)
            # Manually perform handshake or send custom messages
            if perform_version_verack_handshake(conn):
                # Now ready for communication
                pass
        """
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(self.timeout)

            # Enable address reuse (important for port reuse after TIME_WAIT)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

            # If client_port is specified, bind to it before connecting
            if self.client_port is not None:
                try:
                    self.socket.bind(('', self.client_port))
                except OSError as e:
                    logger.warning(f"Failed to bind to client port {self.client_port}: {e}")
                    # Continue without binding - let OS assign ephemeral port

            self.socket.connect((self.host, self.port))

            # Retrieve the actual client port assigned by OS
            client_addr = self.socket.getsockname()
            self.assigned_client_port = client_addr[1]

            # Optionally perform VERSION/VERACK handshake
            if do_handshake:
                if not perform_version_verack_handshake(self):
                    self.close()
                    raise RuntimeError(f"VERSION/VERACK handshake failed with {self.host}:{self.port}")

        except ConnectionError:
            # Re-raise ConnectionError as-is
            raise
        except RuntimeError:
            # Re-raise RuntimeError (handshake failure) as-is
            raise
        except Exception as e:
            # Wrap other exceptions as ConnectionError
            raise ConnectionError(f"Failed to connect to {self.host}:{self.port}: {e}") from e

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
            client_port_info = f" (client port: {self.assigned_client_port})" if self.assigned_client_port else ""
            logger.error(f"Failed to send message to {self.host}:{self.port} client_port:{client_port_info}: {e}")
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
            client_port_info = f" (client port: {self.assigned_client_port})" if self.assigned_client_port else ""
            logger.error(f"Failed to receive message from {self.host}:{self.port} client_port:{client_port_info}: {e}")
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

    def get_client_port(self):
        """
        Get the client-side port assigned by the OS.

        Returns:
            int: Client port number, or None if not connected yet
        """
        return self.assigned_client_port

    def close(self):
        """Close the connection (preserves assigned_client_port for reuse)."""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            # NOTE: Keep self.assigned_client_port for port reuse

    def __enter__(self):
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()
