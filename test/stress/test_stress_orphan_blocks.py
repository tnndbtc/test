#!/usr/bin/env python3
"""
Orphan Block Propagation Stress Test for Blockweave

Memory-based stress test for orphan block handling using 200 parallel processes:
- Runs until memory reaches 80% OR 60 seconds elapsed
- 200 Python processes send orphan blocks in parallel via P2P
- Continues generating while memory < 80% total AND time < 60s
- Automatically stops when threshold reached
- Records CPU usage, memory consumption, test duration, and throughput
- Export metrics to JSON

Environment:
  PYTHONPATH=../functional (required)
  STRESS_TEST_RESULTS_DIR=results (optional, default ./results)

Run: python3 test_stress_orphan_blocks.py
"""

import sys
import time
import unittest
import os
import psutil
import multiprocessing
from test_framework import TestFramework
from test_framework.block_utils import BlockUtils
from test_framework.p2p_utils import MessageType, P2PMessage, P2PConnection
from metrics_collector import MetricsCollector, AggregateMetrics


def worker_process(process_id, node_p2p_ports, genesis_hash, f_stop_requested, node_idx_counter, node_idx_lock,
                   per_node_locks_dict, counts_lock, orphan_stats_lock,
                   process_block_counts, process_errors, per_node_block_counts,
                   per_node_height_counters, per_node_last_block_hash, peak_orphan_size_value):
    """Worker process that sends orphan block pairs until stop signal."""
    local_block_count = 0
    local_error_count = 0

    # Staggered startup: each process waits a bit to avoid connection burst
    startup_delay = (process_id * 0.05) % 2.0  # Stagger over 2 seconds max
    time.sleep(startup_delay)

    while not f_stop_requested.is_set():
        # Select target node with round-robin
        with node_idx_lock:
            target_node_idx = node_idx_counter.value % len(node_p2p_ports)
            node_idx_counter.value += 1

        target_p2p_port = node_p2p_ports[target_node_idx]

        # Get the lock for this specific node
        node_lock_key = f"node_{target_node_idx}"
        node_lock = per_node_locks_dict[node_lock_key]

        # Lock this specific node to ensure chain integrity
        with node_lock:
            # Get next two heights for this node (parent and child)
            heights = list(per_node_height_counters)
            parent_height = heights[target_node_idx]
            child_height = parent_height + 1

            # Update the height counter
            heights[target_node_idx] += 2
            per_node_height_counters[:] = heights

            # Get the hash to build on
            hashes = list(per_node_last_block_hash)
            prev_hash = hashes[target_node_idx]

            try:
                # Create parent block
                parent_block = BlockUtils.create_block(
                    prev_hash_hex=prev_hash,
                    height=parent_height,
                    miner=f"stress_process{process_id}_parent_h{parent_height}",
                    mine=True
                )

                # Create child block
                child_block = BlockUtils.create_block(
                    prev_hash_hex=parent_block['hash_hex'],
                    height=child_height,
                    miner=f"stress_process{process_id}_child_h{child_height}",
                    mine=True
                )

                # Send child first (becomes orphan), then parent (triggers resolution)
                try:
                    with P2PConnection("127.0.0.1", target_p2p_port, timeout=5) as conn:
                        # Send child block (becomes orphan)
                        child_serialized = BlockUtils.serialize_block(child_block)
                        child_msg = P2PMessage(MessageType.BLOCK, child_serialized)
                        conn.send_message(child_msg)

                        # Small delay to allow orphan queue to update
                        time.sleep(0.05)

                        # Send parent block (triggers resolution)
                        parent_serialized = BlockUtils.serialize_block(parent_block)
                        parent_msg = P2PMessage(MessageType.BLOCK, parent_serialized)
                        conn.send_message(parent_msg)

                    local_block_count += 1

                    # Update last block hash to the child
                    hashes[target_node_idx] = child_block['hash_hex']
                    per_node_last_block_hash[:] = hashes

                    # Update block count
                    with counts_lock:
                        counts = list(per_node_block_counts)
                        counts[target_node_idx] += 1
                        per_node_block_counts[:] = counts

                except Exception as e:
                    local_error_count += 1
                    print(f"[Process {process_id}] P2P connection error to port {target_p2p_port}: {str(e)}")

            except Exception as e:
                local_error_count += 1
                print(f"[Process {process_id}] Block creation error: {str(e)}")
                # Add small delay on error to avoid rapid retry storms
                time.sleep(0.5)

        # Small delay between pairs
        time.sleep(0.3)

    # Store process-local counts
    process_block_counts[process_id] = local_block_count
    process_errors[process_id] = local_error_count


