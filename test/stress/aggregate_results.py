#!/usr/bin/env python3
"""
Aggregate and display stress test results from JSON files.

Reads all JSON output files for each stress test and displays results in CSV format.

Usage:
    python3 aggregate_results.py <results_directory>

Example:
    python3 aggregate_results.py test_results/
"""

import sys
import os
import json
from pathlib import Path
from datetime import datetime


def parse_timestamp_from_filename(filename):
    """
    Extract timestamp from filename like: test_stress_transaction_20251205_160612.json

    Args:
        filename: JSON filename

    Returns:
        str: Formatted timestamp like "2025-12-05 16:06:12 UTC"
    """
    try:
        # Extract timestamp part (YYYYMMDD_HHMMSS)
        parts = filename.split('_')
        if len(parts) >= 3:
            # Find date and time parts
            date_part = None
            time_part = None
            for i, part in enumerate(parts):
                if len(part) == 8 and part.isdigit():  # YYYYMMDD
                    date_part = part
                    if i + 1 < len(parts):
                        next_part = parts[i + 1].replace('.json', '')
                        if len(next_part) == 6 and next_part.isdigit():  # HHMMSS
                            time_part = next_part
                            break

            if date_part and time_part:
                # Format: YYYYMMDD_HHMMSS -> YYYY-MM-DD HH:MM:SS UTC
                year = date_part[0:4]
                month = date_part[4:6]
                day = date_part[6:8]
                hour = time_part[0:2]
                minute = time_part[2:4]
                second = time_part[4:6]
                return f"{year}-{month}-{day} {hour}:{minute}:{second} UTC"
    except Exception:
        pass

    return "Unknown"


def extract_test_name(filename):
    """
    Extract test name from filename.

    Args:
        filename: JSON filename like "transaction_stress_20251205_160612.json"

    Returns:
        str: Test name like "test_stress_transaction"
    """
    # Remove timestamp and extension
    base = filename.split('_202')[0]  # Split before timestamp

    # Map common patterns
    if base == 'block':
        return 'test_stress_block'
    elif base.startswith('transaction_stress'):
        return 'test_stress_transaction'
    elif base.startswith('transaction_propagation'):
        return 'test_stress_transaction_propagation'
    elif 'block_propagation' in base:
        return 'test_stress_block_propagation'
    elif 'orphan' in base or base == 'orphan_blocks':
        return 'test_stress_orphan_blocks'
    elif 'rpc' in base or 'chain_endpoint' in base or 'getpeer' in base or 'addpeer' in base or base == 'rpc_endpoints':
        return 'test_stress_rpc_endpoints'
    elif 'malicious' in base:
        return 'test_stress_malicious_nodes'
    else:
        return base


def aggregate_results(results_dir, test_filter=None):
    """
    Aggregate all stress test results from JSON files.

    Args:
        results_dir: Directory containing JSON result files
        test_filter: Optional test name to filter results (e.g., "transaction", "all", or None)
    """
    results_path = Path(results_dir)

    if not results_path.exists():
        print(f"Error: Results directory not found: {results_dir}")
        sys.exit(1)

    # Find all JSON files
    json_files = sorted(results_path.glob('*.json'))

    if not json_files:
        print(f"No JSON files found in {results_dir}")
        return

    # Group files by test name
    test_results = {}
    for json_file in json_files:
        test_name = extract_test_name(json_file.name)
        if test_name not in test_results:
            test_results[test_name] = []
        test_results[test_name].append(json_file)

    # Apply filter if specified
    if test_filter and test_filter.lower() != 'all':
        # Convert filter to full test name format
        filter_full_name = f"test_stress_{test_filter}"
        filtered_results = {}
        for test_name, files in test_results.items():
            # Exact match only
            if test_name == filter_full_name:
                filtered_results[test_name] = files
        test_results = filtered_results

        if not test_results:
            print(f"No results found for test: {test_filter}")
            return

    print(f"Found {sum(len(files) for files in test_results.values())} JSON result file(s)")
    print("")

    # Process each test type
    for test_name in sorted(test_results.keys()):
        files = test_results[test_name]
        print(f"{test_name}:")

        # Process each result file for this test
        for json_file in files:
            try:
                with open(json_file, 'r') as f:
                    data = json.load(f)

                # Extract timestamp from filename
                timestamp = parse_timestamp_from_filename(json_file.name)

                # Extract metadata
                metadata = data.get('metadata', {})

                # Get stats from different possible locations based on test type
                # Check metadata first, then top level
                stats = None
                stats_key = None

                # Try to find stats in metadata first
                for key in ['stress_stats', 'memory_based_stats', 'attack_stats', 'orphan_processing', 'rpc_stats']:
                    if key in metadata:
                        stats = metadata[key]
                        stats_key = key
                        break

                # Fall back to top level
                if stats is None:
                    for key in ['stress_stats', 'memory_based_stats', 'attack_stats', 'orphan_processing', 'rpc_stats']:
                        if key in data:
                            stats = data[key]
                            stats_key = key
                            break

                if stats:
                    # Build output line starting with timestamp
                    output_parts = [f"[{timestamp}]"]

                    # Add metadata fields first (if not already in stats)
                    metadata_fields = {}
                    # Support both num_threads (old) and num_processes (new)
                    if 'num_processes' in metadata:
                        metadata_fields['num_processes'] = metadata['num_processes']
                    elif 'num_threads' in metadata:
                        metadata_fields['num_threads'] = metadata['num_threads']
                    if 'max_duration_sec' in metadata:
                        metadata_fields['max_duration_sec'] = metadata['max_duration_sec']

                    # Add metadata fields that aren't in stats
                    for key, value in metadata_fields.items():
                        if key not in stats:
                            output_parts.append(f"{key}:{value}")

                    # Add all stats fields except distribution arrays
                    excluded_fields = ['per_thread_distribution', 'per_process_distribution', 'per_node_distribution']
                    for key, value in sorted(stats.items()):
                        if key not in excluded_fields:
                            # Format value appropriately
                            if isinstance(value, float):
                                output_parts.append(f"{key}:{value:.2f}")
                            elif isinstance(value, list):
                                # Skip lists (like per_thread_distribution)
                                continue
                            else:
                                output_parts.append(f"{key}:{value}")

                    # Print in CSV format
                    print(", ".join(output_parts))
                else:
                    print(f"[{timestamp}],Error: No stats found in {json_file.name}")

            except json.JSONDecodeError as e:
                print(f"[{timestamp}],Error: Failed to parse {json_file.name}: {e}")
            except Exception as e:
                print(f"[{timestamp}],Error: Failed to process {json_file.name}: {e}")

        print("")  # Blank line between test types


def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print("Usage: python3 aggregate_results.py <results_directory> [test_name]")
        print("")
        print("Arguments:")
        print("  results_directory - Directory containing JSON result files")
        print("  test_name         - Optional test name filter (default: all)")
        print("                      Use 'all' to show all tests, or specify test name:")
        print("                      'transaction', 'transaction_propagation', 'block_propagation',")
        print("                      'orphan_blocks', 'rpc_endpoints', 'malicious_nodes'")
        print("")
        print("Examples:")
        print("  python3 aggregate_results.py test_results/")
        print("  python3 aggregate_results.py test_results/ all")
        print("  python3 aggregate_results.py test_results/ transaction")
        print("  python3 aggregate_results.py test_results/ transaction_propagation")
        sys.exit(1)

    results_dir = sys.argv[1]
    test_filter = sys.argv[2] if len(sys.argv) > 2 else None
    aggregate_results(results_dir, test_filter)


if __name__ == "__main__":
    main()
