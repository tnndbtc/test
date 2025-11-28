#!/usr/bin/env python3
"""
Block utility functions for Blockweave functional tests.

Provides helper functions to create and serialize blocks for testing purposes.
"""

import struct
import hashlib
import time
import random


# Block serialization constants (from src/utils/settings.h)
BLOCK_HASH_SIZE = 32
BLOCK_INT64_SIZE = 8
BLOCK_UINT64_SIZE = 8
BLOCK_UINT32_SIZE = 4


class BlockUtils:
    """Utility class for creating and manipulating blocks in tests."""

    @staticmethod
    def hash_string(data):
        """
        Create a SHA-256 hash from a string.

        Args:
            data: String to hash

        Returns:
            bytes: 32-byte hash
        """
        return hashlib.sha256(data.encode('utf-8')).digest()

    @staticmethod
    def create_zero_hash():
        """
        Create a zero hash (32 bytes of 0x00).

        Returns:
            bytes: 32-byte zero hash
        """
        return b'\x00' * BLOCK_HASH_SIZE

    @staticmethod
    def mine_block(prev_hash_str, height, timestamp, transactions_str=""):
        """
        Mine a block by finding a nonce that produces a valid hash.

        The mining algorithm (from block.cpp:90-109):
        1. Construct block_data = prev_hash + height + timestamp + tx_ids
        2. Try random nonces until hash(block_data + nonce) starts with "0fff" or less

        Args:
            prev_hash_str: Previous block hash as hex string
            height: Block height (int)
            timestamp: Block timestamp (int64 nanoseconds)
            transactions_str: Concatenated transaction IDs (default: "")

        Returns:
            tuple: (nonce, block_hash_hex) where nonce is uint32 and block_hash_hex is the mined hash
        """
        # Construct block data string (matches block.cpp:91-96)
        block_data = prev_hash_str + str(height) + str(timestamp) + transactions_str

        # Try random nonces (matches block.cpp:98-109)
        max_attempts = 10000000  # Safety limit
        for attempt in range(max_attempts):
            nonce = random.randint(0, 0xFFFFFFFF)  # uint32 range
            candidate_str = block_data + str(nonce)

            # Hash the candidate
            hash_bytes = hashlib.sha256(candidate_str.encode('utf-8')).digest()
            hash_hex = hash_bytes.hex()

            # Check if hash meets difficulty (first 4 hex chars < "0fff")
            if hash_hex[:4] < "0fff":
                return (nonce, hash_hex)

        raise RuntimeError(f"Failed to mine block after {max_attempts} attempts")

    @staticmethod
    def create_block(prev_hash_hex, height, miner, timestamp=None, transactions=None, mine=True):
        """
        Create a legitimate block with proper structure and optionally mine it.

        Args:
            prev_hash_hex: Previous block hash as hex string (64 chars)
            height: Block height (int)
            miner: Miner address (string)
            timestamp: Block timestamp in nanoseconds (default: current time)
            transactions: List of transaction objects (default: [])
            mine: Whether to mine the block (find valid nonce) (default: True)

        Returns:
            dict: Block data with fields matching CBlock structure
                - hash_hex: Block hash (hex string)
                - prev_hash_hex: Previous block hash (hex string)
                - height: Block height
                - timestamp: Block timestamp (nanoseconds)
                - miner: Miner address
                - difficulty: Mining difficulty (default: 1000)
                - cumulative_data_size: Total transaction data size
                - nonce: Proof-of-work nonce
                - transactions: List of transactions
        """
        if timestamp is None:
            # Use current time in nanoseconds (matches block.cpp:50)
            timestamp = int(time.time() * 1e9)

        if transactions is None:
            transactions = []

        # Calculate cumulative data size from transaction data
        cumulative_data_size = 0
        for tx in transactions:
            data = tx.get('data', b'')
            if isinstance(data, bytes):
                cumulative_data_size += len(data)
            else:
                cumulative_data_size += len(data.encode('utf-8'))

        # Build transaction IDs string for mining
        # Transaction ID is computed as hash of owner + target + data + reward + timestamp + type
        tx_ids_str = ""
        for tx in transactions:
            tx_id_input = tx['owner'] + tx['target']
            data = tx.get('data', b'')
            if isinstance(data, bytes):
                tx_id_input += data.decode('utf-8', errors='ignore')
            else:
                tx_id_input += data
            tx_id_input += str(tx['reward'])
            tx_id_input += str(tx['timestamp'])
            tx_id_input += str(tx['type'])

            # Compute hash (matching C++ implementation)
            tx_id_hash = hashlib.sha256(tx_id_input.encode('utf-8')).digest()
            tx_ids_str += tx_id_hash.hex()

        block = {
            'prev_hash_hex': prev_hash_hex,
            'height': height,
            'timestamp': timestamp,
            'miner': miner,
            'difficulty': 1000,  # Default difficulty (from block.cpp:49)
            'cumulative_data_size': cumulative_data_size,
            'transactions': transactions
        }

        if mine:
            # Mine the block to find valid nonce and hash
            nonce, hash_hex = BlockUtils.mine_block(
                prev_hash_hex,
                height,
                timestamp,
                tx_ids_str
            )
            block['nonce'] = nonce
            block['hash_hex'] = hash_hex
        else:
            # No mining - use dummy values
            block['nonce'] = 0
            block['hash_hex'] = '0' * 64

        return block

    @staticmethod
    def serialize_transaction(tx):
        """
        Serialize a transaction to binary format matching C++ implementation.

        Format (from transaction.cpp:115-171):
        [owner_len:4][owner][target_len:4][target][data_len:4][data]
        [reward:8][timestamp:8][type:1][metadata_len:4][metadata]

        Args:
            tx: Transaction dict with keys: owner, target, data, reward, timestamp, type, metadata

        Returns:
            bytes: Serialized transaction data
        """
        result = b''

        # Owner (length + data)
        owner_bytes = tx['owner'].encode('utf-8')
        result += struct.pack('!I', len(owner_bytes))
        result += owner_bytes

        # Target (length + data)
        target_bytes = tx['target'].encode('utf-8')
        result += struct.pack('!I', len(target_bytes))
        result += target_bytes

        # Data (length + data)
        data_bytes = tx['data'] if isinstance(tx['data'], bytes) else tx['data'].encode('utf-8')
        result += struct.pack('!I', len(data_bytes))
        result += data_bytes

        # Reward (8 bytes, network byte order)
        result += struct.pack('!Q', tx['reward'])

        # Timestamp (8 bytes, network byte order)
        result += struct.pack('!Q', tx['timestamp'])

        # Type (1 byte)
        result += struct.pack('!B', tx['type'])

        # Metadata (length + data)
        metadata_bytes = tx['metadata'].encode('utf-8')
        result += struct.pack('!I', len(metadata_bytes))
        result += metadata_bytes

        return result

    @staticmethod
    def serialize_block(block):
        """
        Serialize a block to binary format for network transmission.

        Format (from block.cpp:126-175):
        - 32 bytes: Block hash (binary)
        - 32 bytes: Previous block hash (binary)
        - 8 bytes: Height (int64, network byte order)
        - 8 bytes: Timestamp (int64, network byte order)
        - 4 bytes: Miner string length (uint32, network byte order)
        - N bytes: Miner string (UTF-8)
        - 8 bytes: Difficulty (uint64, network byte order)
        - 8 bytes: Cumulative data size (uint64, network byte order)
        - 4 bytes: Nonce (uint32, network byte order)
        - 4 bytes: Transaction count (uint32, network byte order)
        - For each transaction:
          - 4 bytes: Transaction length (uint32, network byte order)
          - M bytes: Serialized transaction data

        Args:
            block: Block dict from create_block()

        Returns:
            bytes: Serialized block data
        """
        result = b''

        # Hash (32 bytes binary)
        hash_bytes = bytes.fromhex(block['hash_hex'])
        result += hash_bytes

        # Previous block hash (32 bytes binary)
        prev_hash_bytes = bytes.fromhex(block['prev_hash_hex'])
        result += prev_hash_bytes

        # Height (8 bytes, network byte order = big-endian)
        result += struct.pack('!Q', block['height'])

        # Timestamp (8 bytes, network byte order)
        result += struct.pack('!Q', block['timestamp'])

        # Miner (length + data)
        miner_bytes = block['miner'].encode('utf-8')
        result += struct.pack('!I', len(miner_bytes))
        result += miner_bytes

        # Difficulty (8 bytes, network byte order)
        result += struct.pack('!Q', block['difficulty'])

        # Cumulative data size (8 bytes, network byte order)
        result += struct.pack('!Q', block['cumulative_data_size'])

        # Nonce (4 bytes, network byte order)
        result += struct.pack('!I', block['nonce'])

        # Transaction count
        transactions = block.get('transactions', [])
        result += struct.pack('!I', len(transactions))

        # Each transaction
        for tx in transactions:
            tx_data = BlockUtils.serialize_transaction(tx)
            result += struct.pack('!I', len(tx_data))
            result += tx_data

        return result

    @staticmethod
    def create_genesis_block():
        """
        Create the genesis block (matches CreateGenesisBlock in block.cpp:53-68).

        Returns:
            dict: Genesis block data
        """
        zero_hash_hex = '0' * 64
        genesis = {
            'hash_hex': 'c62c5e7a89e9a07c1f9ec8e3c0c43fcbca7b8f9e2d6b4a5c8e9f0a1b2c3d4e5f',  # Placeholder
            'prev_hash_hex': zero_hash_hex,
            'height': 0,
            'timestamp': 1762553229520435,  # From block.cpp:61
            'miner': 'genesis',
            'difficulty': 1000,
            'cumulative_data_size': 0,
            'nonce': 1106191456,  # From block.cpp:62
            'transactions': []
        }

        # Calculate actual hash (from block.cpp:63-65)
        block_data = zero_hash_hex + "0" + str(genesis['timestamp'])
        hash_str = block_data + str(genesis['nonce'])
        hash_bytes = hashlib.sha256(hash_str.encode('utf-8')).digest()
        genesis['hash_hex'] = hash_bytes.hex()

        return genesis
