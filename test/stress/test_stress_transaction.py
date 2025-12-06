#!/usr/bin/env python3
"""
Transaction Stress Test for Blockweave (Single Node, Multi-threaded)

Memory-based stress test that generates transactions using 20 parallel threads:
- Runs on a single node (no propagation)
- 20 Python threads submit transactions in parallel
- Continues generating while memory < 80% total
- Automatically stops when memory threshold reached
- Records memory consumption, throughput, and test duration
- Export metrics to JSON

Environment:
  PYTHONPATH=../functional (required)
  STRESS_TEST_RESULTS_DIR=results (optional, default ./results)

Run: python3 test_stress_transaction.py
"""

import sys
import time
import unittest
import os
import psutil
import threading
from test_framework import TestFramework
from metrics_collector import MetricsCollector, AggregateMetrics


class TransactionStressTest(TestFramework):
    """Memory-based stress test for transaction handling on single node."""

    # Single node for this test
    num_nodes = 1

    def setup_nodes(self):
        """
        Start 1 node bound to localhost.

        Topology:
        - node0: localhost (127.0.0.1)
        """
        if self.num_nodes <= 0:
            return

        self.log_info(f"Starting {self.num_nodes} node on localhost...")

        # Calculate port numbers (using localnet ports)
        base_rest_port = 48443
        base_p2p_port = 48333

        rest_port = base_rest_port
        p2p_port = base_p2p_port
        bind_ip = '127.0.0.1'

        self.log_info(
            f"Starting node0 (REST: {rest_port}, P2P: {p2p_port}, "
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
            raise RuntimeError("Failed to start node0")

        self.log_info(f"Node0 started successfully on {bind_ip}")
        self.log_info(f"Node started successfully")

    def setup(self):
        """
        Setup test environment - single node, no peer connections needed.
        """
        self.log_info("setup: Single node ready for transaction stress test")

        # Log node status
        chain_info = self.nodes[0].get_chain_info()
        mempool_size = chain_info.get('mempool_size', 0)
        self.log_info(f"setup: Node0 initial mempool: {mempool_size}")

    def test_transaction_stress(self):
        """
        Memory-based stress test: Generate transactions with 20 threads until 80% memory.

        Steps:
        1. Detect system resources (total memory)
        2. Initialize metrics collector
        3. Spawn 20 threads to submit transactions in parallel
        4. Continue generating while memory < 80% AND time < 120 seconds
        5. Stop when memory reaches 80% OR 120 seconds elapsed
        6. Calculate throughput and export metrics
        """
        self.log_info("test_transaction_stress: Starting memory-based stress test...")

        # Validate node is running
        if len(self.nodes) == 0:
            raise RuntimeError("No nodes are running - cannot start stress test")

        node = self.nodes[0]
        self.log_info(f"Running stress test on single node with 20 parallel threads")

        # Configuration
        n_threads = 200
        memory_threshold_percent = 80  # Stop when memory >= 80% total
        max_duration_sec = 60  # Stop after 60 seconds maximum
        memory_check_interval = 0.5  # Check memory every 500ms
        results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')

        # Create results directory
        os.makedirs(results_dir, exist_ok=True)

        # Step 1: Detect system resources
        self.log_info("Step 1: Detecting system resources...")
        total_memory_mb = psutil.virtual_memory().total / (1024 * 1024)
        memory_threshold_mb = total_memory_mb * (memory_threshold_percent / 100.0)

        self.log_info(f"  Total memory: {total_memory_mb:.2f} MB")
        self.log_info(f"  Memory threshold: {memory_threshold_mb:.2f} MB ({memory_threshold_percent}%)")
        self.log_info(f"  Maximum duration: {max_duration_sec} seconds")
        self.log_info(f"  Thread count: {n_threads}")

        # Step 2: Initialize metrics collector
        self.log_info("Step 2: Initializing metrics collector...")
        collector = MetricsCollector(pid=node.process.pid, node_id="node0")
        aggregate = AggregateMetrics("transaction_stress_single_node")
        aggregate.add_collector(collector)

        # Set metadata
        aggregate.set_metadata(
            num_nodes=self.num_nodes,
            test_type="transaction_stress_memory_based",
            num_threads=n_threads,
            total_memory_mb=round(total_memory_mb, 2),
            memory_threshold_mb=round(memory_threshold_mb, 2),
            memory_threshold_percent=memory_threshold_percent,
            max_duration_sec=max_duration_sec
        )

        # Step 3: Start metrics collection
        self.log_info("Step 3: Starting metrics collection...")
        collector.start_collection()

        # Get initial mempool size
        chain_info = node.get_chain_info()
        initial_mempool = chain_info.get('mempool_size', 0)
        self.log_info(f"  Node0 initial mempool: {initial_mempool}")

        # Step 4: Multi-threaded transaction generation
        self.log_info(f"Step 4: Starting {n_threads}-threaded transaction generation...")

        # Shared state between threads
        f_stop_requested = threading.Event()  # Signal to stop all threads
        tx_counter_lock = threading.Lock()
        tx_counter = 0
        thread_tx_counts = [0] * n_threads  # Per-thread transaction counts
        thread_errors = [0] * n_threads  # Per-thread error counts

        def worker_thread(thread_id):
            """Worker thread that submits transactions until stop signal."""
            nonlocal tx_counter
            local_tx_count = 0
            local_error_count = 0

            while not f_stop_requested.is_set():
                # Generate transaction
                with tx_counter_lock:
                    tx_id = tx_counter
                    tx_counter += 1

                tx_data = {
                    "from": f"thread{thread_id}_wallet_{tx_id}",
                    "to": f"receiver_{tx_id % 100}",
                    "data": f"stress_tx_thread{thread_id}_{tx_id}",
                    "fee": 1
                }

                try:
                    success = node.create_transaction(tx_data)
                    if success:
                        local_tx_count += 1
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

            # Get current memory usage from collector
            current_memory_mb = collector.get_memory_avg()
            elapsed = time.time() - start_time

            # Check if time limit exceeded
            if elapsed >= max_duration_sec:
                self.log_info(f"  Time limit reached: {elapsed:.1f}s >= {max_duration_sec}s")
                stop_reason = "time_limit"
                f_stop_requested.set()
                break

            # Check if memory threshold exceeded
            if current_memory_mb >= memory_threshold_mb:
                self.log_info(f"  Memory threshold reached: {current_memory_mb:.1f}MB >= {memory_threshold_mb:.1f}MB")
                stop_reason = "memory_threshold"
                f_stop_requested.set()
                break

            # Progress logging every 10 seconds
            if time.time() - last_progress_time >= progress_interval:
                with tx_counter_lock:
                    current_tx_count = tx_counter
                rate = current_tx_count / elapsed if elapsed > 0 else 0
                memory_percent = (current_memory_mb / total_memory_mb) * 100
                self.log_info(
                    f"  Progress: {elapsed:.0f}s, {current_tx_count} txs ({rate:.1f} tx/sec), "
                    f"memory: {current_memory_mb:.1f}MB ({memory_percent:.1f}%)"
                )
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
        self.log_info(f"  Total errors: {total_errors}")

        # Step 5: Wait briefly for final processing
        self.log_info("Step 5: Waiting for final transaction processing...")
        time.sleep(2)

        # Step 6: Stop metrics collection
        self.log_info("Step 6: Stopping metrics collection...")
        collector.stop_collection()

        # Step 7: Collect final state
        self.log_info("Step 7: Collecting final state...")
        chain_info = node.get_chain_info()
        final_mempool = chain_info.get('mempool_size', 0)
        self.log_info(f"  Node0 final mempool: {final_mempool}")

        # Update collector counters
        n_sent = total_txs
        n_received = final_mempool - initial_mempool
        collector.set_transaction_count(sent=n_sent, received=n_received)

        # Calculate peak resources
        peak_memory = collector.get_memory_max()
        avg_memory = collector.get_memory_avg()
        peak_cpu = collector.get_cpu_max()
        avg_cpu = collector.get_cpu_avg()

        self.log_info(f"  Peak memory: {peak_memory:.1f} MB")
        self.log_info(f"  Average memory: {avg_memory:.1f} MB")
        self.log_info(f"  Peak CPU: {peak_cpu:.1f}%")
        self.log_info(f"  Average CPU: {avg_cpu:.1f}%")

        # Step 8: Export metrics
        self.log_info("Step 8: Exporting metrics...")
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        output_file = os.path.join(results_dir, f"transaction_stress_{timestamp}.json")

        # Add additional metadata
        aggregate.metadata['stress_stats'] = {
            'total_transactions_generated': total_txs,
            'per_thread_distribution': thread_tx_counts,
            'total_errors': total_errors,
            'actual_duration_sec': round(total_time, 2),
            'max_duration_sec': max_duration_sec,
            'stop_reason': stop_reason,
            'peak_memory_mb': round(peak_memory, 2),
            'avg_memory_mb': round(avg_memory, 2),
            'peak_cpu_percent': round(peak_cpu, 2),
            'avg_cpu_percent': round(avg_cpu, 2),
            'throughput_tx_per_sec': round(total_txs / total_time, 2) if total_time > 0 else 0,
            'mempool_growth': n_received
        }

        aggregate.export_json(output_file)
        aggregate.print_summary()

        self.log_info("test_transaction_stress: Memory-based stress test completed successfully!")


if __name__ == "__main__":
    # Ensure results directory exists
    results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')
    os.makedirs(results_dir, exist_ok=True)
    unittest.main()
