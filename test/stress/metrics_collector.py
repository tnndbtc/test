#!/usr/bin/env python3
"""
Metrics Collector for Blockweave Stress Tests

Provides performance monitoring capabilities for stress tests:
- CPU usage tracking (average and peak)
- Memory consumption monitoring
- Transaction/block throughput calculation
- JSON export for test results

Usage:
    collector = MetricsCollector(pid=node_process_pid, node_id="node0")
    collector.start_collection()
    # ... run test ...
    collector.stop_collection()
    collector.export_json("results/test_metrics.json")
"""

import psutil
import time
import json
import threading
from datetime import datetime
from typing import Dict, List, Optional


class MetricsCollector:
    """Collects performance metrics for a single node during stress tests."""

    def __init__(self, pid: int, node_id: str):
        """
        Initialize metrics collector for a specific process.

        Args:
            pid: Process ID of the bweave daemon to monitor
            node_id: Identifier for this node (e.g., "node0", "node1")
        """
        self.pid = pid
        self.node_id = node_id
        self.process = psutil.Process(pid)

        # Metrics storage
        self.cpu_samples: List[float] = []
        self.memory_samples: List[float] = []
        self.start_time: Optional[float] = None
        self.end_time: Optional[float] = None

        # Collection control
        self.f_collecting = False
        self.collection_thread: Optional[threading.Thread] = None
        self.sample_interval = 0.5  # Sample every 0.5 seconds

        # Application-level counters (set externally)
        self.n_transactions_sent = 0
        self.n_transactions_received = 0
        self.n_blocks_mined = 0
        self.n_blocks_received = 0

    def start_collection(self):
        """Start collecting metrics in background thread."""
        if self.f_collecting:
            return

        self.f_collecting = True
        self.start_time = time.time()
        self.cpu_samples = []
        self.memory_samples = []

        self.collection_thread = threading.Thread(target=self._collect_loop, daemon=True)
        self.collection_thread.start()

    def stop_collection(self):
        """Stop collecting metrics."""
        if not self.f_collecting:
            return

        self.f_collecting = False
        self.end_time = time.time()

        if self.collection_thread:
            self.collection_thread.join(timeout=2.0)

    def _collect_loop(self):
        """Background thread that samples CPU and memory."""
        while self.f_collecting:
            try:
                # Collect CPU percentage (over short interval)
                cpu_percent = self.process.cpu_percent(interval=0.1)
                self.cpu_samples.append(cpu_percent)

                # Collect memory in MB
                memory_info = self.process.memory_info()
                memory_mb = memory_info.rss / (1024 * 1024)
                self.memory_samples.append(memory_mb)

            except psutil.NoSuchProcess:
                # Process died during collection
                break
            except Exception as e:
                print(f"Warning: Metrics collection error for {self.node_id}: {e}")
                break

            time.sleep(self.sample_interval)

    def get_duration(self) -> float:
        """Get collection duration in seconds."""
        if self.start_time is None:
            return 0.0
        end = self.end_time if self.end_time else time.time()
        return end - self.start_time

    def get_cpu_avg(self) -> float:
        """Get average CPU percentage."""
        if not self.cpu_samples:
            return 0.0
        return sum(self.cpu_samples) / len(self.cpu_samples)

    def get_cpu_max(self) -> float:
        """Get maximum CPU percentage."""
        if not self.cpu_samples:
            return 0.0
        return max(self.cpu_samples)

    def get_memory_avg(self) -> float:
        """Get average memory usage in MB."""
        if not self.memory_samples:
            return 0.0
        return sum(self.memory_samples) / len(self.memory_samples)

    def get_memory_max(self) -> float:
        """Get maximum memory usage in MB."""
        if not self.memory_samples:
            return 0.0
        return max(self.memory_samples)

    def set_transaction_count(self, sent: int = 0, received: int = 0):
        """Set transaction counts for throughput calculation."""
        self.n_transactions_sent = sent
        self.n_transactions_received = received

    def set_block_count(self, mined: int = 0, received: int = 0):
        """Set block counts for throughput calculation."""
        self.n_blocks_mined = mined
        self.n_blocks_received = received

    def get_metrics_dict(self) -> Dict:
        """
        Get metrics as dictionary for JSON export.

        Returns:
            Dictionary containing all collected metrics for this node.
        """
        duration = self.get_duration()

        metrics = {
            "node_id": self.node_id,
            "pid": self.pid,
            "duration_sec": round(duration, 2),
            "cpu": {
                "avg_percent": round(self.get_cpu_avg(), 2),
                "max_percent": round(self.get_cpu_max(), 2),
                "samples": len(self.cpu_samples)
            },
            "memory": {
                "avg_mb": round(self.get_memory_avg(), 2),
                "max_mb": round(self.get_memory_max(), 2),
                "samples": len(self.memory_samples)
            },
            "counters": {
                "transactions_sent": self.n_transactions_sent,
                "transactions_received": self.n_transactions_received,
                "blocks_mined": self.n_blocks_mined,
                "blocks_received": self.n_blocks_received
            }
        }

        return metrics


