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
import shutil
from pathlib import Path
from test_framework.test_framework import TestFramework


class TestRunner:
    """Runs functional tests using unittest framework."""

    def __init__(self, tmpdir=None, nocleanup=False, verbosity=1, output=None):
        """Initialize the test runner."""
        self.script_dir = Path(__file__).parent.resolve()
        self.tmpdir = tmpdir
        self.nocleanup = nocleanup
        self.verbosity = verbosity
        self.output = output

    def setup_environment(self):
        """Setup environment variables for tests."""
        from datetime import datetime

        # Generate timestamp for this test run
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

        if self.tmpdir:
            # If tmpdir is specified, create test_run_* subdirectory within it
            base_tmpdir = Path(self.tmpdir)
            base_tmpdir.mkdir(parents=True, exist_ok=True)
            tmpdir_path = base_tmpdir / f"test_run_{timestamp}"
            tmpdir_path.mkdir(parents=True, exist_ok=True)
            os.environ["TEST_TMPDIR"] = str(tmpdir_path)
        else:
            # Create temp directory with test_run_ prefix and timestamp
            tmpdir_path = Path(tempfile.mkdtemp(prefix=f"test_run_{timestamp}_"))
            os.environ["TEST_TMPDIR"] = str(tmpdir_path)

        if self.nocleanup:
            os.environ["TEST_NOCLEANUP"] = "1"

        # Set STRESS_TEST_RESULTS_DIR for stress tests to save JSON metrics
        if self.output:
            output_path = Path(self.output)
            output_path.mkdir(parents=True, exist_ok=True)
            os.environ["STRESS_TEST_RESULTS_DIR"] = str(output_path)

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

    def run_test_file(self, file_path):
        """
        Run all tests from a specific test file.

        Args:
            file_path: Path to the test file

        Returns:
            int: Exit code (0 for success, 1 for failure)
        """
        tmpdir_path = self.setup_environment()

        print(f"\n{'='*70}")
        print(f"RUNNING TESTS FROM: {Path(file_path).name}")
        print(f"{'='*70}")
        if self.tmpdir or tmpdir_path:
            print(f"Test tmpdir: {tmpdir_path}")
        if self.output:
            print(f"Results output directory: {self.output}")
        print()

        try:
            # Load tests from the specific file
            loader = unittest.TestLoader()

            # Get the module name from file path
            file_path_obj = Path(file_path)
            module_name = file_path_obj.stem  # e.g., 'test_blockfile' from 'test_blockfile.py'

            # Discover tests in this specific module
            suite = loader.loadTestsFromName(module_name)

            # Count tests
            test_count = suite.countTestCases()
            if test_count == 0:
                print(f"No tests found in {file_path_obj.name}")
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
                print(f"\n ALL TESTS PASSED\n")
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
                print(f"\n SOME TESTS FAILED\n")
                return 1
        finally:
            if self.nocleanup:
                print(f"\nTest data preserved in: {tmpdir_path}")
            else:
                # Remove the parent tmpdir when not preserving test data
                if tmpdir_path and tmpdir_path.exists():
                    TestFramework.force_remove_tree(tmpdir_path)

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

            # Try to load directly first
            suite = loader.loadTestsFromName(test_name)

            # Check if the suite contains a _FailedTest (indicating import failure)
            if suite.countTestCases() > 0:
                # Check if it's a failed test
                for test in suite:
                    if test.__class__.__name__ == '_FailedTest':
                        # Try to find the test class in test modules
                        suite = None
                        for test_file in self.script_dir.glob("test_*.py"):
                            module_name = test_file.stem
                            try:
                                # Try module.TestClass or module.TestClass.test_method
                                full_name = f"{module_name}.{test_name}"
                                candidate_suite = loader.loadTestsFromName(full_name)
                                # Check if this succeeded
                                has_failed = False
                                for t in candidate_suite:
                                    if t.__class__.__name__ == '_FailedTest':
                                        has_failed = True
                                        break
                                if not has_failed:
                                    suite = candidate_suite
                                    break
                            except (ImportError, AttributeError):
                                continue

                        if suite is None:
                            print(f"Error: Could not find test '{test_name}'")
                            print(f"Try using the full module path like 'test_blockcore.BlockcoreTest'")
                            return 1
                        break

            # Run the test
            runner = unittest.TextTestRunner(verbosity=self.verbosity)
            result = runner.run(suite)

            return 0 if result.wasSuccessful() else 1
        finally:
            if self.nocleanup:
                print(f"\nTest data preserved in: {tmpdir_path}")
            else:
                # Remove the parent tmpdir when not preserving test data
                if tmpdir_path and tmpdir_path.exists():
                    TestFramework.force_remove_tree(tmpdir_path)

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
        if self.output:
            print(f"Results output directory: {self.output}")
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
                print(f"\n ALL TESTS PASSED\n")
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
                print(f"\n SOME TESTS FAILED\n")
                return 1
        finally:
            if self.nocleanup:
                print(f"\nTest data preserved in: {tmpdir_path}")
            else:
                # Remove the parent tmpdir when not preserving test data
                if tmpdir_path and tmpdir_path.exists():
                    TestFramework.force_remove_tree(tmpdir_path)