class OrphanBlocksStressTest(TestFramework):
    """Stress test for orphan block handling."""

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
        Node0 <-> Node1, Node2, Node3
        """
        self.log_info("setup: Establishing mesh topology...")

        # Connect node0 to all other nodes
        node0 = self.nodes[0]
        for i in range(1, len(self.nodes)):
            node_i = self.nodes[i]

            self.log_info(f"setup: Connecting node0 <-> node{i}...")
            if node0.connect_to_peer(node_i, wait=True):
                self.log_info(f"setup: node0 <-> node{i} connected")
            else:
                self.log_error(f"setup: FAILED to connect node0 <-> node{i}")

        # Wait for connections to stabilize
        time.sleep(2)

        # Log peer counts
        for i, node in enumerate(self.nodes):
            peer_info = node.get_peer_info()
            total = peer_info.get('total_peers', 0)
            self.log_info(f"setup: Node{i} has {total} peers")

        self.log_info("setup: Mesh topology established")

    def test_orphan_blocks_stress(self):
        """
        Memory-based stress test: Send orphan blocks with 200 processes until threshold reached.

        Steps:
        1. Detect system resources (total memory)
        2. Initialize metrics collectors
        3. Create genesis block
        4. Spawn 200 processes to send orphan/parent block pairs in parallel
        5. Continue generating while memory < 80% AND time < 60 seconds
        6. Stop when memory reaches 80% OR 60 seconds elapsed
        7. Calculate throughput and export metrics
        """
        self.log_info("test_orphan_blocks_stress: Starting memory-based stress test...")

        # Validate nodes are running
        if len(self.nodes) == 0:
            raise RuntimeError("No nodes are running - cannot start stress test")

        self.log_info(f"Running stress test with {len(self.nodes)} nodes")

        # Configuration
        n_processes = 200
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
        self.log_info(f"  Memory threshold: {memory_threshold_mb:.2f} MB (stop when >= {memory_threshold_mb:.2f} MB)")
        self.log_info(f"  Maximum duration: {max_duration_sec} seconds")
        self.log_info(f"  Process count: {n_processes}")
        self.log_info(f"  Test runs until memory reaches {memory_threshold_percent}% OR {max_duration_sec}s elapsed")

        # Step 2: Initialize metrics collectors
        self.log_info("Step 2: Initializing metrics collectors...")
        collectors = []
        aggregate = AggregateMetrics("orphan_blocks_stress")

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
            test_type="orphan_blocks_memory_based",
            num_processes=n_processes,
            total_memory_mb=round(total_memory_mb, 2),
            memory_threshold_mb=round(memory_threshold_mb, 2),
            memory_threshold_percent=memory_threshold_percent,
            max_duration_sec=max_duration_sec
        )

        # Get initial state
        node0 = self.nodes[0]
        initial_chain_info = node0.get_chain_info()
        initial_orphan_size = initial_chain_info.get('orphan_blocks_size', 0)
        initial_blocks = initial_chain_info.get('blocks', 0)

        self.log_info(f"  Initial orphan queue size: {initial_orphan_size}")
        self.log_info(f"  Initial blocks: {initial_blocks}")

        # Step 3: Create genesis block
        self.log_info("Step 3: Creating genesis block...")
        genesis = BlockUtils.create_genesis_block()
        self.log_info(f"  Genesis hash: {genesis['hash_hex'][:16]}...")

        # Step 4: Start metrics collection
        self.log_info("Step 4: Starting metrics collection...")
        for collector in collectors:
            collector.start_collection()

        # Get node P2P ports for worker processes
        node_p2p_ports = [node.p2p_port for node in self.nodes]

        # Step 5: Multi-process orphan block sending with time limit
        self.log_info(f"Step 5: Starting {n_processes}-process orphan block generation until {memory_threshold_percent}% memory or {max_duration_sec}s...")

        # Create multiprocessing Manager for shared state
        manager = multiprocessing.Manager()

        # Shared state between processes
        f_stop_requested = manager.Event()  # Signal to stop all processes
        node_idx_lock = manager.Lock()
        counts_lock = manager.Lock()  # Lock for per_node_block_counts monitoring
        orphan_stats_lock = manager.Lock()
        node_idx_counter = manager.Value('i', 0)  # Round-robin node selection
        process_block_counts = manager.list([0] * n_processes)  # Per-process block counts (pairs)
        process_errors = manager.list([0] * n_processes)  # Per-process error counts
        per_node_block_counts = manager.list([0] * len(self.nodes))  # Track blocks sent per node
        per_node_height_counters = manager.list([1] * len(self.nodes))  # Per-node height counters (start at 1)
        per_node_last_block_hash = manager.list([genesis['hash_hex']] * len(self.nodes))  # Track last block hash per node
        peak_orphan_size_value = manager.Value('i', 0)  # Track peak orphan queue size

        # Create per-node locks using a dict
        per_node_locks_dict = manager.dict()
        for i in range(len(self.nodes)):
            per_node_locks_dict[f"node_{i}"] = manager.Lock()

        # Start worker processes
        start_time = time.time()
        processes = []
        self.log_info(f"  Spawning {n_processes} worker processes...")

        for i in range(n_processes):
            p = multiprocessing.Process(
                target=worker_process,
                args=(i, node_p2p_ports, genesis['hash_hex'], f_stop_requested, node_idx_counter, node_idx_lock,
                      per_node_locks_dict, counts_lock, orphan_stats_lock,
                      process_block_counts, process_errors, per_node_block_counts,
                      per_node_height_counters, per_node_last_block_hash, peak_orphan_size_value),
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
                with counts_lock:
                    node_distribution = list(per_node_block_counts)
                    current_block_count = sum(per_node_block_counts)
                with orphan_stats_lock:
                    current_peak_orphan = peak_orphan_size_value.value
                rate = current_block_count / elapsed if elapsed > 0 else 0
                memory_percent = (avg_memory / total_memory_mb) * 100
                self.log_info(f"  Progress: {elapsed:.0f}s, "
                              f"{current_block_count} pairs ({rate:.1f} pairs/sec), "
                              f"memory: {avg_memory:.1f}MB ({memory_percent:.1f}%), "
                              f"peak orphans: {current_peak_orphan}, "
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
        total_pairs = sum(per_node_block_counts)
        total_blocks = total_pairs * 2  # Each pair has 2 blocks
        total_errors = sum(process_errors)

        self.log_info(f"Step 5 complete: {total_pairs} orphan pairs ({total_blocks} blocks) sent in {total_time:.2f}s")
        self.log_info(f"  Per-process distribution: {list(process_block_counts)}")
        self.log_info(f"  Per-node distribution: {list(per_node_block_counts)}")
        self.log_info(f"  Total errors: {total_errors}")

        # Wait briefly for final propagation
        self.log_info("  Waiting for final orphan resolution...")
        time.sleep(2)

        # Step 6: Stop metrics collection
        self.log_info("Step 6: Stopping metrics collection...")
        for collector in collectors:
            collector.stop_collection()

        # Step 7: Collect final state and update counters
        self.log_info("Step 7: Collecting final state...")
        final_chain_info = node0.get_chain_info()
        final_orphan_size = final_chain_info.get('orphan_blocks_size', 0)
        final_blocks = final_chain_info.get('blocks', 0)

        self.log_info(f"  Final orphan queue size: {final_orphan_size}")
        self.log_info(f"  Final blocks: {final_blocks}")

        # Update collector counters and sum total received across all nodes
        total_received = 0
        per_node_counts_list = list(per_node_block_counts)
        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', 0)
            n_sent = per_node_counts_list[i] * 2  # Each pair has 2 blocks
            n_received = blocks - initial_blocks
            total_received += n_received  # Sum across all nodes
            collectors[i].set_block_count(mined=0, received=n_received)
            self.log_info(f"  Node{i} blocks: {blocks} (received {n_received})")

        # Calculate peak resources
        peak_memory = max(c.get_memory_max() for c in collectors)
        avg_memory = sum(c.get_memory_avg() for c in collectors) / len(collectors)
        peak_cpu = max(c.get_cpu_max() for c in collectors)
        avg_cpu = sum(c.get_cpu_avg() for c in collectors) / len(collectors)
        peak_orphan_size = peak_orphan_size_value.value

        self.log_info(f"  Peak memory: {peak_memory:.1f} MB")
        self.log_info(f"  Average memory: {avg_memory:.1f} MB")
        self.log_info(f"  Peak CPU: {peak_cpu:.1f}%")
        self.log_info(f"  Average CPU: {avg_cpu:.1f}%")
        self.log_info(f"  Peak orphan queue size: {peak_orphan_size}")

        # Step 8: Export metrics
        self.log_info("Step 8: Exporting metrics...")
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        output_file = os.path.join(results_dir, f"orphan_blocks_{timestamp}.json")

        # Add additional metadata with stress_stats
        aggregate.metadata['stress_stats'] = {
            'total_orphan_pairs_sent': total_pairs,
            'total_blocks_sent': total_blocks,
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
            'throughput_pairs_per_sec': round(total_pairs / total_time, 2) if total_time > 0 else 0,
            'throughput_blocks_per_sec': round(total_blocks / total_time, 2) if total_time > 0 else 0,
            'initial_orphan_size': initial_orphan_size,
            'peak_orphan_size': peak_orphan_size,
            'final_orphan_size': final_orphan_size,
            'orphans_resolved': max(0, peak_orphan_size - final_orphan_size),
            'total_blocks_propagated': total_received
        }

        aggregate.export_json(output_file)
        aggregate.print_summary()

        self.log_info("test_orphan_blocks_stress: Memory-based stress test completed successfully!")


if __name__ == "__main__":
    # Set multiprocessing start method for better compatibility
    multiprocessing.set_start_method('spawn', force=True)

    # Ensure results directory exists
    results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')
    os.makedirs(results_dir, exist_ok=True)
    unittest.main()
