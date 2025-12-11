#!/usr/bin/env python3
"""
Single Node Block Creation Stress Test for Blockweave

Memory-based stress test that creates blocks using 200 parallel processes targeting a single node:
- Runs until memory reaches 80% OR 60 seconds elapsed
- 200 Python processes create blocks in parallel on the same node
- Continues generating while memory < 80% total AND time < 60s
- Automatically stops when threshold reached
- Records CPU usage, memory consumption, test duration, and throughput
- Export metrics to JSON

Environment: PYTHONPATH=../functional
Run: python3 test_stress_block.py
"""

import sys
import time
import unittest
import os
import psutil
import multiprocessing
import requests
from test_framework import TestFramework
from metrics_collector import MetricsCollector, AggregateMetrics


def worker_process(process_id, rest_port, credentials, f_stop_requested, block_counter, block_counter_lock,
                   process_block_counts, process_errors):
    """Worker process that creates transaction and mines blocks until stop signal."""
    local_block_count = 0
    local_error_count = 0

    # Base URL for REST API
    base_url = f"http://127.0.0.1:{rest_port}"

    while not f_stop_requested.is_set():
        # Get block ID atomically
        with block_counter_lock:
            block_id = block_counter.value
            block_counter.value += 1

        try:
            # Create a transaction first
            tx_data = {
                "from": f"process{process_id}_wallet_{block_id}",
                "to": f"receiver_{block_id % 20}",
                "data": f"tx_process{process_id}_block{block_id}",
                "fee": 1
            }
            response = requests.post(f"{base_url}/transaction", json=tx_data, timeout=5)

            # Then trigger mining on node0
            response = requests.post(
                f"{base_url}/rpc/minetrigger",
                auth=credentials,
                timeout=5
            )
            if response.status_code == 200:
                local_block_count += 1
            else:
                local_error_count += 1
                self.log_error("trigger mining response error: " + str(response))
        except Exception as e:
            local_error_count += 1
            self.log_error("etrigger mining exception: " + str(e))

        # Small delay between mining attempts
        time.sleep(0.1)

    # Store process-local counts
    process_block_counts[process_id] = local_block_count
    process_errors[process_id] = local_error_count


