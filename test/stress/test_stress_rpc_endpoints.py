#!/usr/bin/env python3
"""
RPC Endpoints Stress Test for Blockweave

Memory-based stress test for RPC endpoint performance under load:
- Runs until memory reaches 80% OR 60 seconds elapsed
- 20 Python threads stress all endpoints in parallel
- Tests /rpc/getpeer, /chain, and other endpoints
- Continues stress testing while memory < 80% total AND time < 60s
- Automatically stops when threshold reached
- Record request latency (min/avg/max)
- Record CPU usage, success rate, and throughput
- Export metrics to JSON

Environment:
  PYTHONPATH=../functional (required)
  STRESS_TEST_RESULTS_DIR=results (optional, default ./results)

Run: python3 test_stress_rpc_endpoints.py
"""

import sys
import time
import unittest
import os
import random
import psutil
import threading
from test_framework import TestFramework
from metrics_collector import MetricsCollector, AggregateMetrics


class RPCEndpointsStressTest(TestFramework):
    """Stress test for RPC endpoint performance."""

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
        Setup test environment - establish connections between nodes.

        Topology:
        Node0 <-> Node1
        Node2 <-> Node3
        """
        self.log_info("setup: Establishing connections...")

        # Connect node0 <-> node1
        if self.nodes[0].connect_to_peer(self.nodes[1], wait=True):
            self.log_info("setup: node0 <-> node1 connected")

        # Connect node2 <-> node3
        if self.nodes[2].connect_to_peer(self.nodes[3], wait=True):
            self.log_info("setup: node2 <-> node3 connected")

        # Wait for connections to stabilize
        time.sleep(2)

        # Log peer counts
        for i, node in enumerate(self.nodes):
            peer_info = node.get_peer_info()
            total = peer_info.get('total_peers', 0)
            self.log_info(f"setup: Node{i} has {total} peers")

        self.log_info("setup: Connections established")

    def test_rpc_endpoints_stress(self):
        """
        Memory-based stress test: Test all RPC endpoints with 20 threads until threshold reached.

        Steps:
        1. Detect system resources (total memory)
        2. Initialize metrics collectors
        3. Spawn 20 threads to stress RPC endpoints in parallel
        4. Continue stress testing while memory < 80% AND time < 60 seconds
        5. Stop when memory reaches 80% OR 60 seconds elapsed
        6. Calculate throughput and export metrics
        """
        self.log_info("test_rpc_endpoints_stress: Starting memory-based stress test...")

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
        aggregate = AggregateMetrics("rpc_endpoints_stress")

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
            test_type="rpc_endpoints_memory_based",
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

        # Step 4: Multi-threaded RPC endpoint stress testing
        self.log_info(f"Step 4: Starting {n_threads}-threaded RPC endpoint stress testing until {memory_threshold_percent}% memory or {max_duration_sec}s...")

        # Shared state between threads
        f_stop_requested = threading.Event()  # Signal to stop all threads
        req_counter_lock = threading.Lock()
        per_thread_lock = threading.Lock()
        latency_lock = threading.Lock()
        req_counter = 0
        thread_req_counts = [0] * n_threads  # Per-thread request counts
        thread_errors = [0] * n_threads  # Per-thread error counts
        endpoint_counts = {'get_chain': 0, 'get_peer': 0}  # Endpoint usage counts
        all_latencies = []  # All request latencies

        def worker_thread(thread_id):
            """Worker thread that stresses RPC endpoints until stop signal."""
            nonlocal req_counter
            local_req_count = 0
            local_error_count = 0
            local_latencies = []

            # Each thread picks a random node to target
            target_node = self.nodes[thread_id % len(self.nodes)]

            while not f_stop_requested.is_set():
                # Get request ID atomically
                with req_counter_lock:
                    req_id = req_counter
                    req_counter += 1

                # Randomly choose an endpoint to test
                endpoint_choice = random.choice(['get_chain', 'get_peer'])

                request_start = time.time()

                try:
                    if endpoint_choice == 'get_chain':
                        # Test /chain endpoint
                        chain_info = target_node.get_chain_info()
                        if chain_info and 'blocks' in chain_info:
                            local_req_count += 1
                        else:
                            local_error_count += 1
                    else:  # get_peer
                        # Test /rpc/getpeer endpoint
                        peer_info = target_node.get_peer_info()
                        if peer_info and 'total_peers' in peer_info:
                            local_req_count += 1
                        else:
                            local_error_count += 1

                    # Record latency
                    request_latency = time.time() - request_start
                    local_latencies.append(request_latency)

                    # Update endpoint usage counter
                    with per_thread_lock:
                        endpoint_counts[endpoint_choice] += 1

                except Exception as e:
                    local_error_count += 1

                # Small delay between requests
                time.sleep(0.01)

            # Store thread-local counts
            with per_thread_lock:
                thread_req_counts[thread_id] = local_req_count
                thread_errors[thread_id] = local_error_count

            with latency_lock:
                all_latencies.extend(local_latencies)

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
                with req_counter_lock:
                    current_req_count = req_counter
                with per_thread_lock:
                    current_endpoint_counts = dict(endpoint_counts)
                rate = current_req_count / elapsed if elapsed > 0 else 0
                memory_percent = (avg_memory / total_memory_mb) * 100
                self.log_info(f"  Progress: {elapsed:.0f}s, "
                              f"{current_req_count} requests ({rate:.1f} req/sec), "
                              f"memory: {avg_memory:.1f}MB ({memory_percent:.1f}%), "
                              f"endpoints: {current_endpoint_counts}")
                last_progress_time = time.time()

        # Wait for all threads to finish
        self.log_info("  Waiting for threads to complete...")
        for t in threads:
            t.join(timeout=5)

        total_time = time.time() - start_time
        total_requests = req_counter
        total_successful = sum(thread_req_counts)
        total_errors = sum(thread_errors)

        self.log_info(f"Step 4 complete: {total_successful} successful requests in {total_time:.2f}s")
        self.log_info(f"  Per-thread distribution: {thread_req_counts}")
        self.log_info(f"  Endpoint usage: {endpoint_counts}")
        self.log_info(f"  Total errors: {total_errors}")

        # Wait briefly for system to stabilize
        self.log_info("  Waiting for system to stabilize...")
        time.sleep(2)

        # Step 5: Stop metrics collection
        self.log_info("Step 5: Stopping metrics collection...")
        for collector in collectors:
            collector.stop_collection()

        # Step 6: Calculate statistics
        self.log_info("Step 6: Calculating statistics...")

        # Calculate peak resources
        peak_memory = max(c.get_memory_max() for c in collectors)
        avg_memory = sum(c.get_memory_avg() for c in collectors) / len(collectors)
        peak_cpu = max(c.get_cpu_max() for c in collectors)
        avg_cpu = sum(c.get_cpu_avg() for c in collectors) / len(collectors)

        # Calculate latency statistics
        avg_latency = sum(all_latencies) / len(all_latencies) if all_latencies else 0
        min_latency = min(all_latencies) if all_latencies else 0
        max_latency = max(all_latencies) if all_latencies else 0

        success_rate = (total_successful / total_requests) * 100 if total_requests > 0 else 0
        requests_per_sec = total_successful / total_time if total_time > 0 else 0

        self.log_info(f"  Peak memory: {peak_memory:.1f} MB")
        self.log_info(f"  Average memory: {avg_memory:.1f} MB")
        self.log_info(f"  Peak CPU: {peak_cpu:.1f}%")
        self.log_info(f"  Average CPU: {avg_cpu:.1f}%")
        self.log_info(f"  Latency: min={min_latency*1000:.2f}ms, avg={avg_latency*1000:.2f}ms, max={max_latency*1000:.2f}ms")
        self.log_info(f"  Success rate: {success_rate:.1f}%")

        # Step 7: Export metrics
        self.log_info("Step 7: Exporting metrics...")
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        output_file = os.path.join(results_dir, f"rpc_endpoints_{timestamp}.json")

        # Add additional metadata with stress_stats
        aggregate.metadata['stress_stats'] = {
            'total_requests': total_requests,
            'successful_requests': total_successful,
            'failed_requests': total_errors,
            'per_thread_distribution': thread_req_counts,
            'endpoint_usage': endpoint_counts,
            'success_rate_percent': round(success_rate, 2),
            'actual_duration_sec': round(total_time, 2),
            'max_duration_sec': max_duration_sec,
            'stop_reason': stop_reason,
            'peak_cpu_percent': round(peak_cpu, 2),
            'peak_memory_mb': round(peak_memory, 2),
            'avg_cpu_percent': round(avg_cpu, 2),
            'avg_memory_mb': round(avg_memory, 2),
            'throughput_req_per_sec': round(requests_per_sec, 2),
            'latency_ms': {
                'min': round(min_latency * 1000, 2),
                'avg': round(avg_latency * 1000, 2),
                'max': round(max_latency * 1000, 2)
            }
        }

        aggregate.export_json(output_file)
        aggregate.print_summary()

        self.log_info("test_rpc_endpoints_stress: Memory-based stress test completed successfully!")


if __name__ == "__main__":
    # Ensure results directory exists
    results_dir = os.environ.get('STRESS_TEST_RESULTS_DIR', 'results')
    os.makedirs(results_dir, exist_ok=True)
    unittest.main()
