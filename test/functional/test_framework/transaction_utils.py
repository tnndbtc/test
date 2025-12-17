#!/usr/bin/env python3
"""
Transaction creation utilities using bweave_account module.
Produces binary serialized transactions for submission to /rpc/transaction.
"""

import os
from pathlib import Path
import sys

# Add test/functional directory to path so we can import bweave_account package
sys.path.insert(0, str(Path(__file__).parent.parent))

from bweave_account import BweaveAccount, Transaction, TxVersion, TransactionType


class TransactionHelper:
    """Helper class for creating signed binary transactions in functional tests."""

    def __init__(self, keystore_path: str, password: str):
        """
        Initialize with keystore credentials.

        Args:
            keystore_path: Path to Web3 v3 keystore JSON file
            password: Keystore decryption password
        """
        self.account = BweaveAccount.from_keystore(keystore_path, password)
        self.nonce_counter = 1  # Auto-incrementing nonce

    def create_transaction(self, data, fee=100, tx_type=TransactionType.TRANSFER, custom_nonce=None):
        """
        Create a properly signed transaction and return serialized bytes.

        Args:
            data: Transaction payload (string or bytes)
            fee: Transaction fee in base units
            tx_type: Transaction type (default: TRANSFER)
            custom_nonce: Optional custom nonce (for multi-process environments)
                         If None, uses auto-incrementing nonce_counter

        Returns:
            tuple: (tx_details: dict, serialized_bytes: bytes)
                - tx_details: Dict with transaction_id, from, nonce, version, etc.
                - serialized_bytes: Binary transaction data ready for /rpc/transaction
        """
        # Convert data to bytes if needed
        if isinstance(data, str):
            data_bytes = data.encode('utf-8')
        else:
            data_bytes = data

        # Use custom nonce if provided, otherwise use auto-incrementing counter
        if custom_nonce is not None:
            nonce = custom_nonce
        else:
            nonce = self.nonce_counter
            self.nonce_counter += 1

        # Create transaction object
        tx = Transaction(
            version=TxVersion.V0,
            nonce=nonce,
            from_address=self.account.address_bytes,  # 20-byte address
            tx_type=tx_type,
            data=data_bytes,
            fee=fee
        )

        # Sign transaction
        signing_hash = tx.generate_signing_hash()
        r, s, v = self.account.sign_hash(signing_hash)
        tx.attach_signature(r, s, v)

        # Compute transaction ID
        tx_id = tx.compute_transaction_id()

        # Serialize to binary
        serialized_bytes = tx.serialize()

        # Return details and binary data
        tx_details = {
            'transaction_id': tx_id.hex(),
            'from': self.account.address,  # Checksummed address (0xd150...)
            'nonce': tx.nonce,
            'version': int(tx.version),
            'type': int(tx.tx_type),
            'data': data_bytes,
            'fee': fee,
            'signature': tx.signature.hex(),
        }

        return tx_details, serialized_bytes
