#!/usr/bin/env python3
"""
Orphan Block Propagation Stress Test for Blockweave

Memory-based stress test for orphan block handling using 20 parallel threads:
- Runs until memory reaches 80% OR 60 seconds elapsed
- 20 Python threads send orphan blocks in parallel via P2P
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
import threading
from test_framework import TestFramework
from test_framework.block_utils import BlockUtils
from test_framework.p2p_utils import MessageType, P2PMessage, P2PConnection
from metrics_collector import MetricsCollector, AggregateMetrics


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
        Memory-based stress test: Send orphan blocks with 20 threads until threshold reached.

        Steps:
        1. Detect system resources (total memory)
        2. Initialize metrics collectors
        3. Create genesis block
        4. Spawn 20 threads to send orphan/parent block pairs in parallel
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
        self.log_info(f"  Memory threshold: {memory_threshold_mb:.2f} MB (stop when >= {memory_threshold_mb:.2f} MB)")
        self.log_info(f"  Maximum duration: {max_duration_sec} seconds")
        self.log_info(f"  Thread count: {n_threads}")
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
            num_threads=n_threads,
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

        # Step 5: Multi-threaded orphan block sending with time limit
        self.log_info(f"Step 5: Starting {n_threads}-threaded orphan block generation until {memory_threshold_percent}% memory or {max_duration_sec}s...")

        # Shared state between threads
        f_stop_requested = threading.Event()  # Signal to stop all threads
        node_idx_lock = threading.Lock()
        per_node_locks = [threading.Lock() for _ in range(len(self.nodes))]  # Per-node locks for chain integrity
        counts_lock = threading.Lock()  # Lock for per_node_block_counts monitoring
        orphan_stats_lock = threading.Lock()
        node_idx = 0  # Round-robin node selection
        thread_block_counts = [0] * n_threads  # Per-thread block counts (pairs)
        thread_errors = [0] * n_threads  # Per-thread error counts
        per_node_block_counts = [0] * len(self.nodes)  # Track blocks sent per node
        per_node_height_counters = [1] * len(self.nodes)  # Per-node height counters (start at 1)
        per_node_last_block_hash = [genesis['hash_hex']] * len(self.nodes)  # Track last block hash per node
        peak_orphan_size = 0  # Track peak orphan queue size

        def worker_thread(thread_id):
            """Worker thread that sends orphan block pairs until stop signal."""
            nonlocal node_idx, peak_orphan_size
            local_block_count = 0
            local_error_count = 0

            # Staggered startup: each thread waits a bit to avoid connection burst
            # With 200 threads and 0.05s delay, startup takes ~10 seconds total
            startup_delay = (thread_id * 0.05) % 2.0  # Stagger over 2 seconds max
            time.sleep(startup_delay)

            while not f_stop_requested.is_set():
                # Select target node with round-robin (fast, no contention)
                with node_idx_lock:
                    target_node_idx = node_idx % len(self.nodes)
                    node_idx += 1

                target_node = self.nodes[target_node_idx]

                # Lock this specific node to ensure chain integrity
                # This allows parallel mining on different nodes while maintaining sequential order per node
                with per_node_locks[target_node_idx]:
                    # Get next two heights for this node (parent and child)
                    parent_height = per_node_height_counters[target_node_idx]
                    child_height = parent_height + 1
                    per_node_height_counters[target_node_idx] += 2  # Reserve both heights

                    # Get the hash to build on (forms a chain)
                    prev_hash = per_node_last_block_hash[target_node_idx]

                    try:
                        # Create parent block pointing to the last block in the chain
                        parent_block = BlockUtils.create_block(
                            prev_hash_hex=prev_hash,
                            height=parent_height,
                            miner=f"stress_thread{thread_id}_parent_h{parent_height}",
                            mine=True
                        )

                        # Create child block pointing to parent
                        child_block = BlockUtils.create_block(
                            prev_hash_hex=parent_block['hash_hex'],
                            height=child_height,
                            miner=f"stress_thread{thread_id}_child_h{child_height}",
                            mine=True
                        )
                        self.log_info(f"For target node port: {target_node.p2p_port} generated parent block height: {parent_height} parent hash: {parent_block['hash_hex']}, parent prev_hash: {prev_hash}, child block height: {child_height}, child block hash: {child_block['hash_hex']}, per_node_last_block_hash[{target_node_idx}]: {per_node_last_block_hash[target_node_idx]} ")

                        # Send child first (becomes orphan), then parent (triggers resolution)
                        # Use shorter timeout (5s) to fail faster on connection issues
                        try:
                            with P2PConnection("127.0.0.1", target_node.p2p_port, timeout=5) as conn:
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
                        except Exception as e:
                            self.log_info(f"Connection rejected: {e}")

                        local_block_count += 1

                        # Update last block hash to the child (for next iteration to build on)
                        # This happens atomically within the per-node lock, preventing race conditions
                        per_node_last_block_hash[target_node_idx] = child_block['hash_hex']
                        self.log_info(f"DEBUG: per_node_last_block_hash[{target_node_idx}]: {per_node_last_block_hash[target_node_idx]}")

                        # Update block count (inside try block to ensure proper error handling)
                        with counts_lock:
                            per_node_block_counts[target_node_idx] += 1

                        # Periodically check orphan queue size
                        if local_block_count % 10 == 0:
                            try:
                                chain_info = target_node.get_chain_info()
                                orphan_size = chain_info.get('orphan_blocks_size', 0)
                                with orphan_stats_lock:
                                    if orphan_size > peak_orphan_size:
                                        peak_orphan_size = orphan_size
                            except Exception:
                                pass  # Ignore errors reading orphan queue

                    except Exception as e:
                        local_error_count += 1
                        # Add small delay on error to avoid rapid retry storms
                        time.sleep(0.5)

                # Small delay between pairs
                time.sleep(0.3)

            # Store thread-local counts
            thread_block_counts[thread_id] = local_block_count
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
                with counts_lock:
                    node_distribution = list(per_node_block_counts)
                    current_block_count = sum(per_node_block_counts)
                with orphan_stats_lock:
                    current_peak_orphan = peak_orphan_size
                rate = current_block_count / elapsed if elapsed > 0 else 0
                memory_percent = (avg_memory / total_memory_mb) * 100
                self.log_info(f"  Progress: {elapsed:.0f}s, "
                              f"{current_block_count} pairs ({rate:.1f} pairs/sec), "
                              f"memory: {avg_memory:.1f}MB ({memory_percent:.1f}%), "
                              f"peak orphans: {current_peak_orphan}, "
                              f"distribution: {node_distribution}")
                last_progress_time = time.time()

        # Wait for all threads to finish
        self.log_info("  Waiting for threads to complete...")
        for t in threads:
            t.join(timeout=5)

        total_time = time.time() - start_time
        total_pairs = sum(per_node_block_counts)
        total_blocks = total_pairs * 2  # Each pair has 2 blocks
        total_errors = sum(thread_errors)

        self.log_info(f"Step 5 complete: {total_pairs} orphan pairs ({total_blocks} blocks) sent in {total_time:.2f}s")
        self.log_info(f"  Per-thread distribution: {thread_block_counts}")
        self.log_info(f"  Per-node distribution: {per_node_block_counts}")
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
        for i, node in enumerate(self.nodes):
            chain_info = node.get_chain_info()
            blocks = chain_info.get('blocks', 0)
            n_sent = per_node_block_counts[i] * 2  # Each pair has 2 blocks
            n_received = blocks - initial_blocks
            total_received += n_received  # Sum across all nodes
            collectors[i].set_block_count(mined=0, received=n_received)
            self.log_info(f"  Node{i} blocks: {blocks} (received {n_received})")

        # Calculate peak resources
        peak_memory = max(c.get_memory_max() for c in collectors)
        avg_memory = sum(c.get_memory_avg() for c in collectors) / len(collectors)
        peak_cpu = max(c.get_cpu_max() for c in collectors)
        avg_cpu = sum(c.get_cpu_avg() for c in collectors) / len(collectors)

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
            'per_thread_distribution': thread_block_counts,
            'per_node_distribution': per_node_block_counts,
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
    # Ensure results directory exists
    results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')
    os.makedirs(results_dir, exist_ok=True)
    unittest.main()
