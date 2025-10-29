#!/usr/bin/env python3
"""
Functional test runner for Blockweave REST daemon.

Runs individual functional tests or all tests in the test/functional directory using unittest.

Usage:
    python3 test_runner.py                    # Run all tests
    python3 test_runner.py TestClass          # Run specific test class
    python3 test_runner.py TestClass.test_method  # Run specific test method
    python3 test_runner.py -v                 # Run with verbose output
"""

import sys
import os
import unittest
import argparse
import tempfile
import logging
from pathlib import Path


class TestRunner:
    """Runs functional tests using unittest framework."""

    def __init__(self, tmpdir=None, nocleanup=False, verbosity=1):
        """Initialize the test runner."""
        self.script_dir = Path(__file__).parent.resolve()
        self.tmpdir = tmpdir
        self.nocleanup = nocleanup
        self.verbosity = verbosity

    def setup_environment(self):
        """Setup environment variables for tests."""
        if self.tmpdir:
            tmpdir_path = Path(self.tmpdir)
            tmpdir_path.mkdir(parents=True, exist_ok=True)
            os.environ["TEST_TMPDIR"] = str(tmpdir_path)
        else:
            tmpdir_path = Path(tempfile.mkdtemp(prefix="blockweave_test_"))
            os.environ["TEST_TMPDIR"] = str(tmpdir_path)

        if self.nocleanup:
            os.environ["TEST_NOCLEANUP"] = "1"

        return tmpdir_path

    def discover_tests(self, pattern='test*.py'):
        """
        Discover tests using unittest's test discovery.

        Args:
            pattern: Pattern to match test files (default: 'test*.py')

        Returns:
            TestSuite: Discovered test suite
        """
        loader = unittest.TestLoader()
        start_dir = str(self.script_dir)
        suite = loader.discover(start_dir, pattern=pattern, top_level_dir=start_dir)
        return suite

    def run_specific_test(self, test_name):
        """
        Run a specific test class or test method.

        Args:
            test_name: Test name (e.g., 'TestClass' or 'TestClass.test_method')

        Returns:
            int: Exit code (0 for success, 1 for failure)
        """
        tmpdir_path = self.setup_environment()

        try:
            # Load the specific test
            loader = unittest.TestLoader()
            suite = loader.loadTestsFromName(test_name)

            # Run the test
            runner = unittest.TextTestRunner(verbosity=self.verbosity)
            result = runner.run(suite)

            return 0 if result.wasSuccessful() else 1
        finally:
            if self.nocleanup:
                print(f"\nTest data preserved in: {tmpdir_path}")

    def run_all_tests(self):
        """
        Run all functional tests using unittest.

        Returns:
            int: Exit code (0 for success, 1 for failure)
        """
        tmpdir_path = self.setup_environment()

        print(f"\n{'='*70}")
        print(f"RUNNING ALL FUNCTIONAL TESTS")
        print(f"{'='*70}")
        if self.tmpdir or tmpdir_path:
            print(f"Test tmpdir: {tmpdir_path}")
        print()

        try:
            # Discover all tests
            suite = self.discover_tests()

            # Count tests
            test_count = suite.countTestCases()
            if test_count == 0:
                print("No tests found matching pattern 'test*.py'")
                return 0

            print(f"Found {test_count} test(s)\n")

            # Run tests
            runner = unittest.TextTestRunner(verbosity=self.verbosity)
            result = runner.run(suite)

            # Print summary
            print(f"\n{'='*70}")
            print(f"TEST EXECUTION SUMMARY")
            print(f"{'='*70}")
            print(f"Total tests:  {result.testsRun}")
            print(f"Passed:       {result.testsRun - len(result.failures) - len(result.errors)}")
            print(f"Failed:       {len(result.failures) + len(result.errors)}")

            if result.wasSuccessful():
                print(f"\n✓ ALL TESTS PASSED\n")
                return 0
            else:
                if result.failures:
                    print(f"\nFailed tests:")
                    for test, _ in result.failures:
                        print(f"  - {test}")
                if result.errors:
                    print(f"\nErrors:")
                    for test, _ in result.errors:
                        print(f"  - {test}")
                print(f"\n✗ SOME TESTS FAILED\n")
                return 1
        finally:
            if self.nocleanup:
                print(f"\nTest data preserved in: {tmpdir_path}")


def main():
    """Main entry point for the test runner."""
    parser = argparse.ArgumentParser(
        description="Run Blockweave functional tests using unittest framework",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                                  # Run all tests
  %(prog)s TestClass                        # Run specific test class
  %(prog)s TestClass.test_method            # Run specific test method
  %(prog)s -v                               # Run with verbose output
  %(prog)s --tmpdir=/tmp/test_runs          # Use custom tmpdir
  %(prog)s --nocleanup                      # Keep test data after run
        """
    )

    parser.add_argument(
        "test_name",
        nargs="?",
        help="Test class or method to run (e.g., 'BlockcoreTest' or 'BlockcoreTest.test_transaction_and_mining')"
    )

    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Verbose output"
    )

    parser.add_argument(
        "--tmpdir",
        type=str,
        help="Directory for test data (default: auto-generated temp directory)"
    )

    parser.add_argument(
        "--nocleanup",
        action="store_true",
        help="Do not cleanup test data after test run"
    )

    args = parser.parse_args()

    verbosity = 2 if args.verbose else 1
    runner = TestRunner(tmpdir=args.tmpdir, nocleanup=args.nocleanup, verbosity=verbosity)

    # Print tmpdir info
    if args.tmpdir:
        print(f"Using tmpdir: {args.tmpdir}")
    if args.nocleanup:
        print("Cleanup disabled - test data will be preserved")

    if args.test_name:
        # Run specific test
        return runner.run_specific_test(args.test_name)
    else:
        # Run all tests
        return runner.run_all_tests()


if __name__ == "__main__":
    sys.exit(main())
