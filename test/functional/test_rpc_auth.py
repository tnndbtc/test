#!/usr/bin/env python3
"""
Functional test for RPC authentication.

Tests that RPC endpoints require authentication and work correctly
with the .cookie file credentials.
"""

import sys
import json
import unittest
from test_framework import TestFramework, BlockweaveNode


class RpcAuthTest(TestFramework):
    """Test RPC authentication with .cookie file."""

    num_nodes = 1

    def setup(self):
        """Setup test environment - start a local node."""
        self.node = self.nodes[0] if self.nodes else None
        if not self.node:
            raise RuntimeError("Failed to start blockweave node")

    def test_rpc_getpeer_authenticated(self):
        """Test /rpc/getpeer with authentication."""
        self.log_info("Testing /rpc/getpeer with authentication...")

        # Make authenticated request
        response = self.node.get("/rpc/getpeer")

        self.log_info(f"Response status: {response.status_code}")

        # Should succeed with authentication
        self.assert_equal(
            response.status_code,
            200,
            "GET /rpc/getpeer returns 200 OK with authentication"
        )

        # Parse JSON response
        try:
            data = response.json()
            self.log_info(f"Response data: {json.dumps(data, indent=2)}")
        except json.JSONDecodeError as e:
            self.fail(f"Response is not valid JSON: {e}")
            return

        # Check for peers field
        self.assert_in(
            "peers",
            data,
            "Response contains 'peers' field"
        )

        # Peers should be a list
        self.assert_true(
            isinstance(data.get("peers"), list),
            "peers is a list"
        )

        self.log_info(f"Number of peers: {len(data.get('peers', []))}")

    def test_rpc_getpeer_handshake_fields(self):
        """Test that /rpc/getpeer includes handshake status fields."""
        self.log_info("Testing /rpc/getpeer handshake fields...")

        response = self.node.get("/rpc/getpeer")
        self.assert_equal(response.status_code, 200, "/rpc/getpeer successful")

        data = response.json()
        peers = data.get("peers", [])

        if len(peers) > 0:
            # Check first peer has handshake fields
            peer = peers[0]
            self.log_info(f"First peer: {json.dumps(peer, indent=2)}")

            self.assert_in(
                "handshake_complete",
                peer,
                "Peer has 'handshake_complete' field"
            )

            self.assert_in(
                "handshake_state",
                peer,
                "Peer has 'handshake_state' field"
            )

            # handshake_complete should be boolean
            self.assert_true(
                isinstance(peer.get("handshake_complete"), bool),
                "handshake_complete is boolean"
            )

            # handshake_state should be integer
            self.assert_true(
                isinstance(peer.get("handshake_state"), int),
                "handshake_state is integer"
            )

            self.log_info(f"Handshake complete: {peer.get('handshake_complete')}")
            self.log_info(f"Handshake state: {peer.get('handshake_state')}")
        else:
            self.log_info("No peers connected, skipping handshake field check")

    def test_cookie_credentials_loaded(self):
        """Test that cookie credentials were loaded successfully."""
        self.log_info("Checking cookie credentials...")

        self.assert_true(
            hasattr(self.node, 'cookie_credentials'),
            "Node has cookie_credentials attribute"
        )

        self.assert_true(
            self.node.cookie_credentials is not None,
            "cookie_credentials is not None"
        )

        username, password = self.node.cookie_credentials

        self.assert_equal(
            username,
            "__cookie__",
            "Cookie username is '__cookie__'"
        )

        self.assert_equal(
            len(password),
            64,
            "Cookie password is 64 characters (32 bytes hex)"
        )

        self.log_info(f"Cookie credentials loaded: {username}:{password[:8]}...")

    def cleanup(self):
        """Cleanup - stop the node."""
        if self.node:
            self.log_info("Stopping blockweave node...")
            self.node.stop()
            self.log_info("Node stopped")


if __name__ == "__main__":
    unittest.main()
