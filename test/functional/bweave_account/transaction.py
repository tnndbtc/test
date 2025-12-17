"""
Blockweave Transaction with Ethereum-compatible hashing

This module provides the Transaction class for creating, signing,
and serializing transactions with Keccak256 + RLP encoding.
"""

from dataclasses import dataclass
from typing import Optional
from enum import IntEnum
import struct
from .crypto import keccak256
from .encoding import rlp_encode_transaction
from .exceptions import TransactionError


class TxVersion(IntEnum):
    """Transaction version enum"""
    V0 = 0


class TransactionType(IntEnum):
    """Transaction type enum"""
    TRANSFER = 0


@dataclass
class Transaction:
    """
    Blockweave transaction with Ethereum-compatible hashing

    Fields match C++ CTransaction structure:
    - m_n_version (uint8)
    - m_n_nonce (uint64)
    - m_from (20 bytes)
    - m_type (uint8)
    - m_data (variable bytes)
    - m_n_fee (uint64)
    - m_signature (65 bytes when signed)

    Example:
        >>> tx = Transaction(
        ...     version=TxVersion.V0,
        ...     nonce=1,
        ...     from_address=b'\\x12'*20,
        ...     tx_type=TransactionType.TRANSFER,
        ...     data=b"Hello!",
        ...     fee=1000
        ... )
        >>> signing_hash = tx.generate_signing_hash()
        >>> # Sign with account, attach signature, compute ID, serialize
    """

    version: int          # uint8 (TxVersion.V0)
    nonce: int            # uint64
    from_address: bytes   # 20 bytes
    tx_type: int          # uint8 (TransactionType.TRANSFER)
    data: bytes           # variable
    fee: int              # uint64
    signature: Optional[bytes] = None  # 65 bytes when signed

    def __post_init__(self):
        """
        Validate field constraints

        Raises:
            ValueError: If any field has invalid value
        """
        if len(self.from_address) != 20:
            raise ValueError(
                f"from_address must be exactly 20 bytes, got {len(self.from_address)}"
            )
        if not (0 <= self.version <= 255):
            raise ValueError(f"version must be uint8 (0-255), got {self.version}")
        if not (0 <= self.nonce < 2**64):
            raise ValueError(f"nonce must be uint64 (0 to 2^64-1), got {self.nonce}")
        if not (0 <= self.tx_type <= 255):
            raise ValueError(f"tx_type must be uint8 (0-255), got {self.tx_type}")
        if not (0 <= self.fee < 2**64):
            raise ValueError(f"fee must be uint64 (0 to 2^64-1), got {self.fee}")

        # Signature validation (if provided)
        if self.signature is not None:
            if len(self.signature) != 65:
                raise ValueError(
                    f"signature must be 65 bytes (r||s||v), got {len(self.signature)}"
                )

    def generate_signing_hash(self) -> bytes:
        """
        Generate signing hash: keccak256(rlp_encode(tx_without_signature))

        Returns:
            32-byte hash ready for signing

        Example:
            >>> tx = Transaction(version=0, nonce=1, ...)
            >>> signing_hash = tx.generate_signing_hash()
            >>> len(signing_hash)
            32
            >>> # Pass to account.sign_hash(signing_hash)

        Note:
            This hash is computed WITHOUT the signature field to avoid
            circular dependency. Only the 6 core transaction fields are included.
        """
        # RLP encode transaction fields WITHOUT signature
        tx_data = [
            self.version,
            self.nonce,
            self.from_address,
            self.tx_type,
            self.data,
            self.fee
        ]

        encoded = rlp_encode_transaction(tx_data)
        return keccak256(encoded)

    def attach_signature(self, r: bytes, s: bytes, v: int) -> None:
        """
        Attach signature to transaction

        Args:
            r: 32-byte signature component
            s: 32-byte signature component
            v: Recovery id (0 or 1)

        Raises:
            ValueError: If signature components have wrong length

        Example:
            >>> r, s, v = account.sign_hash(signing_hash)
            >>> tx.attach_signature(r, s, v)
            >>> len(tx.signature)
            65
        """
        if len(r) != 32:
            raise ValueError(f"r must be 32 bytes, got {len(r)}")
        if len(s) != 32:
            raise ValueError(f"s must be 32 bytes, got {len(s)}")
        if v not in (0, 1):
            raise ValueError(f"v must be 0 or 1, got {v}")

        # Combine into 65-byte signature: r || s || v
        self.signature = r + s + bytes([v])

    def compute_transaction_id(self) -> bytes:
        """
        Compute transaction ID: keccak256(rlp_encode(full_transaction))

        Must be called AFTER attach_signature()

        Returns:
            32-byte transaction ID

        Raises:
            TransactionError: If transaction not signed yet

        Example:
            >>> tx.attach_signature(r, s, v)
            >>> tx_id = tx.compute_transaction_id()
            >>> len(tx_id)
            32

        Note:
            The transaction ID includes the signature, so it can only be
            computed after the transaction has been signed.
        """
        if self.signature is None:
            raise TransactionError(
                "Cannot compute ID of unsigned transaction. "
                "Call attach_signature() first."
            )

        # Use C++-compatible ID computation: SHA256(concatenation)
        # This matches the C++ implementation in transaction.cpp:220-229
        id_input = b''
        id_input += str(self.version).encode('utf-8')
        id_input += str(self.nonce).encode('utf-8')
        id_input += self.from_address  # raw bytes
        id_input += str(self.tx_type).encode('utf-8')
        id_input += self.data  # raw bytes
        id_input += str(self.fee).encode('utf-8')
        id_input += self.signature  # raw bytes

        # Use SHA256 (not Keccak256) to match C++ implementation
        import hashlib
        return hashlib.sha256(id_input).digest()

    def serialize(self) -> bytes:
        """
        Serialize transaction to bytes (C++ compatible format)

        Format matches C++ CTransaction::Serialize():
        [version:1][nonce:8][from:20][type:1][data_len:4][data]
        [fee:8][signature_len:4][signature]

        Returns:
            Serialized transaction bytes

        Raises:
            TransactionError: If transaction not signed yet

        Example:
            >>> tx.attach_signature(r, s, v)
            >>> serialized = tx.serialize()
            >>> # Can now send over network or deserialize later

        Note:
            - Network byte order (big-endian) for multi-byte integers
            - Uses struct.pack('>Q') for uint64, struct.pack('>I') for uint32
            - Variable-length fields prefixed with their byte count
        """
        if self.signature is None:
            raise TransactionError(
                "Cannot serialize unsigned transaction. "
                "Call attach_signature() first."
            )

        result = b''

        # version (1 byte)
        result += struct.pack('B', self.version)

        # nonce (8 bytes, network byte order)
        result += struct.pack('>Q', self.nonce)

        # from address (20 bytes)
        result += self.from_address

        # tx_type (1 byte)
        result += struct.pack('B', self.tx_type)

        # data (length + data)
        result += struct.pack('>I', len(self.data))
        result += self.data

        # fee (8 bytes, network byte order)
        result += struct.pack('>Q', self.fee)

        # signature (length + data)
        result += struct.pack('>I', len(self.signature))
        result += self.signature

        # metadata (length + data) - empty string if not set
        metadata = getattr(self, 'metadata', '')
        metadata_bytes = metadata.encode('utf-8') if isinstance(metadata, str) else metadata
        result += struct.pack('>I', len(metadata_bytes))
        result += metadata_bytes

        return result

    @classmethod
    def deserialize(cls, data: bytes) -> 'Transaction':
        """
        Deserialize transaction from bytes (C++ compatible)

        Args:
            data: Serialized transaction bytes

        Returns:
            Transaction instance

        Raises:
            TransactionError: If deserialization fails

        Example:
            >>> serialized = tx.serialize()
            >>> tx2 = Transaction.deserialize(serialized)
            >>> tx2.nonce == tx.nonce
            True
            >>> tx2.data == tx.data
            True
        """
        try:
            offset = 0

            # version (1 byte)
            version = struct.unpack_from('B', data, offset)[0]
            offset += 1

            # nonce (8 bytes)
            nonce = struct.unpack_from('>Q', data, offset)[0]
            offset += 8

            # from address (20 bytes)
            from_address = data[offset:offset+20]
            offset += 20

            # tx_type (1 byte)
            tx_type = struct.unpack_from('B', data, offset)[0]
            offset += 1

            # data (length + data)
            data_len = struct.unpack_from('>I', data, offset)[0]
            offset += 4
            tx_data = data[offset:offset+data_len]
            offset += data_len

            # fee (8 bytes)
            fee = struct.unpack_from('>Q', data, offset)[0]
            offset += 8

            # signature (length + data)
            sig_len = struct.unpack_from('>I', data, offset)[0]
            offset += 4
            signature = data[offset:offset+sig_len] if sig_len > 0 else None

            return cls(
                version=version,
                nonce=nonce,
                from_address=from_address,
                tx_type=tx_type,
                data=tx_data,
                fee=fee,
                signature=signature
            )

        except struct.error as e:
            raise TransactionError(f"Deserialization failed (invalid format): {e}")
        except IndexError as e:
            raise TransactionError(f"Deserialization failed (truncated data): {e}")
        except Exception as e:
            raise TransactionError(f"Deserialization failed: {e}")

    def to_dict(self) -> dict:
        """
        Convert transaction to dictionary for JSON serialization

        Returns:
            Dictionary representation

        Example:
            >>> import json
            >>> tx_dict = tx.to_dict()
            >>> json.dumps(tx_dict, indent=2)
        """
        return {
            'version': self.version,
            'nonce': self.nonce,
            'from': self.from_address.hex(),
            'type': self.tx_type,
            'data': self.data.hex(),
            'fee': self.fee,
            'signature': self.signature.hex() if self.signature else None
        }

    @classmethod
    def from_dict(cls, data: dict) -> 'Transaction':
        """
        Create transaction from dictionary

        Args:
            data: Dictionary with transaction fields

        Returns:
            Transaction instance

        Example:
            >>> tx_dict = {...}
            >>> tx = Transaction.from_dict(tx_dict)
        """
        signature = None
        if data.get('signature'):
            signature = bytes.fromhex(data['signature'])

        return cls(
            version=data['version'],
            nonce=data['nonce'],
            from_address=bytes.fromhex(data['from']),
            tx_type=data['type'],
            data=bytes.fromhex(data['data']),
            fee=data['fee'],
            signature=signature
        )
