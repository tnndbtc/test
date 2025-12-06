#!/usr/bin/env python3
"""
Memory-Based Transaction Propagation Stress Test for Blockweave

Memory-based stress test that generates transactions until memory threshold:
- Runs until memory reaches 80% OR 120 seconds elapsed
- 20 Python threads submit transactions in parallel
- Distributes transactions across all 4 nodes in round-robin fashion
- Continues generating while memory < 80% total AND time < 120s
- Automatically stops when threshold reached
- Records CPU usage, memory consumption, test duration, and throughput
- Export metrics to JSON

Environment:
  PYTHONPATH=../functional (required)
  STRESS_TEST_RESULTS_DIR=results (optional, default ./results)

Run: python3 test_stress_transaction_propagation.py
"""

import sys
import time
import unittest
import os
import psutil
import threading
from test_framework import TestFramework
from metrics_collector import MetricsCollector, AggregateMetrics


class TransactionPropagationStressTest(TestFramework):
    """Stress test for transaction propagation across multiple nodes."""

    # 4 nodes across different subnets
    num_nodes = 4

    def setup_nodes(self):
        """
        Start 4 nodes, all bound to localhost with different ports.

        Topology:
        - node0: localhost:48443 (REST), localhost:48333 (P2P)
        - node1: localhost:48444 (REST), localhost:48334 (P2P)
        - node2: localhost:48445 (REST), localhost:48335 (P2P)
        - node3: localhost:48446 (REST), localhost:48336 (P2P)
        """
        if self.num_nodes <= 0:
            return

        self.log_info(f"Starting {self.num_nodes} nodes on localhost...")

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

        self.log_info(f"All {len(self.nodes)} nodes started successfully")

    def setup(self):
        """
        Setup test environment - establish mesh topology.

        Topology:
        Node0 <-> Node1
        Node0 <-> Node2
        Node0 <-> Node3
        Node1 <-> Node2
        Node1 <-> Node3
        Node2 <-> Node3
        """
        self.log_info("setup: Establishing mesh topology...")

        # Connect all nodes in mesh (each node connects to all others)
        for i in range(len(self.nodes)):
            for j in range(i + 1, len(self.nodes)):
                node_i = self.nodes[i]
                node_j = self.nodes[j]

                self.log_info(f"setup: Connecting node{i} <-> node{j}...")
                if node_i.connect_to_peer(node_j, wait=True):
                    self.log_info(f"setup: node{i} <-> node{j} connected")
                else:
                    self.log_error(f"setup: FAILED to connect node{i} <-> node{j}")

        # Wait for connections to stabilize
        time.sleep(2)

        # Log peer counts
        for i, node in enumerate(self.nodes):
            peer_info = node.get_peer_info()
            total = peer_info.get('total_peers', 0)
            self.log_info(f"setup: Node{i} has {total} peers")

        self.log_info("setup: Mesh topology established")

    def test_transaction_propagation_stress(self):
        """
        Memory-based stress test: Generate transactions until memory reaches 80%.

        Steps:
        1. Detect system resources (total memory)
        2. Initialize metrics collectors for all nodes
        3. Generate transactions distributed across all 4 nodes
        4. Continue generating while memory < 80% AND time < 120 seconds
        5. Stop when memory reaches 80% OR 120 seconds elapsed
        6. Calculate throughput and export metrics
        """
        self.log_info("test_transaction_propagation_stress: Starting memory-based stress test...")

        # Validate nodes are running
        if len(self.nodes) == 0:
            raise RuntimeError("No nodes are running - cannot start stress test")

        self.log_info(f"Running stress test with {len(self.nodes)} nodes")

        # Configuration
        n_threads = 200
        memory_threshold_percent = 80  # Stop when memory >= 80% total
        max_duration_sec = 60  # Stop after 120 seconds maximum
        memory_check_interval = 0.5  # Check memory every 500ms
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
        aggregate = AggregateMetrics("memory_transaction_propagation_stress")

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
            test_type="memory_based_transaction_propagation",
            num_threads=n_threads,
            total_memory_mb=round(total_memory_mb, 2),
            memory_threshold_mb=round(memory_threshold_mb, 2),
            memory_threshold_percent=memory_threshold_percent,
            max_duration_sec=max_duration_sec
        )

        # Step 3: Start metrics collection
        self.log_info("Step 3: Starting metrics collection...")
        for collector in collectors:
            collector.start_collection()

        # Get initial mempool sizes
        initial_mempools = []
        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            mempool_size = chain_info.get('mempool_size', 0)
            initial_mempools.append(mempool_size)
            self.log_info(f"  Node{i} initial mempool: {mempool_size}")

        # Step 4: Multi-threaded transaction generation with time limit
        self.log_info(f"Step 4: Starting {n_threads}-threaded transaction generation until {memory_threshold_percent}% memory or {max_duration_sec}s...")

        # Shared state between threads
        f_stop_requested = threading.Event()  # Signal to stop all threads
        tx_counter_lock = threading.Lock()
        node_idx_lock = threading.Lock()
        per_node_lock = threading.Lock()
        tx_counter = 0
        node_idx = 0  # Round-robin node selection
        thread_tx_counts = [0] * n_threads  # Per-thread transaction counts
        thread_errors = [0] * n_threads  # Per-thread error counts
        per_node_tx_counts = [0] * len(self.nodes)  # Track txs sent to each node

        def worker_thread(thread_id):
            """Worker thread that submits transactions until stop signal."""
            nonlocal tx_counter, node_idx
            local_tx_count = 0
            local_error_count = 0

            while not f_stop_requested.is_set():
                # Get transaction ID and target node atomically
                with tx_counter_lock:
                    tx_id = tx_counter
                    tx_counter += 1

                with node_idx_lock:
                    target_node_idx = node_idx % len(self.nodes)
                    node_idx += 1

                target_node = self.nodes[target_node_idx]

                # Generate transaction
                tx_data = {
                    "from": f"thread{thread_id}_wallet_{tx_id}",
                    "to": f"receiver_{tx_id % 100}",
                    "data": f"propagation_stress_tx_thread{thread_id}_{tx_id}",
                    "fee": 1
                }

                try:
                    success = target_node.create_transaction(tx_data)
                    if success:
                        local_tx_count += 1
                        with per_node_lock:
                            per_node_tx_counts[target_node_idx] += 1
                except Exception as e:
                    local_error_count += 1

            # Store thread-local counts
            thread_tx_counts[thread_id] = local_tx_count
            thread_errors[thread_id] = local_error_count

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
                with tx_counter_lock:
                    current_tx_count = tx_counter
                with per_node_lock:
                    node_distribution = list(per_node_tx_counts)
                rate = current_tx_count / elapsed if elapsed > 0 else 0
                memory_percent = (avg_memory / total_memory_mb) * 100
                self.log_info(f"  Progress: {elapsed:.0f}s, "
                              f"{current_tx_count} txs ({rate:.1f} tx/sec), "
                              f"memory: {avg_memory:.1f}MB ({memory_percent:.1f}%), "
                              f"distribution: {node_distribution}")
                last_progress_time = time.time()

        # Wait for all threads to finish
        self.log_info("  Waiting for threads to complete...")
        for t in threads:
            t.join(timeout=5)

        total_time = time.time() - start_time
        total_txs = tx_counter
        total_errors = sum(thread_errors)

        self.log_info(f"Step 4 complete: {total_txs} transactions generated in {total_time:.2f}s")
        self.log_info(f"  Per-thread distribution: {thread_tx_counts}")
        self.log_info(f"  Per-node distribution: {per_node_tx_counts}")
        self.log_info(f"  Total errors: {total_errors}")

        # Step 5: Wait briefly for final propagation
        self.log_info("Step 5: Waiting for final transaction propagation...")
        time.sleep(2)

        # Step 6: Stop metrics collection
        self.log_info("Step 6: Stopping metrics collection...")
        for collector in collectors:
            collector.stop_collection()

        # Step 7: Collect final state and update counters
        self.log_info("Step 7: Collecting final state...")
        total_received = 0
        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            mempool_size = chain_info.get('mempool_size', 0)

            self.log_info(f"  Node{i} final mempool: {mempool_size}")

            # Update collector counters
            n_sent = per_node_tx_counts[i]
            n_received = mempool_size - initial_mempools[i]
            total_received += n_received
            collectors[i].set_transaction_count(sent=n_sent, received=n_received)

        # Calculate peak resources
        peak_cpu = max(c.get_cpu_max() for c in collectors)
        peak_memory = max(c.get_memory_max() for c in collectors)

        self.log_info(f"  Peak CPU: {peak_cpu:.1f}%")
        self.log_info(f"  Peak memory: {peak_memory:.1f} MB")

        # Step 8: Export metrics
        self.log_info("Step 8: Exporting metrics...")
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        output_file = os.path.join(results_dir, f"memory_transaction_propagation_{timestamp}.json")

        # Add additional metadata
        aggregate.metadata['memory_based_stats'] = {
            'total_transactions_generated': total_txs,
            'per_thread_distribution': thread_tx_counts,
            'per_node_distribution': per_node_tx_counts,
            'total_errors': total_errors,
            'actual_duration_sec': round(total_time, 2),
            'max_duration_sec': max_duration_sec,
            'stop_reason': stop_reason,
            'peak_cpu_percent': round(peak_cpu, 2),
            'peak_memory_mb': round(peak_memory, 2),
            'throughput_tx_per_sec': round(total_txs / total_time, 2) if total_time > 0 else 0,
            'total_propagated': total_received
        }

        aggregate.export_json(output_file)
        aggregate.print_summary()

        self.log_info("test_transaction_propagation_stress: Memory-based stress test completed successfully!")


if __name__ == "__main__":
    # Ensure results directory exists
    results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')
    os.makedirs(results_dir, exist_ok=True)
    unittest.main()
