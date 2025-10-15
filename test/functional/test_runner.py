#!/usr/bin/env python3
"""
Functional test runner for Blockweave REST daemon.

Runs individual functional tests or all tests in the test/functional directory.

Usage:
    python3 test_runner.py                    # Run all tests
    python3 test_runner.py test_chain.py      # Run specific test
    python3 test_runner.py path/to/test.py    # Run test by path
"""

import sys
import os
import subprocess
import argparse
from pathlib import Path


class TestRunner:
    """Runs functional tests and aggregates results."""

    def __init__(self):
        """Initialize the test runner."""
        self.script_dir = Path(__file__).parent.resolve()
        self.num_passed = 0
        self.num_failed = 0
        self.failed_tests = []

    def find_tests(self):
        """
        Find all test_*.py files in the functional test directory.

        Returns:
            list: List of Path objects for test files
        """
        test_files = []
        for file_path in self.script_dir.glob("test_*.py"):
            # Skip test_framework.py and test_runner.py
            if file_path.name not in ("test_framework.py", "test_runner.py"):
                test_files.append(file_path)
        return sorted(test_files)

    def run_test(self, test_file):
        """
        Run a single test file.

        Args:
            test_file: Path to the test file

        Returns:
            bool: True if test passed, False otherwise
        """
        test_file = Path(test_file).resolve()

        if not test_file.exists():
            print(f"✗ ERROR: Test file not found: {test_file}")
            return False

        if not test_file.name.startswith("test_"):
            print(f"✗ ERROR: Test file must start with 'test_': {test_file.name}")
            return False

        print(f"\n{'='*70}")
        print(f"Running: {test_file.name}")
        print(f"{'='*70}")

        try:
            # Run the test as a subprocess
            result = subprocess.run(
                [sys.executable, str(test_file)],
                cwd=str(test_file.parent),
                capture_output=False,
                text=True
            )

            if result.returncode == 0:
                print(f"\n✓ {test_file.name} PASSED\n")
                return True
            else:
                print(f"\n✗ {test_file.name} FAILED (exit code: {result.returncode})\n")
                return False

        except Exception as e:
            print(f"\n✗ {test_file.name} FAILED WITH EXCEPTION: {e}\n")
            return False

    def run_all_tests(self):
        """
        Run all functional tests.

        Returns:
            int: Number of failed tests
        """
        test_files = self.find_tests()

        if not test_files:
            print("No test files found matching pattern 'test_*.py'")
            return 0

        print(f"\n{'='*70}")
        print(f"RUNNING ALL FUNCTIONAL TESTS")
        print(f"{'='*70}")
        print(f"Found {len(test_files)} test(s)\n")

        for test_file in test_files:
            if self.run_test(test_file):
                self.num_passed += 1
            else:
                self.num_failed += 1
                self.failed_tests.append(test_file.name)

        # Print summary
        self.print_summary()

        return self.num_failed

    def print_summary(self):
        """Print test execution summary."""
        print(f"\n{'='*70}")
        print(f"TEST EXECUTION SUMMARY")
        print(f"{'='*70}")
        print(f"Total tests:  {self.num_passed + self.num_failed}")
        print(f"Passed:       {self.num_passed}")
        print(f"Failed:       {self.num_failed}")

        if self.num_failed > 0:
            print(f"\nFailed tests:")
            for test_name in self.failed_tests:
                print(f"  - {test_name}")
            print(f"\n✗ SOME TESTS FAILED\n")
        else:
            print(f"\n✓ ALL TESTS PASSED\n")


def main():
    """Main entry point for the test runner."""
    parser = argparse.ArgumentParser(
        description="Run Blockweave functional tests",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                    # Run all tests
  %(prog)s test_chain.py      # Run specific test
  %(prog)s path/to/test.py    # Run test by path
        """
    )

    parser.add_argument(
        "test_file",
        nargs="?",
        help="Path to specific test file to run (runs all tests if not specified)"
    )

    args = parser.parse_args()

    runner = TestRunner()

    if args.test_file:
        # Run single test
        test_path = Path(args.test_file)

        # If relative path or just filename, look in script directory
        if not test_path.is_absolute():
            test_path = runner.script_dir / test_path

        if runner.run_test(test_path):
            return 0
        else:
            return 1
    else:
        # Run all tests
        return runner.run_all_tests()


if __name__ == "__main__":
    sys.exit(main())