class AggregateMetrics:
    """Aggregates metrics from multiple nodes for test-level reporting."""

    def __init__(self, test_name: str):
        """
        Initialize aggregate metrics for a test.

        Args:
            test_name: Name of the stress test being run
        """
        self.test_name = test_name
        self.collectors: List[MetricsCollector] = []
        self.start_timestamp: Optional[str] = None
        self.metadata: Dict = {}

    def add_collector(self, collector: MetricsCollector):
        """Add a node's metrics collector to the aggregate."""
        self.collectors.append(collector)

    def set_metadata(self, **kwargs):
        """Set test metadata (e.g., num_nodes, test_config)."""
        self.metadata.update(kwargs)

    def calculate_throughput(self) -> Dict:
        """
        Calculate aggregate throughput metrics.

        Returns:
            Dictionary with throughput calculations
        """
        total_tx_sent = sum(c.n_transactions_sent for c in self.collectors)
        total_tx_received = sum(c.n_transactions_received for c in self.collectors)
        total_blocks_mined = sum(c.n_blocks_mined for c in self.collectors)
        total_blocks_received = sum(c.n_blocks_received for c in self.collectors)

        # Use maximum duration across all nodes
        max_duration = max((c.get_duration() for c in self.collectors), default=1.0)
        if max_duration == 0:
            max_duration = 1.0

        throughput = {
            "transactions_per_sec": round(total_tx_sent / max_duration, 2) if total_tx_sent > 0 else 0.0,
            "blocks_per_sec": round(total_blocks_mined / max_duration, 2) if total_blocks_mined > 0 else 0.0,
            "total_transactions_sent": total_tx_sent,
            "total_transactions_received": total_tx_received,
            "total_blocks_mined": total_blocks_mined,
            "total_blocks_received": total_blocks_received
        }

        return throughput

    def export_json(self, filepath: str):
        """
        Export aggregated metrics to JSON file.

        Args:
            filepath: Path to output JSON file
        """
        # Collect all node metrics
        nodes_metrics = [collector.get_metrics_dict() for collector in self.collectors]

        # Calculate throughput
        throughput = self.calculate_throughput()

        # Build complete report
        report = {
            "test_name": self.test_name,
            "timestamp": datetime.now().isoformat(),
            "metadata": self.metadata,
            "nodes": nodes_metrics,
            "throughput": throughput
        }

        # Write to file
        with open(filepath, 'w') as f:
            json.dump(report, f, indent=2)

        print(f"Metrics exported to: {filepath}")

    def print_summary(self):
        """Print human-readable summary of metrics."""
        print(f"\n{'='*60}")
        print(f"Test: {self.test_name}")
        print(f"{'='*60}")

        for collector in self.collectors:
            metrics = collector.get_metrics_dict()
            print(f"\n{metrics['node_id']}:")
            print(f"  CPU: avg={metrics['cpu']['avg_percent']}%, max={metrics['cpu']['max_percent']}%")
            print(f"  Memory: avg={metrics['memory']['avg_mb']}MB, max={metrics['memory']['max_mb']}MB")
            print(f"  Transactions: sent={metrics['counters']['transactions_sent']}, received={metrics['counters']['transactions_received']}")
            print(f"  Blocks: mined={metrics['counters']['blocks_mined']}, received={metrics['counters']['blocks_received']}")

        throughput = self.calculate_throughput()
        print(f"\nThroughput:")
        print(f"  Transactions/sec: {throughput['transactions_per_sec']}")
        print(f"  Blocks/sec: {throughput['blocks_per_sec']}")
        print(f"{'='*60}\n")


# Example usage
if __name__ == "__main__":
    # Example: Monitor a process for 5 seconds
    import os

    print("Example: Monitoring current Python process for 5 seconds...")

    collector = MetricsCollector(pid=os.getpid(), node_id="example_node")
    collector.start_collection()

    # Simulate some work
    time.sleep(5)

    collector.stop_collection()
    collector.set_transaction_count(sent=100, received=300)

    # Create aggregate and export
    aggregate = AggregateMetrics("example_test")
    aggregate.add_collector(collector)
    aggregate.set_metadata(num_nodes=1, test_type="example")

    aggregate.print_summary()
    aggregate.export_json("/tmp/example_metrics.json")
