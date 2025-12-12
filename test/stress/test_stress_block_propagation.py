#!/usr/bin/env python3
"""
Block Propagation Stress Test for Blockweave

Memory-based stress test that mines blocks using 200 parallel processes:
- Runs until memory reaches 80% OR 60 seconds elapsed
- 200 Python processes mine blocks in parallel across all nodes
- Continues generating while memory < 80% total AND time < 60s
- Automatically stops when threshold reached
- Records CPU usage, memory consumption, test duration, and throughput
- Export metrics to JSON

Environment: PYTHONPATH=../functional
Run: python3 test_stress_block_propagation.py
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


def worker_process(process_id, node_ports, node_credentials, f_stop_requested, block_counter, block_counter_lock,
                   node_idx_counter, node_idx_lock, per_node_block_counts, per_node_lock,
                   process_block_counts, process_errors):
    """Worker process that creates transactions and mines blocks until stop signal."""
    local_block_count = 0
    local_error_count = 0

    while not f_stop_requested.is_set():
        # Get block ID and target node atomically
        with block_counter_lock:
            block_id = block_counter.value
            block_counter.value += 1

        with node_idx_lock:
            target_node_idx = node_idx_counter.value % len(node_ports)
            node_idx_counter.value += 1

        rest_port = node_ports[target_node_idx]
        credentials = node_credentials[target_node_idx]
        base_url = f"http://127.0.0.1:{rest_port}"

        try:
            # Create a transaction first
            tx_data = {
                "from": f"process{process_id}_wallet_{block_id}",
                "to": f"receiver_{block_id % 20}",
                "data": f"tx_process{process_id}_block{block_id}",
                "fee": 1
            }
            tx_response = requests.post(f"{base_url}/rpc/transaction", json=tx_data, auth=credentials, timeout=5)

            # Then trigger mining on the selected node
            mine_response = requests.post(
                f"{base_url}/rpc/minetrigger",
                auth=credentials,
                timeout=5
            )

            if mine_response.status_code == 200:
                local_block_count += 1
                with per_node_lock:
                    counts = list(per_node_block_counts)
                    counts[target_node_idx] += 1
                    per_node_block_counts[:] = counts
            else:
                local_error_count += 1
                print(f"[Process {process_id}] Mining response error on node{target_node_idx}: {mine_response.status_code}")

        except Exception as e:
            local_error_count += 1
            print(f"[Process {process_id}] Mining exception on node{target_node_idx}: {str(e)}")

        # Small delay between mining attempts
        time.sleep(0.1)

    # Store process-local counts
    process_block_counts[process_id] = local_block_count
    process_errors[process_id] = local_error_count


class BlockPropagationStressTest(TestFramework):
    """Stress test for block propagation across multiple nodes."""

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
        Setup test environment - establish relay topology.

        Topology (linear relay):
        Node0 <-> Node1 <-> Node2 <-> Node3
        """
        self.log_info("setup: Establishing relay topology...")

        # Connect nodes in linear chain for relay testing
        for i in range(len(self.nodes) - 1):
            node_i = self.nodes[i]
            node_j = self.nodes[i + 1]

            self.log_info(f"setup: Connecting node{i} <-> node{i+1}...")
            if node_i.connect_to_peer(node_j, wait=True):
                self.log_info(f"setup: node{i} <-> node{i+1} connected")
            else:
                self.log_error(f"setup: FAILED to connect node{i} <-> node{i+1}")

        # Wait for connections to stabilize
        time.sleep(2)

        # Log peer counts
        for i, node in enumerate(self.nodes):
            peer_info = node.get_peer_info()
            total = peer_info.get('total_peers', 0)
            self.log_info(f"setup: Node{i} has {total} peers")

        self.log_info("setup: Relay topology established")

    def test_block_propagation_stress(self):
        """
        Memory-based stress test: Mine blocks with 200 processes until threshold reached.

        Steps:
        1. Detect system resources (total memory)
        2. Initialize metrics collectors for all nodes
        3. Pre-fill mempools with transactions
        4. Spawn 200 processes to mine blocks in parallel across all nodes
        5. Continue generating while memory < 80% AND time < 60 seconds
        6. Stop when memory reaches 80% OR 60 seconds elapsed
        7. Calculate throughput and export metrics
        """
        self.log_info("test_block_propagation_stress: Starting memory-based stress test...")

        # Validate nodes are running
        if len(self.nodes) == 0:
            raise RuntimeError("No nodes are running - cannot start stress test")

        self.log_info(f"Running stress test with {len(self.nodes)} nodes")

        # Configuration
        n_processes = 200
        memory_threshold_percent = 80  # Stop when memory >= 80% total
        max_duration_sec = 60  # Stop after 60 seconds maximum
        memory_check_interval = 0.5  # Check memory every 500ms
        n_prefill_transactions = 100  # Pre-fill each node with transactions
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
        self.log_info(f"  Process count: {n_processes}")
        self.log_info(f"  Test runs until memory reaches {memory_threshold_percent}% OR {max_duration_sec}s elapsed")

        # Step 2: Initialize metrics collectors
        self.log_info("Step 2: Initializing metrics collectors...")
        collectors = []
        aggregate = AggregateMetrics("block_propagation_stress")

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
            test_type="block_propagation_memory_based",
            num_processes=n_processes,
            total_memory_mb=round(total_memory_mb, 2),
            memory_threshold_mb=round(memory_threshold_mb, 2),
            memory_threshold_percent=memory_threshold_percent,
            max_duration_sec=max_duration_sec
        )

        # Get initial block counts
        initial_blocks = []
        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', 0)
            initial_blocks.append(blocks)
            self.log_info(f"  Node{i} initial blocks: {blocks}")

        # Step 3: Pre-fill all nodes' mempools with transactions
        self.log_info(f"Step 3: Pre-filling all nodes' mempools with {n_prefill_transactions} transactions each...")
        for node_idx, node in enumerate(self.nodes):
            for tx_idx in range(n_prefill_transactions):
                tx_data = {
                    "from": f"node{node_idx}_wallet_{tx_idx}",
                    "to": f"receiver_{tx_idx % 20}",
                    "data": f"prefill_tx_node{node_idx}_{tx_idx}",
                    "fee": 1
                }

                try:
                    node.create_transaction(tx_data)
                except Exception as e:
                    pass  # Ignore errors during prefill

            # Log progress
            self.log_info(f"  Node{node_idx} mempool pre-filled with {n_prefill_transactions} transactions")

        # Wait for transactions to settle
        time.sleep(2)
        self.log_info(f"Step 3 complete: All mempools pre-filled")

        # Step 4: Start metrics collection
        self.log_info("Step 4: Starting metrics collection...")
        for collector in collectors:
            collector.start_collection()

        # Get node ports and credentials for worker processes
        node_ports = [node.port for node in self.nodes]
        node_credentials = []
        for i, node in enumerate(self.nodes):
            creds = node.get_cookie_credentials()
            if not creds:
                raise RuntimeError(f"Failed to get RPC credentials from node{i}")
            node_credentials.append(creds)

        # Step 5: Multi-process block mining with time limit
        self.log_info(f"Step 5: Starting {n_processes}-process block mining until {memory_threshold_percent}% memory or {max_duration_sec}s...")

        # Create multiprocessing Manager for shared state
        manager = multiprocessing.Manager()

        # Shared state between processes
        f_stop_requested = manager.Event()  # Signal to stop all processes
        block_counter_lock = manager.Lock()
        node_idx_lock = manager.Lock()
        per_node_lock = manager.Lock()
        block_counter = manager.Value('i', 0)  # Shared counter
        node_idx_counter = manager.Value('i', 0)  # Round-robin node selection
        process_block_counts = manager.list([0] * n_processes)  # Per-process block counts
        process_errors = manager.list([0] * n_processes)  # Per-process error counts
        per_node_block_counts = manager.list([0] * len(self.nodes))  # Track blocks mined per node

        # Start worker processes
        start_time = time.time()
        processes = []
        self.log_info(f"  Spawning {n_processes} worker processes...")

        for i in range(n_processes):
            p = multiprocessing.Process(
                target=worker_process,
                args=(i, node_ports, node_credentials, f_stop_requested, block_counter, block_counter_lock,
                      node_idx_counter, node_idx_lock, per_node_block_counts, per_node_lock,
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
                with block_counter_lock:
                    current_block_count = block_counter.value
                with per_node_lock:
                    node_distribution = list(per_node_block_counts)
                rate = current_block_count / elapsed if elapsed > 0 else 0
                memory_percent = (avg_memory / total_memory_mb) * 100
                self.log_info(f"  Progress: {elapsed:.0f}s, "
                              f"{current_block_count} blocks ({rate:.1f} blocks/sec), "
                              f"memory: {avg_memory:.1f}MB ({memory_percent:.1f}%), "
                              f"distribution: {node_distribution}")
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

        self.log_info(f"Step 5 complete: {total_blocks} blocks mined in {total_time:.2f}s")
        self.log_info(f"  Per-process distribution: {list(process_block_counts)}")
        self.log_info(f"  Per-node distribution: {list(per_node_block_counts)}")
        self.log_info(f"  Total errors: {total_errors}")

        # Wait briefly for final propagation
        self.log_info("  Waiting for final block propagation...")
        time.sleep(2)

        # Step 6: Stop metrics collection
        self.log_info("Step 6: Stopping metrics collection...")
        for collector in collectors:
            collector.stop_collection()

        # Step 7: Collect final state and update counters
        self.log_info("Step 7: Collecting final state...")
        total_received = 0
        final_blocks = []
        per_node_counts_list = list(per_node_block_counts)
        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', 0)
            final_blocks.append(blocks)

            self.log_info(f"  Node{i} final blocks: {blocks}")

            # Update collector counters
            n_mined = per_node_counts_list[i]
            n_received = blocks - initial_blocks[i]
            total_received += n_received
            collectors[i].set_block_count(mined=n_mined, received=n_received)

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
        output_file = os.path.join(results_dir, f"block_propagation_{timestamp}.json")

        # Add additional metadata with stress_stats
        aggregate.metadata['stress_stats'] = {
            'total_blocks_mined': total_blocks,
            'per_process_distribution': list(process_block_counts),
            'per_node_distribution': per_node_counts_list,
            'total_errors': total_errors,
            'actual_duration_sec': round(total_time, 2),
            'max_duration_sec': max_duration_sec,
            'stop_reason': stop_reason,
            'peak_cpu_percent': round(peak_cpu, 2),
            'peak_memory_mb': round(peak_memory, 2),
            'avg_cpu_percent': round(avg_cpu, 2),
            'avg_memory_mb': round(avg_memory, 2),
            'throughput_blocks_per_sec': round(total_blocks / total_time, 2) if total_time > 0 else 0,
            'total_blocks_propagated': total_received
        }

        aggregate.export_json(output_file)
        aggregate.print_summary()

        self.log_info("test_block_propagation_stress: Memory-based stress test completed successfully!")


if __name__ == "__main__":
    # Set multiprocessing start method for better compatibility
    multiprocessing.set_start_method('spawn', force=True)

    # Ensure results directory exists
    results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')
    os.makedirs(results_dir, exist_ok=True)
    unittest.main()
