#!/usr/bin/env python3
"""
Malicious Nodes Stress Test for Blockweave

Memory-based stress test for resilience against invalid message flooding:
- Runs until memory reaches 80% OR 60 seconds elapsed
- 20 Python threads flood with invalid messages in parallel
- 3 legitimate nodes operating normally
- Continues flooding while memory < 80% total AND time < 60s
- Automatically stops when threshold reached
- Monitor misbehavior scoring and banning
- Verify legitimate nodes continue operating normally
- Record CPU overhead from validation and rejection rate
- Export metrics to JSON

Environment:
  PYTHONPATH=../functional (required)
  STRESS_TEST_RESULTS_DIR=results (optional, default ./results)

Run: python3 test_stress_malicious_nodes.py
"""

import sys
import time
import unittest
import os
import struct
import psutil
import threading
from test_framework import TestFramework
from test_framework.p2p_utils import MessageType, P2PMessage, P2PConnection
from metrics_collector import MetricsCollector, AggregateMetrics


class MaliciousNodesStressTest(TestFramework):
    """Stress test for resilience against malicious nodes."""

    # 3 legitimate nodes
    num_nodes = 3

    def setup_nodes(self):
        """
        Start 3 legitimate nodes, all bound to localhost with different ports.

        Topology:
        - node0: localhost:48443 (REST), localhost:48333 (P2P) - target of malicious traffic
        - node1: localhost:48444 (REST), localhost:48334 (P2P) - legitimate peer
        - node2: localhost:48445 (REST), localhost:48335 (P2P) - legitimate peer
        """
        if self.num_nodes <= 0:
            return

        self.log_info(f"Starting {self.num_nodes} legitimate nodes on localhost...")

        # Calculate port numbers (using localnet ports)
        base_rest_port = 48443
        base_p2p_port = 48333

        # Create and start nodes
        for i in range(self.num_nodes):
            rest_port = base_rest_port + i
            p2p_port = base_p2p_port + i
            bind_ip = '127.0.0.1'

            self.log_info(
                f"Starting node{i} (REST: {rest_port}, P2P: {p2p_port}, "
                f"bind_ip: {bind_ip}, max_inbound_peers=120, max_outbound_peers=8)"
            )

            blockweave_node = self.add_node(
                port=rest_port,
                p2p_port=p2p_port,
                bind_ip=bind_ip,
                max_inbound_peers=120,
                max_outbound_peers=8
            )

            if not blockweave_node.start(timeout=20):
                raise RuntimeError(f"Failed to start node{i}")

            self.log_info(f"Node{i} started successfully on {bind_ip}")

        self.log_info(f"All {len(self.nodes)} legitimate nodes started successfully")

    def setup(self):
        """
        Setup test environment - establish connections between legitimate nodes.

        Topology:
        Node0 <-> Node1 <-> Node2
        """
        self.log_info("setup: Establishing legitimate node connections...")

        # Connect node0 <-> node1
        if self.nodes[0].connect_to_peer(self.nodes[1], wait=True):
            self.log_info("setup: node0 <-> node1 connected")

        # Connect node1 <-> node2
        if self.nodes[1].connect_to_peer(self.nodes[2], wait=True):
            self.log_info("setup: node1 <-> node2 connected")

        # Wait for connections to stabilize
        time.sleep(2)

        # Log peer counts
        for i, node in enumerate(self.nodes):
            peer_info = node.get_peer_info()
            total = peer_info.get('total_peers', 0)
            self.log_info(f"setup: Node{i} has {total} peers")

        self.log_info("setup: Legitimate network established")

    def create_invalid_inventory(self):
        """
        Create invalid INVENTORY message (malformed hash data).

        Valid format: [count:4][type:2][hash:32]...
        Invalid format: [count:1][type:2][hash:1]  <- Only 1 byte hash instead of 32

        Returns:
            P2PMessage: Invalid INVENTORY message
        """
        payload = b""
        payload += struct.pack('!I', 1)  # Count: 1 item
        payload += struct.pack('!H', 2)  # Type: TRANSACTION
        payload += b'X'  # Hash: Only 1 byte (INVALID)

        return P2PMessage(MessageType.INVENTORY, payload)

    def create_invalid_transaction(self, tx_id):
        """
        Create invalid transaction message (malformed JSON or missing fields).

        Args:
            tx_id: Transaction identifier for uniqueness

        Returns:
            P2PMessage: Invalid transaction message
        """
        # Create malformed transaction payload (invalid JSON)
        payload = f"{{invalid_json_tx_{tx_id}".encode('utf-8')
        return P2PMessage(MessageType.TX, payload)

    def test_malicious_node_stress(self):
        """
        Memory-based stress test: Flood node0 with invalid messages using 20 threads until threshold reached.

        Steps:
        1. Detect system resources (total memory)
        2. Initialize metrics collectors
        3. Submit legitimate transactions to verify normal operation
        4. Spawn 20 threads to flood with invalid messages in parallel
        5. Continue flooding while memory < 80% AND time < 60 seconds
        6. Stop when memory reaches 80% OR 60 seconds elapsed
        7. Verify legitimate nodes continue operating
        8. Calculate rejection rate and export metrics
        """
        self.log_info("test_malicious_node_stress: Starting memory-based stress test...")

        # Validate nodes are running
        if len(self.nodes) == 0:
            raise RuntimeError("No nodes are running - cannot start stress test")

        self.log_info(f"Running stress test with {len(self.nodes)} nodes")

        # Configuration
        n_threads = 200
        memory_threshold_percent = 80  # Stop when memory >= 80% total
        max_duration_sec = 60  # Stop after 60 seconds maximum
        memory_check_interval = 0.5  # Check memory every 500ms
        n_legitimate_txs = 10
        results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')

        # Create results directory
        os.makedirs(results_dir, exist_ok=True)

        # Step 1: Detect system resources
        self.log_info("Step 1: Detecting system resources...")
        total_memory_mb = psutil.virtual_memory().total / (1024 * 1024)
        memory_threshold_mb = total_memory_mb * (memory_threshold_percent / 100.0)

        self.log_info(f"  Total memory: {total_memory_mb:.2f} MB")
        self.log_info(f"  Memory threshold: {memory_threshold_mb:.2f} MB (stop when >= {memory_threshold_mb:.2f} MB)")
        self.log_info(f"  Maximum duration: {max_duration_sec} seconds")
        self.log_info(f"  Thread count: {n_threads}")
        self.log_info(f"  Test runs until memory reaches {memory_threshold_percent}% OR {max_duration_sec}s elapsed")

        # Step 2: Initialize metrics collectors
        self.log_info("Step 2: Initializing metrics collectors...")
        collectors = []
        aggregate = AggregateMetrics("malicious_nodes_stress")

        for i, node in enumerate(self.nodes):
            if node.process and node.process.pid:
                collector = MetricsCollector(pid=node.process.pid, node_id=f"node{i}")
                collectors.append(collector)
                aggregate.add_collector(collector)
                self.log_info(f"  Collector initialized for node{i} (PID: {node.process.pid})")
            else:
                raise RuntimeError(f"Node{i} process not running")

        # Set metadata
        aggregate.set_metadata(
            num_nodes=self.num_nodes,
            test_type="malicious_nodes_memory_based",
            num_threads=n_threads,
            total_memory_mb=round(total_memory_mb, 2),
            memory_threshold_mb=round(memory_threshold_mb, 2),
            memory_threshold_percent=memory_threshold_percent,
            max_duration_sec=max_duration_sec,
            num_legitimate_transactions=n_legitimate_txs
        )

        node0 = self.nodes[0]
        node1 = self.nodes[1]

        # Step 3: Send legitimate transactions to verify normal operation
        self.log_info(f"Step 3: Sending {n_legitimate_txs} legitimate transactions...")
        for i in range(n_legitimate_txs):
            tx_data = {
                "from": f"legitimate_wallet_{i}",
                "to": f"legitimate_receiver_{i}",
                "data": f"legitimate_tx_{i}",
                "fee": 1
            }

            try:
                node1.create_transaction(tx_data)
            except Exception as e:
                self.log_error(f"  Legitimate transaction {i} failed: {e}")

        time.sleep(1)

        # Verify legitimate transactions propagated
        chain_info = node0.get_chain_info()
        mempool_size = chain_info.get('mempool_size', 0)
        self.log_info(f"  Node0 mempool after legitimate txs: {mempool_size}")

        # Step 4: Start metrics collection
        self.log_info("Step 4: Starting metrics collection...")
        for collector in collectors:
            collector.start_collection()

        # Step 5: Multi-threaded malicious message flooding
        self.log_info(f"Step 5: Starting {n_threads}-threaded malicious message flooding until {memory_threshold_percent}% memory or {max_duration_sec}s...")

        # Shared state between threads
        f_stop_requested = threading.Event()  # Signal to stop all threads
        msg_counter_lock = threading.Lock()
        per_thread_lock = threading.Lock()
        msg_counter = 0
        thread_msg_counts = [0] * n_threads  # Per-thread message counts
        thread_errors = [0] * n_threads  # Per-thread error counts
        connections_closed = [0] * n_threads  # Per-thread connection closures

        def worker_thread(thread_id):
            """Worker thread that sends invalid messages until stop signal."""
            nonlocal msg_counter
            local_msg_count = 0
            local_error_count = 0
            local_connection_closed = False

            while not f_stop_requested.is_set():
                # Get message ID atomically
                with msg_counter_lock:
                    msg_id = msg_counter
                    msg_counter += 1

                try:
                    # Create a new P2P connection for each batch of messages
                    # (connections may get closed by misbehavior detection)
                    with P2PConnection("127.0.0.1", node0.p2p_port, timeout=5) as conn:
                        # Send a batch of invalid messages
                        batch_size = 10
                        for i in range(batch_size):
                            if f_stop_requested.is_set():
                                break

                            # Alternate between invalid INVENTORY and TX messages
                            if (msg_id + i) % 2 == 0:
                                msg = self.create_invalid_inventory()
                            else:
                                msg = self.create_invalid_transaction(msg_id + i)

                            sent = conn.send_message(msg)
                            if sent:
                                local_msg_count += 1
                            else:
                                local_error_count += 1
                                local_connection_closed = True
                                break

                            # Small delay between messages
                            time.sleep(0.01)

                except Exception as e:
                    local_error_count += 1
                    local_connection_closed = True

                # Small delay between batches
                time.sleep(0.1)

            # Store thread-local counts
            with per_thread_lock:
                thread_msg_counts[thread_id] = local_msg_count
                thread_errors[thread_id] = local_error_count
                connections_closed[thread_id] = 1 if local_connection_closed else 0

        # Start worker threads
        start_time = time.time()
        threads = []
        self.log_info(f"  Spawning {n_threads} worker threads...")

        for i in range(n_threads):
            t = threading.Thread(target=worker_thread, args=(i,), daemon=True)
            t.start()
            threads.append(t)

        self.log_info(f"  All {n_threads} threads started")

        # Monitor memory usage and time limit
        last_progress_time = start_time
        progress_interval = 10  # Log progress every 10 seconds
        stop_reason = ""

        self.log_info("  Monitoring memory usage and time limit...")

        while True:
            time.sleep(memory_check_interval)

            elapsed = time.time() - start_time

            # Check if time limit exceeded
            if elapsed >= max_duration_sec:
                self.log_info(f"  Time limit reached: {elapsed:.1f}s >= {max_duration_sec}s")
                stop_reason = "time_limit"
                f_stop_requested.set()
                break

            # Get average memory from collectors
            total_memory = 0
            for collector in collectors:
                total_memory += collector.get_memory_avg()

            avg_memory = total_memory / len(collectors) if len(collectors) > 0 else 0

            # Check if memory threshold exceeded
            if avg_memory >= memory_threshold_mb:
                self.log_info(f"  Memory threshold reached: {avg_memory:.1f}MB >= {memory_threshold_mb:.1f}MB")
                stop_reason = "memory_threshold"
                f_stop_requested.set()
                break

            # Progress logging every 10 seconds
            if time.time() - last_progress_time >= progress_interval:
                with msg_counter_lock:
                    current_msg_count = msg_counter
                rate = current_msg_count / elapsed if elapsed > 0 else 0
                memory_percent = (avg_memory / total_memory_mb) * 100
                self.log_info(f"  Progress: {elapsed:.0f}s, "
                              f"{current_msg_count} messages ({rate:.1f} msg/sec), "
                              f"memory: {avg_memory:.1f}MB ({memory_percent:.1f}%)")
                last_progress_time = time.time()

        # Wait for all threads to finish
        self.log_info("  Waiting for threads to complete...")
        for t in threads:
            t.join(timeout=5)

        total_time = time.time() - start_time
        total_messages_attempted = msg_counter
        total_messages_sent = sum(thread_msg_counts)
        total_errors = sum(thread_errors)
        total_connection_closures = sum(connections_closed)

        self.log_info(f"Step 5 complete: {total_messages_sent} malicious messages sent in {total_time:.2f}s")
        self.log_info(f"  Per-thread distribution: {thread_msg_counts}")
        self.log_info(f"  Total errors: {total_errors}")
        self.log_info(f"  Connections closed: {total_connection_closures}/{n_threads}")

        # Wait for system to stabilize
        self.log_info("  Waiting for system to stabilize...")
        time.sleep(2)

        # Step 6: Stop metrics collection
        self.log_info("Step 6: Stopping metrics collection...")
        for collector in collectors:
            collector.stop_collection()

        # Step 7: Verify legitimate nodes still operating
        self.log_info("Step 7: Verifying legitimate nodes still operating...")

        all_nodes_operational = True

        # Check node1 and node2 are still connected
        for i in [1, 2]:
            node = self.nodes[i]
            try:
                chain_info = node.get_chain_info()
                blocks = chain_info.get('blocks', 0)
                mempool = chain_info.get('mempool_size', 0)
                self.log_info(f"  Node{i}: blocks={blocks}, mempool={mempool}")
            except Exception as e:
                self.log_error(f"  Node{i} appears to be down: {e}")
                all_nodes_operational = False

        # Check node0 still responds to legitimate requests
        try:
            chain_info = node0.get_chain_info()
            blocks = chain_info.get('blocks', 0)
            mempool = chain_info.get('mempool_size', 0)
            self.log_info(f"  Node0 (attack target): blocks={blocks}, mempool={mempool}")
        except Exception as e:
            self.log_error(f"  Node0 appears to be unresponsive: {e}")
            all_nodes_operational = False

        # Calculate peak resources
        peak_memory = max(c.get_memory_max() for c in collectors)
        avg_memory = sum(c.get_memory_avg() for c in collectors) / len(collectors)
        peak_cpu = max(c.get_cpu_max() for c in collectors)
        avg_cpu = sum(c.get_cpu_avg() for c in collectors) / len(collectors)

        self.log_info(f"  Peak memory: {peak_memory:.1f} MB")
        self.log_info(f"  Average memory: {avg_memory:.1f} MB")
        self.log_info(f"  Peak CPU: {peak_cpu:.1f}%")
        self.log_info(f"  Average CPU: {avg_cpu:.1f}%")

        # Step 8: Export metrics
        self.log_info("Step 8: Exporting metrics...")
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        output_file = os.path.join(results_dir, f"malicious_nodes_{timestamp}.json")

        # Calculate effectiveness metrics
        rejection_rate = (total_errors / total_messages_attempted) * 100 if total_messages_attempted > 0 else 0
        messages_per_sec = total_messages_sent / total_time if total_time > 0 else 0

        # Add additional metadata with stress_stats
        aggregate.metadata['stress_stats'] = {
            'total_messages_attempted': total_messages_attempted,
            'total_messages_sent': total_messages_sent,
            'total_errors': total_errors,
            'per_thread_distribution': thread_msg_counts,
            'rejection_rate_percent': round(rejection_rate, 2),
            'actual_duration_sec': round(total_time, 2),
            'max_duration_sec': max_duration_sec,
            'stop_reason': stop_reason,
            'peak_cpu_percent': round(peak_cpu, 2),
            'peak_memory_mb': round(peak_memory, 2),
            'avg_cpu_percent': round(avg_cpu, 2),
            'avg_memory_mb': round(avg_memory, 2),
            'throughput_msg_per_sec': round(messages_per_sec, 2),
            'connections_closed': total_connection_closures,
            'ban_triggered': total_connection_closures > 0,
            'legitimate_nodes_operational': all_nodes_operational
        }

        aggregate.export_json(output_file)
        aggregate.print_summary()

        self.log_info("test_malicious_node_stress: Memory-based stress test completed successfully!")


if __name__ == "__main__":
    # Ensure results directory exists
    results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')
    os.makedirs(results_dir, exist_ok=True)
    unittest.main()
