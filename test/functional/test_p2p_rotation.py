#!/usr/bin/env python3
"""
Functional test for CPeerManager::RotateOutboundConnections()

Tests the outbound peer rotation policy using mock time to control
time progression without real-time waits.

Policy:
- Rotate 1-2 oldest outbound peers every 30 minutes (1800 seconds)
- Requires minimum 2 outbound peers to trigger rotation
- Connection time tracked per peer
"""

import sys
import time
import unittest
from test_framework import TestFramework


class OutboundRotationTest(TestFramework):
    """Test outbound peer rotation policy with mock time control."""

    num_nodes = 4  # 1 main node + 3 peer nodes for connections

    def setup(self):
        """Setup test environment - nodes automatically started by framework."""
        self.main_node = self.nodes[0]
        self.peer1 = self.nodes[1]
        self.peer2 = self.nodes[2]
        self.peer3 = self.nodes[3]

        self.log_info("Test setup complete")
        self.log_info(f"Main node: port {self.main_node.port}, p2p {self.main_node.p2p_port}")
        self.log_info(f"Peer1: port {self.peer1.port}, p2p {self.peer1.p2p_port}")
        self.log_info(f"Peer2: port {self.peer2.port}, p2p {self.peer2.p2p_port}")
        self.log_info(f"Peer3: port {self.peer3.port}, p2p {self.peer3.p2p_port}")

    def test_1_rotation_requires_minimum_peers(self):
        """Test: No rotation occurs with fewer than 2 outbound peers."""
        self.log_info("test_1_rotation_requires_minimum_peers: Rotation requires minimum 2 peers")

        # Set initial mock time
        initial_time = 10000
        self.main_node.set_mock_time(initial_time)
        self.log_info(f"Set mock time to {initial_time}")
        # Trigger rotation to reset m_last_rotation_time to initial_time
        self.main_node.trigger_rotation()

        # Connect to peer1 (only 1 outbound connection)
        self.assert_true(
            self.main_node.connect_to_peer(self.peer1, wait=True),
            "Successfully connected to peer1"
        )

        # Wait for connection to establish
        # time.sleep(1)

        # Verify 1 outbound peer
        outbound_count = self.main_node.count_outbound_peers()
        self.assert_equal(
            outbound_count,
            1,
            f"Have 1 outbound peer (got {outbound_count})"
        )

        # Advance time by 1800s (rotation interval)
        advanced_time = initial_time + 1800
        self.main_node.set_mock_time(advanced_time)
        self.log_info(f"Advanced mock time to {advanced_time} (+1800s)")

        # Trigger rotation check immediately
        self.main_node.trigger_rotation()
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # Verify peer1 still connected (no rotation with only 1 peer)
        self.assert_true(
            self.main_node.is_connected_to(self.peer1),
            "Peer1 still connected (no rotation with <2 peers)"
        )

        outbound_count = self.main_node.count_outbound_peers()
        self.assert_equal(
            outbound_count,
            1,
            f"Still have 1 outbound peer (got {outbound_count})"
        )

        # Cleanup: Disconnect peer1 explicitly for next test
        self.main_node.disconnect_peer(self.peer1)
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # Verify peer1 is disconnected
        self.assert_true(
            not self.main_node.is_connected_to(self.peer1),
            "Peer1 disconnected after cleanup"
        )

    def test_2_rotation_requires_time_interval(self):
        """Test: No rotation occurs before 1800s interval elapsed."""
        self.log_info("test_2_rotation_requires_time_interval: Rotation requires 1800s time interval")

        # Set initial mock time
        # need to use same initial_time so that m_last_rotation_time is still at 10000
        initial_time = 10000
        self.main_node.set_mock_time(initial_time)
        self.log_info(f"Set mock time to {initial_time}")
        # No need to trigger rotation to reset m_last_rotation_time
        # self.main_node.trigger_rotation()

        # Connect to peer1
        self.assert_true(
            self.main_node.connect_to_peer(self.peer1, wait=True),
            "Successfully connected to peer1"
        )

        # Connect to peer2
        self.assert_true(
            self.main_node.connect_to_peer(self.peer2, wait=True),
            "Successfully connected to peer2"
        )

        # Connect to peer3
        self.assert_true(
            self.main_node.connect_to_peer(self.peer3, wait=True),
            "Successfully connected to peer3"
        )

        # Verify 3 outbound peers
        outbound_count = self.main_node.count_outbound_peers()
        self.assert_equal(
            outbound_count,
            3,
            f"Have 3 outbound peers (got {outbound_count})"
        )

        # Advance time by 1700s (not enough for rotation - need 1800s)
        advanced_time = initial_time + 1700
        self.main_node.set_mock_time(advanced_time)
        self.log_info(f"Advanced mock time to {advanced_time} (+1700s, not enough)")

        # Trigger rotation check immediately
        self.main_node.trigger_rotation()
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # Verify both peers still connected
        self.assert_true(
            self.main_node.is_connected_to(self.peer1),
            "Peer1 still connected (interval not met)"
        )
        self.assert_true(
            self.main_node.is_connected_to(self.peer2),
            "Peer2 still connected (interval not met)"
        )
        self.assert_true(
            self.main_node.is_connected_to(self.peer3),
            "Peer3 still connected (interval not met)"
        )

        outbound_count = self.main_node.count_outbound_peers()
        self.assert_equal(
            outbound_count,
            3,
            f"Still have 3 outbound peers (got {outbound_count})"
        )

        # Cleanup: Disconnect all peers explicitly for next test
        self.main_node.disconnect_peer(self.peer1)
        self.main_node.disconnect_peer(self.peer2)
        self.main_node.disconnect_peer(self.peer3)
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # Verify cleanup
        outbound_after_cleanup = self.main_node.count_outbound_peers()
        self.log_info(f"After cleanup: {outbound_after_cleanup} peers (expected 0)")
        self.assert_equal(
            outbound_after_cleanup,
            0,
            f"After cleanup, should have 0 outbound peers (got {outbound_after_cleanup})"
        )

    def test_3_rotation_disconnects_oldest(self):
        """Test: Rotation disconnects oldest peer after 1800s interval."""
        self.log_info("test_3_rotation_disconnects_oldest: Rotation disconnects oldest peer")

        # Set initial mock time
        initial_time = 10000
        self.main_node.set_mock_time(initial_time)
        self.log_info(f"Set mock time to {initial_time}")
        # No need to trigger rotation to reset m_last_rotation_time because we are still using 10000
        # self.main_node.trigger_rotation()

        # Connect to peer1 (will be oldest)
        self.assert_true(
            self.main_node.connect_to_peer(self.peer1, wait=True),
            "Successfully connected to peer1 (newest)"
        )

        # Advance time before connecting to peer2
        intermediate_time = initial_time + 100
        self.main_node.set_mock_time(intermediate_time)
        self.log_info(f"Advanced time to {intermediate_time} before peer2 connection")

        # Connect to peer2 (will be newer)
        self.assert_true(
            self.main_node.connect_to_peer(self.peer2, wait=True),
             "Successfully connected to peer2"
        )

        # Advance time before connecting to peer3
        intermediate_time2 = initial_time + 1000
        self.main_node.set_mock_time(intermediate_time2)
        self.log_info(f"Advanced time to {intermediate_time2} before peer3 connection")

        # Connect to peer3
        self.assert_true(
            self.main_node.connect_to_peer(self.peer3, wait=True),
             "Successfully connected to peer3"
        )

        # Verify 3 outbound peers
        outbound_count = self.main_node.count_outbound_peers()
        self.assert_equal(
            outbound_count,
            3,
            f"Have 3 outbound peers before rotation (got {outbound_count})"
        )

        # Advance time by 800s from last rotation (should trigger rotation)
        rotation_time = intermediate_time2 + 800
        self.main_node.set_mock_time(rotation_time)
        self.log_info(f"Advanced mock time to {rotation_time} (+800s from peer3 connection)")

        # Trigger rotation check immediately
        self.main_node.trigger_rotation()
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # Both peers will be rotated out
        # Note: The actual behavior depends on CPeerManager's rotation logic
        outbound_count_after = self.main_node.count_outbound_peers()

        # The rotation should have removed the oldest 2 peers
        # Since we started with 3 peers, after rotation we should have 1 left
        self.assert_equal(
            outbound_count_after,
            1,
            f"After rotation, should have 1 peer left (got {outbound_count_after})"
        )

        # peer1 (oldest) should be disconnected
        self.assert_true(
            self.main_node.is_connected_to(self.peer1) == False,
            "Peer1 is connected after rotation"
        )

        # peer2 (newer) should still be disconnected
        self.assert_true(
            self.main_node.is_connected_to(self.peer2) == False,
            "Peer2 is disconected after rotation"
        )

        # peer3 (newest) should still be connected
        self.assert_true(
            self.main_node.is_connected_to(self.peer3),
            "Peer3 (newest) still connected after rotation"
        )

        # Cleanup: Disconnect all peers explicitly for next test
        self.main_node.disconnect_peer(self.peer3)
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # Verify cleanup
        outbound_after_cleanup = self.main_node.count_outbound_peers()
        self.log_info(f"After cleanup: {outbound_after_cleanup} peers (expected 0)")
        self.assert_equal(
            outbound_after_cleanup,
            0,
            f"After cleanup, should have 0 outbound peers (got {outbound_after_cleanup})"
        )

    def test_4_rotation_updates_timestamp(self):
        """Test: Rotation interval resets after rotation occurs."""
        self.log_info("test_4_rotation_updates_timestamp: Rotation updates m_last_rotation_time")

        # Set initial mock time, this 11800 is calculated from previous 3 tests
        initial_time = 11800
        self.main_node.set_mock_time(initial_time)
        self.log_info(f"Set mock time to {initial_time}")
        self.main_node.trigger_rotation()

        # Connect to all peers
        self.assert_true(
            self.main_node.connect_to_peer(self.peer1, wait=True),
            "Successfully connected to peer1"
        )
        self.assert_true(
            self.main_node.connect_to_peer(self.peer2, wait=True),
            "Successfully connected to peer2"
        )

        # make peer3 as newer peer
        self.main_node.set_mock_time(initial_time+100)
        self.assert_true(
            self.main_node.connect_to_peer(self.peer3, wait=True),
            "Successfully connected to peer3"
        )

        # Verify 3 peers connected
        outbound_count = self.main_node.count_outbound_peers()
        self.assert_equal(
            outbound_count,
            3,
            f"Have 3 outbound peers (got {outbound_count})"
        )

        # Trigger first rotation by advancing 1800s
        first_rotation_time = initial_time + 1800
        self.main_node.set_mock_time(first_rotation_time)
        self.log_info(f"Advanced time to {first_rotation_time} for first rotation")
        self.main_node.trigger_rotation()
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # After first rotation, should have 1 peer (peer3) left
        outbound_after_first = self.main_node.count_outbound_peers()
        self.assert_equal(
            outbound_after_first,
            1,
            f"After first rotation, have 1 peer (got {outbound_after_first})"
        )

        # Reconnect the disconnected peer to restore 3 connections
        # Note: We know it's peer1 and peer2 disconnected, so reconnect both
        self.assert_true(
            self.main_node.connect_to_peer(self.peer1, wait=True),
            "Successfully connected to peer1"
        )
        self.main_node.set_mock_time(first_rotation_time+100)
        self.assert_true(
            self.main_node.connect_to_peer(self.peer2, wait=True),
            "Successfully connected to peer2"
        )

        # Verify we have 3 peers again
        outbound_count = self.main_node.count_outbound_peers()
        self.log_info(f"After reconnecting, have {outbound_count} peers")
        self.assert_equal(
            outbound_count,
            3,
            f"Have 3 outbound peers (got {outbound_count})"
        )

        # Advance time by only 1700s (not enough for next rotation since last was at first_rotation_time)
        second_time = first_rotation_time + 1700
        self.main_node.set_mock_time(second_time)
        self.log_info(f"Advanced time to {second_time} (+1700s, should not rotate)")
        self.main_node.trigger_rotation()
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # No rotation should occur (interval not met)
        current_count = self.main_node.count_outbound_peers()
        self.log_info(f"After +1700s: {current_count} peers (should be 3, no rotation)")
        self.assert_equal(
            current_count,
            3,
            f"Have 3 outbound peers (got {current_count})"
        )

        # Now advance by additional 100s (total 1800s from first rotation) to trigger second rotation
        third_time = second_time + 100
        self.main_node.set_mock_time(third_time)
        self.log_info(f"Advanced time to {third_time} (+100s more, total 1800s, should rotate)")
        self.main_node.trigger_rotation()
        time.sleep(0.5)  # Brief wait for disconnect to complete

        # Second rotation should have occurred
        # peer1 and peer3 will be rotated out, peer2 is newest
        final_count = self.main_node.count_outbound_peers()
        self.log_info(f"After second rotation: {final_count} peers")
        self.assert_equal(
            final_count,
            1,
            f"Have 1 outbound peers (got {current_count})"
        )
        # Verify peer1 is disconnected
        self.assert_true(
            self.main_node.is_connected_to(self.peer1) == False,
            "Peer1 is disconnected"
        )

        # Verify peer3 is disconnected
        self.assert_true(
            self.main_node.is_connected_to(self.peer3) == False,
            "Peer3 is disconnected"
        )

        # Verify peer2 still connected, because it's newest
        self.assert_true(
            self.main_node.is_connected_to(self.peer2),
            "Peer2 still connected"
        )

        # The key assertion is that rotation did not occur at +1700s but did at +1800s
        # This test primarily validates timing logic rather than specific peer counts
        self.log_info("Rotation interval timing validated")

        # Cleanup: disconnect last peer2
        self.main_node.disconnect_peer(self.peer2)
        # Verify cleanup
        outbound_after_cleanup = self.main_node.count_outbound_peers()
        self.log_info(f"After cleanup: {outbound_after_cleanup} peers (expected 0)")
        self.assert_equal(
            outbound_after_cleanup,
            0,
            f"After cleanup, should have 0 outbound peers (got {outbound_after_cleanup})"
        )

if __name__ == "__main__":
    unittest.main()
