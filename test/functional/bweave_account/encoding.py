"""
RLP (Recursive Length Prefix) encoding for Ethereum compatibility

This module provides RLP encoding utilities for transaction signing.
RLP is the serialization method used by Ethereum for data structures.
"""

import rlp


def rlp_encode(data) -> bytes:
    """
    RLP encode data following Ethereum specification

    Args:
        data: Python object to encode (list, bytes, int)

    Returns:
        RLP-encoded bytes

    Raises:
        TypeError: If data type not supported

    Example:
        >>> rlp_encode(b"dog")
        b'\\x83dog'
        >>> rlp_encode([b"cat", b"dog"])
        b'\\xc8\\x83cat\\x83dog'
        >>> rlp_encode([])
        b'\\xc0'

    Note:
        RLP supports encoding of:
        - Byte strings
        - Lists (recursively)
        - Integers (converted to big-endian bytes)
    """
    return rlp.encode(data)


def rlp_encode_transaction(tx_data: list) -> bytes:
    """
    RLP encode transaction fields (specialized for transaction structure)

    Args:
        tx_data: List of transaction fields in order:
                 [version, nonce, from, type, data, fee]
                 or with signature:
                 [version, nonce, from, type, data, fee, v, r, s]

    Returns:
        RLP-encoded transaction bytes

    Example:
        >>> # Unsigned transaction
        >>> tx_data = [0, 1, b'\\x12'*20, 0, b'hello', 1000]
        >>> encoded = rlp_encode_transaction(tx_data)
        >>> len(encoded) > 0
        True

        >>> # Signed transaction
        >>> tx_data_signed = [0, 1, b'\\x12'*20, 0, b'hello', 1000, 0, b'\\x00'*32, b'\\x00'*32]
        >>> encoded_signed = rlp_encode_transaction(tx_data_signed)
        >>> len(encoded_signed) > len(encoded)
        True
    """
    return rlp.encode(tx_data)


def rlp_decode(data: bytes):
    """
    RLP decode bytes back to Python objects

    Args:
        data: RLP-encoded bytes

    Returns:
        Decoded Python object (bytes, list, or int)

    Example:
        >>> encoded = rlp_encode(b"dog")
        >>> rlp_decode(encoded)
        b'dog'
    """
    return rlp.decode(data)
