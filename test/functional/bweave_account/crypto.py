"""
Cryptographic primitives for Ethereum-compatible signing

This module provides:
- keccak256(): Ethereum Keccak-256 hashing (NOT NIST SHA3-256)
- sign(): secp256k1 ECDSA signing
"""

from Crypto.Hash import keccak
from eth_keys import keys


def keccak256(data: bytes) -> bytes:
    """
    Compute Keccak-256 hash (Ethereum standard, NOT NIST SHA3)

    Args:
        data: Input bytes to hash

    Returns:
        32-byte hash digest

    Example:
        >>> hash_result = keccak256(b"hello")
        >>> len(hash_result)
        32

    Note:
        Ethereum uses the original Keccak-256, not the final NIST SHA3-256.
        These produce different outputs for the same input.
    """
    k = keccak.new(digest_bits=256)
    k.update(data)
    return k.digest()


def sign(private_key: bytes, message_hash: bytes) -> tuple[bytes, bytes, int]:
    """
    Sign 32-byte message hash with secp256k1 ECDSA

    Args:
        private_key: 32-byte secp256k1 private key
        message_hash: 32-byte hash to sign

    Returns:
        (r, s, v) tuple where:
        - r: 32 bytes (signature component)
        - s: 32 bytes (signature component)
        - v: int (recovery id, 0 or 1)

    Raises:
        ValueError: If private_key or message_hash wrong length

    Example:
        >>> private_key = bytes.fromhex("1234...")
        >>> msg_hash = keccak256(b"message")
        >>> r, s, v = sign(private_key, msg_hash)
        >>> len(r), len(s), v in (0, 1)
        (32, 32, True)
    """
    if len(private_key) != 32:
        raise ValueError("private_key must be 32 bytes")
    if len(message_hash) != 32:
        raise ValueError("message_hash must be 32 bytes")

    pk = keys.PrivateKey(private_key)
    signature = pk.sign_msg_hash(message_hash)

    return (
        signature.r.to_bytes(32, 'big'),
        signature.s.to_bytes(32, 'big'),
        signature.v
    )


def verify_signature(public_key: bytes, message_hash: bytes,
                     signature: tuple[bytes, bytes, int]) -> bool:
    """
    Verify ECDSA signature (useful for testing)

    Args:
        public_key: 64-byte uncompressed public key (no 0x04 prefix)
        message_hash: 32-byte hash that was signed
        signature: (r, s, v) tuple from sign()

    Returns:
        True if signature valid, False otherwise

    Example:
        >>> private_key = b'\\x01' * 32
        >>> pk = keys.PrivateKey(private_key)
        >>> public_key = pk.public_key.to_bytes()
        >>> msg_hash = keccak256(b"test")
        >>> sig = sign(private_key, msg_hash)
        >>> verify_signature(public_key, msg_hash, sig)
        True
    """
    try:
        r, s, v = signature

        # Create signature object
        from eth_keys.datatypes import Signature
        sig_obj = Signature(vrs=(v, int.from_bytes(r, 'big'), int.from_bytes(s, 'big')))

        # Recover public key from signature
        recovered_pubkey = sig_obj.recover_public_key_from_msg_hash(message_hash)

        # Compare with provided public key
        return recovered_pubkey.to_bytes() == public_key

    except Exception:
        return False