class SingleNodeBlockStressTest(TestFramework):
    """Stress test for block creation on a single node with 200 processes."""

    # Only 1 node for this stress test
    num_nodes = 1

    def setup(self):
        """
        Setup test environment - no peer connections needed for single node test.
        """
        # Framework already created and started node via num_nodes = 1
        self.node = self.nodes[0]
        self.log_info("setup: Single node ready for stress test")

    def test_single_node_block_stress(self):
        """
        Memory-based stress test: Create blocks with 200 processes targeting 1 node until threshold reached.

        Steps:
        1. Detect system resources (total memory)
        2. Initialize metrics collector for the node
        3. Start metrics collection
        4. Spawn 200 processes to create blocks in parallel on the same node
           (each process creates a transaction before each mining attempt)
        5. Continue generating while memory < 80% AND time < 60 seconds
        6. Stop when memory reaches 80% OR 60 seconds elapsed
        7. Calculate throughput and export metrics
        """
        self.log_info("test_single_node_block_stress: Starting memory-based stress test...")

        # Validate node is running
        if len(self.nodes) == 0:
            raise RuntimeError("No nodes are running - cannot start stress test")

        self.log_info(f"Running stress test with {len(self.nodes)} node")

        # Configuration
        n_processes = 200
        memory_threshold_percent = 80  # Stop when memory >= 80% total
        max_duration_sec = 60  # Stop after 60 seconds maximum
        memory_check_interval = 0.5  # Check memory every 500ms
        results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'test_results')

        # Create results directory
        os.makedirs(results_dir, exist_ok=True)

        # Step 1: Detect system resources
        self.log_info("Step 1: Detecting system resources...")
        total_memory_mb = psutil.virtual_memory().total / (1024 * 1024)
        memory_threshold_mb = total_memory_mb * (memory_threshold_percent / 100.0)

        self.log_info(f"  Total memory: {total_memory_mb:.2f} MB")
        self.log_info(f"  Memory threshold: {memory_threshold_mb:.2f} MB (stop when >= {memory_threshold_mb:.2f} MB)")
        self.log_info(f"  Maximum duration: {max_duration_sec} seconds")
        self.log_info(f"  Process count: {n_processes}")
        self.log_info(f"  Test runs until memory reaches {memory_threshold_percent}% OR {max_duration_sec}s elapsed")

        # Step 2: Initialize metrics collector
        self.log_info("Step 2: Initializing metrics collector...")
        collectors = []
        aggregate = AggregateMetrics("single_node_block_stress")

        if self.node.process and self.node.process.pid:
            collector = MetricsCollector(pid=self.node.process.pid, node_id=f"node0")
            collectors.append(collector)
            aggregate.add_collector(collector)
            self.log_info(f"  Collector initialized for node0 (PID: {self.node.process.pid})")
        else:
            raise RuntimeError(f"Node0 process not running")

        # Set metadata
        aggregate.set_metadata(
            num_nodes=len(self.nodes),
            test_type="single_node_block_stress",
            num_processes=n_processes,
            total_memory_mb=round(total_memory_mb, 2),
            memory_threshold_mb=round(memory_threshold_mb, 2),
            memory_threshold_percent=memory_threshold_percent,
            max_duration_sec=max_duration_sec
        )

        # Get initial block count
        chain_info = self.node.get_chain_info()
        initial_blocks = chain_info.get('blocks', 0)
        self.log_info(f"  Node0 initial blocks: {initial_blocks}")

        # Get node REST port and RPC credentials
        rest_port = self.node.port
        credentials = self.node.get_cookie_credentials()
        if not credentials:
            raise RuntimeError("Failed to get RPC credentials from node")

        # Step 3: Start metrics collection
        self.log_info("Step 3: Starting metrics collection...")
        for collector in collectors:
            collector.start_collection()

        # Step 4: Multi-process block creation with time limit
        self.log_info(f"Step 4: Starting {n_processes}-process block creation until {memory_threshold_percent}% memory or {max_duration_sec}s...")

        # Create multiprocessing Manager for shared state
        manager = multiprocessing.Manager()

        # Shared state between processes
        f_stop_requested = manager.Event()  # Signal to stop all processes
        block_counter_lock = manager.Lock()
        block_counter = manager.Value('i', 0)  # Shared counter
        process_block_counts = manager.list([0] * n_processes)  # Per-process block counts
        process_errors = manager.list([0] * n_processes)  # Per-process error counts

        # Start worker processes
        start_time = time.time()
        processes = []
        self.log_info(f"  Spawning {n_processes} worker processes...")

        for i in range(n_processes):
            p = multiprocessing.Process(
                target=worker_process,
                args=(i, rest_port, credentials, f_stop_requested, block_counter, block_counter_lock,
                      process_block_counts, process_errors),
                daemon=True
            )
            p.start()
            processes.append(p)

        self.log_info(f"  All {n_processes} processes started")

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

            # Get average memory from collector
            avg_memory = collector.get_memory_avg()

            # Check if memory threshold exceeded
            if avg_memory >= memory_threshold_mb:
                self.log_info(f"  Memory threshold reached: {avg_memory:.1f}MB >= {memory_threshold_mb:.1f}MB")
                stop_reason = "memory_threshold"
                f_stop_requested.set()
                break

            # Progress logging every 10 seconds
            if time.time() - last_progress_time >= progress_interval:
                with block_counter_lock:
                    current_block_count = block_counter.value
                rate = current_block_count / elapsed if elapsed > 0 else 0
                memory_percent = (avg_memory / total_memory_mb) * 100
                self.log_info(f"  Progress: {elapsed:.0f}s, "
                              f"{current_block_count} blocks ({rate:.1f} blocks/sec), "
                              f"memory: {avg_memory:.1f}MB ({memory_percent:.1f}%)")
                last_progress_time = time.time()

        # Wait for all processes to finish (with total timeout, not per-process)
        self.log_info("  Waiting for processes to complete...")
        deadline = time.time() + 5  # 5 second total timeout
        for p in processes:
            remaining = max(0, deadline - time.time())
            p.join(timeout=remaining)

        # Terminate any processes still alive
        for p in processes:
            if p.is_alive():
                p.terminate()
                p.join(timeout=1)

        total_time = time.time() - start_time
        total_blocks = block_counter.value
        total_errors = sum(process_errors)

        self.log_info(f"Step 4 complete: {total_blocks} blocks created in {total_time:.2f}s")
        self.log_info(f"  Per-process distribution: {list(process_block_counts)}")
        self.log_info(f"  Total errors: {total_errors}")

        # Wait briefly for final propagation
        self.log_info("  Waiting for final block propagation...")
        time.sleep(2)

        # Step 5: Stop metrics collection
        self.log_info("Step 5: Stopping metrics collection...")
        for collector in collectors:
            collector.stop_collection()

        # Step 6: Collect final state and update counters
        self.log_info("Step 6: Collecting final state...")
        chain_info = self.node.get_chain_info()
        final_blocks = chain_info.get('blocks', 0)

        self.log_info(f"  Node0 final blocks: {final_blocks}")

        # Update collector counters
        n_mined = total_blocks
        n_received = final_blocks - initial_blocks
        collector.set_block_count(mined=n_mined, received=n_received)

        # Calculate peak resources
        peak_memory = collector.get_memory_max()
        avg_memory = collector.get_memory_avg()
        peak_cpu = collector.get_cpu_max()
        avg_cpu = collector.get_cpu_avg()

        self.log_info(f"  Peak memory: {peak_memory:.1f} MB")
        self.log_info(f"  Average memory: {avg_memory:.1f} MB")
        self.log_info(f"  Peak CPU: {peak_cpu:.1f}%")
        self.log_info(f"  Average CPU: {avg_cpu:.1f}%")

        # Step 7: Export metrics
        self.log_info("Step 7: Exporting metrics...")
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        output_file = os.path.join(results_dir, f"block_{timestamp}.json")

        # Add additional metadata with stress_stats
        aggregate.metadata['stress_stats'] = {
            'total_blocks_created': total_blocks,
            'per_process_distribution': list(process_block_counts),
            'total_errors': total_errors,
            'actual_duration_sec': round(total_time, 2),
            'max_duration_sec': max_duration_sec,
            'stop_reason': stop_reason,
            'peak_cpu_percent': round(peak_cpu, 2),
            'peak_memory_mb': round(peak_memory, 2),
            'avg_cpu_percent': round(avg_cpu, 2),
            'avg_memory_mb': round(avg_memory, 2),
            'throughput_blocks_per_sec': round(total_blocks / total_time, 2) if total_time > 0 else 0,
            'total_blocks_received': n_received
        }

        aggregate.export_json(output_file)
        aggregate.print_summary()

        self.log_info("test_single_node_block_stress: Memory-based stress test completed successfully!")


if __name__ == "__main__":
    # Set multiprocessing start method for better compatibility
    multiprocessing.set_start_method('spawn', force=True)

    # Ensure results directory exists
    results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'test_results')
    os.makedirs(results_dir, exist_ok=True)
    unittest.main()
