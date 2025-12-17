"""
Custom exceptions for bweave_account package

This module defines a hierarchy of exceptions for error handling
across the package.
"""


class BweaveError(Exception):
    """
    Base exception for bweave package

    All custom exceptions in this package inherit from this class.
    This allows catching all package-specific errors with a single except clause.

    Example:
        >>> try:
        ...     account = BweaveAccount.from_keystore("missing.json", "pass")
        ... except BweaveError as e:
        ...     print(f"Bweave error: {e}")
    """
    pass


class KeystoreError(BweaveError):
    """
    Keystore loading/decryption errors

    Raised when:
    - Keystore file not found
    - Invalid JSON format
    - Wrong password
    - Incompatible keystore version
    - Decryption failure

    Example:
        >>> try:
        ...     account = BweaveAccount.from_keystore("wallet.json", "wrongpass")
        ... except KeystoreError as e:
        ...     print(f"Keystore error: {e}")
    """
    pass


class SignatureError(BweaveError):
    """
    Signature generation/verification errors

    Raised when:
    - Signing operation fails
    - Invalid signature format
    - Signature verification fails

    Example:
        >>> try:
        ...     r, s, v = account.sign_hash(invalid_hash)
        ... except SignatureError as e:
        ...     print(f"Signature error: {e}")
    """
    pass


class TransactionError(BweaveError):
    """
    Transaction creation/serialization errors

    Raised when:
    - Transaction validation fails
    - Missing signature (for operations requiring it)
    - Serialization/deserialization fails
    - Invalid transaction fields

    Example:
        >>> try:
        ...     tx_id = tx.compute_transaction_id()  # Before signing
        ... except TransactionError as e:
        ...     print(f"Transaction error: {e}")
    """
    pass


class EncodingError(BweaveError):
    """
    RLP encoding errors

    Raised when:
    - RLP encoding fails
    - Invalid data type for RLP
    - Decoding corrupted RLP data

    Example:
        >>> try:
        ...     encoded = rlp_encode(unsupported_type)
        ... except EncodingError as e:
        ...     print(f"Encoding error: {e}")
    """
    pass