def main():
    """Main entry point for the test runner."""
    parser = argparse.ArgumentParser(
        description="Run Blockweave functional tests using unittest framework",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                                  # Run all tests
  %(prog)s test_blockfile.py                # Run all tests in a file
  %(prog)s test/functional/test_blockfile.py  # Run tests with path
  %(prog)s BlockcoreTest                    # Run specific test class
  %(prog)s BlockcoreTest.test_transaction_and_mining  # Run specific test method
  %(prog)s -v                               # Run with verbose output
  %(prog)s --tmpdir=/tmp/test_runs          # Use custom tmpdir
  %(prog)s --nocleanup                      # Keep test data after run
  %(prog)s --output test_results/           # Save stress test metrics to directory
  %(prog)s test_stress_transaction.py --tmpdir /tmp --nocleanup --output test_results/
        """
    )

    parser.add_argument(
        "test_name",
        nargs="?",
        help="Test file, class, or method to run (e.g., 'test_blockfile.py', 'BlockcoreTest', or 'BlockcoreTest.test_transaction_and_mining')"
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

    parser.add_argument(
        "--output",
        type=str,
        help="Directory for stress test results/metrics (sets STRESS_TEST_RESULTS_DIR environment variable)"
    )

    args = parser.parse_args()

    verbosity = 2 if args.verbose else 1
    runner = TestRunner(tmpdir=args.tmpdir, nocleanup=args.nocleanup, verbosity=verbosity, output=args.output)

    # Print configuration info
    if args.tmpdir:
        print(f"Using tmpdir: {args.tmpdir}")
    if args.nocleanup:
        print("Cleanup disabled - test data will be preserved")
    if args.output:
        print(f"Stress test results will be saved to: {args.output}")

    if args.test_name:
        # Check if it's a file path
        test_arg = args.test_name

        # Handle file paths (e.g., test_blockfile.py or test/functional/test_blockfile.py)
        if test_arg.endswith('.py'):
            from pathlib import Path
            test_path = Path(test_arg)

            # If it's not an absolute path and doesn't exist, try relative to script dir
            if not test_path.is_absolute() and not test_path.exists():
                # Strip any directory prefix and just use the filename
                test_filename = test_path.name
                test_path = runner.script_dir / test_filename

            # Check if file exists
            if not test_path.exists():
                print(f"Error: Test file not found: {test_arg}")
                return 1

            # Run tests from this specific file
            return runner.run_test_file(str(test_path))
        else:
            # It's a test class or method name
            return runner.run_specific_test(test_arg)
    else:
        # Run all tests
        return runner.run_all_tests()


if __name__ == "__main__":
    sys.exit(main())
