"""
Bweave Python Utilities

Ethereum-compatible transaction signing and keystore management.

This package provides utilities for:
- Loading Web3 v3 keystores (compatible with C++ wallet)
- Creating and signing transactions with Keccak256 + RLP
- secp256k1 ECDSA signature generation
- Transaction serialization for network transmission

Example usage:
    >>> from bweave_account import BweaveAccount, Transaction, TxVersion, TransactionType
    >>>
    >>> # Load account from keystore
    >>> account = BweaveAccount.from_keystore("wallet.json", "password")
    >>>
    >>> # Create transaction
    >>> tx = Transaction(
    ...     version=TxVersion.V0,
    ...     nonce=1,
    ...     from_address=account.address_bytes,
    ...     tx_type=TransactionType.TRANSFER,
    ...     data=b"Hello!",
    ...     fee=1000
    ... )
    >>>
    >>> # Sign transaction
    >>> signing_hash = tx.generate_signing_hash()
    >>> r, s, v = account.sign_hash(signing_hash)
    >>> tx.attach_signature(r, s, v)
    >>>
    >>> # Serialize for network
    >>> serialized = tx.serialize()
"""

from .bweave_account import BweaveAccount
from .transaction import Transaction, TxVersion, TransactionType
from .crypto import keccak256, sign, verify_signature
from .encoding import rlp_encode, rlp_encode_transaction, rlp_decode
from .exceptions import (
    BweaveError,
    KeystoreError,
    SignatureError,
    TransactionError,
    EncodingError
)

__version__ = "0.1.0"

__all__ = [
    # Main classes
    'BweaveAccount',
    'Transaction',

    # Enums
    'TxVersion',
    'TransactionType',

    # Crypto functions
    'keccak256',
    'sign',
    'verify_signature',

    # Encoding functions
    'rlp_encode',
    'rlp_encode_transaction',
    'rlp_decode',

    # Exceptions
    'BweaveError',
    'KeystoreError',
    'SignatureError',
    'TransactionError',
    'EncodingError',
]
