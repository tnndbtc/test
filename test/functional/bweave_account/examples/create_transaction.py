#!/usr/bin/env python3
"""
Example: Create and sign a transaction using keystore

This script demonstrates the complete workflow for creating and signing
a Blockweave transaction using a Web3 keystore file.

Usage:
    python3 create_transaction.py <keystore_path> <password>

Example:
    python3 create_transaction.py wallet.json mypassword
"""

import sys
import os

#parent_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
#sys.path.insert(0, parent_dir)

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from bweave_account import BweaveAccount, Transaction, TxVersion, TransactionType


def main():
    # Check command line arguments
    if len(sys.argv) < 3:
        print("Usage: python3 create_transaction.py <keystore_path> <password>")
        print()
        print("Example:")
        print("  python3 create_transaction.py wallet.json mypassword")
        sys.exit(1)

    keystore_path = sys.argv[1]
    password = sys.argv[2]

    print("=" * 60)
    print("Blockweave Transaction Creation Example")
    print("=" * 60)
    print()

    # Step 1: Load account from keystore
    print(f"[1] Loading account from keystore: {keystore_path}")
    try:
        account = BweaveAccount.from_keystore(keystore_path, password)
        print(f"    ✓ Account loaded successfully")
        print(f"    Address: {account.address}")
    except Exception as e:
        print(f"    ✗ Error loading keystore: {e}")
        sys.exit(1)
    print()

    # Step 2: Create transaction
    print("[2] Creating transaction")
    tx = Transaction(
        version=TxVersion.V0,
        nonce=1,
        from_address=account.address_bytes,
        tx_type=TransactionType.TRANSFER,
        data=b"Hello, Blockweave! This is a test transaction.",
        fee=1000
    )
    print(f"    ✓ Transaction created")
    print(f"    Version: {tx.version}")
    print(f"    Nonce: {tx.nonce}")
    print(f"    From: 0x{tx.from_address.hex()}")
    print(f"    Type: {tx.tx_type} (TRANSFER)")
    print(f"    Data: {tx.data.decode('utf-8', errors='replace')}")
    print(f"    Fee: {tx.fee}")
    print()

    # Step 3: Generate signing hash
    print("[3] Generating signing hash")
    signing_hash = tx.generate_signing_hash()
    print(f"    ✓ Signing hash generated")
    print(f"    Hash: 0x{signing_hash.hex()}")
    print()

    # Step 4: Sign transaction
    print("[4] Signing transaction")
    r, s, v = account.sign_hash(signing_hash)
    tx.attach_signature(r, s, v)
    print(f"    ✓ Transaction signed")
    print(f"    Signature (r): 0x{r.hex()}")
    print(f"    Signature (s): 0x{s.hex()}")
    print(f"    Signature (v): {v}")
    print(f"    Combined signature: 0x{tx.signature.hex()}")
    print()

    # Step 5: Compute transaction ID
    print("[5] Computing transaction ID")
    tx_id = tx.compute_transaction_id()
    print(f"    ✓ Transaction ID computed")
    print(f"    ID: 0x{tx_id.hex()}")
    print()

    # Step 6: Serialize for network transmission
    print("[6] Serializing transaction")
    serialized = tx.serialize()
    print(f"    ✓ Transaction serialized")
    print(f"    Size: {len(serialized)} bytes")
    print(f"    Hex: 0x{serialized.hex()}")
    print()

    # Step 7: Verify round-trip serialization
    print("[7] Verifying serialization (round-trip test)")
    tx2 = Transaction.deserialize(serialized)
    assert tx2.version == tx.version, "Version mismatch"
    assert tx2.nonce == tx.nonce, "Nonce mismatch"
    assert tx2.from_address == tx.from_address, "From address mismatch"
    assert tx2.tx_type == tx.tx_type, "Type mismatch"
    assert tx2.data == tx.data, "Data mismatch"
    assert tx2.fee == tx.fee, "Fee mismatch"
    assert tx2.signature == tx.signature, "Signature mismatch"

    tx_id2 = tx2.compute_transaction_id()
    assert tx_id2 == tx_id, "Transaction ID mismatch after deserialization"

    print(f"    ✓ Serialization verified - all fields match")
    print()

    print("=" * 60)
    print("Transaction Summary")
    print("=" * 60)
    print(f"Transaction ID:  0x{tx_id.hex()}")
    print(f"From Address:    {account.address}")
    print(f"Data Size:       {len(tx.data)} bytes")
    print(f"Fee:             {tx.fee}")
    print(f"Serialized Size: {len(serialized)} bytes")
    print()
    print("✓ Transaction is ready for network transmission!")


if __name__ == "__main__":
    main()
