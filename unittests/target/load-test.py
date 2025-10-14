#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0 OR MIT
"""
ESP Flasher Stub Target Test Loader
Loads test binary to ESP RAM and monitors serial output for test results
"""

import sys
import os
import argparse
import time
import re
import serial
import esptool


# ANSI color codes for output
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    MAGENTA = '\033[0;35m'
    CYAN = '\033[0;36m'
    NC = '\033[0m'  # No Color


def print_status(msg):
    print(f'{Colors.GREEN}[INFO]{Colors.NC} {msg}')


def print_warning(msg):
    print(f'{Colors.YELLOW}[WARN]{Colors.NC} {msg}')


def print_error(msg):
    print(f'{Colors.RED}[ERROR]{Colors.NC} {msg}')


class SerialMonitor:
    def __init__(self, serial_connection, timeout=30):
        """Initialize SerialMonitor with existing serial connection from esptool"""
        self.serial = serial_connection
        self.timeout = timeout
        self.test_complete = False
        self.unity_stats = {
            'tests_run': 0,
            'tests_passed': 0,
            'tests_failed': 0,
            'passed_tests': [],
            'failed_tests': [],
        }

    def monitor(self):
        """Monitor serial output - simplified output"""
        if not self.serial or not self.serial.is_open:
            print_error('Serial port not connected')
            return False

        start_time = time.time()
        line_buffer = ''

        while not self.test_complete:
            try:
                elapsed_time = time.time() - start_time

                # Check overall timeout
                if elapsed_time >= self.timeout:
                    print_warning(f'\n⏱️  Test monitoring timeout ({self.timeout}s) reached')
                    break

                # Try to read data
                try:
                    data = self.serial.read(self.serial.in_waiting or 1).decode('utf-8', errors='ignore')
                    if data:
                        line_buffer += data

                        # Print raw data immediately - just the UART output
                        print(data, end='', flush=True)

                        # Process complete lines for test completion detection
                        while '\n' in line_buffer:
                            line, line_buffer = line_buffer.split('\n', 1)
                            line = line.strip()

                            if line:
                                self._process_line(line)

                        # Check if test completed
                        if self.test_complete:
                            # Give a short delay to capture any final output
                            time.sleep(0.5)
                            # Read any remaining data
                            remaining_data = self.serial.read_all().decode('utf-8', errors='ignore')
                            if remaining_data:
                                print(remaining_data, end='', flush=True)
                                # Process any remaining lines
                                for remaining_line in remaining_data.split('\n'):
                                    remaining_line = remaining_line.strip()
                                    if remaining_line:
                                        self._process_line(remaining_line)
                            break
                    else:
                        time.sleep(0.01)

                except serial.SerialTimeoutException:
                    time.sleep(0.01)

            except serial.SerialException as e:
                print_error(f'Serial error: {e}')
                break
            except UnicodeDecodeError:
                continue
            except KeyboardInterrupt:
                break

        return self.test_complete

    def _process_line(self, line):
        """Process a line for Unity test result parsing and completion detection"""
        line_clean = line.strip()

        # Parse individual test results - Unity format: "TestFileName:LineNumber:TestName:PASS" or "FAIL: message"
        if ':PASS' in line_clean or ':FAIL' in line_clean:
            test_info = self._parse_unity_test_line(line_clean)

            if ':PASS' in line_clean:
                self.unity_stats['tests_passed'] += 1
                if test_info:
                    self.unity_stats['passed_tests'].append(test_info)
            elif ':FAIL' in line_clean:
                self.unity_stats['tests_failed'] += 1
                if test_info:
                    self.unity_stats['failed_tests'].append(test_info)
            self.unity_stats['tests_run'] += 1

        # Parse Unity summary lines (e.g., "5 Tests 0 Failures 0 Ignored") - PRIMARY completion detection
        elif 'Tests' in line_clean and ('Failures' in line_clean or 'Ignored' in line_clean):
            numbers = re.findall(r'\d+', line_clean)
            if len(numbers) >= 2:
                self.unity_stats['tests_run'] = int(numbers[0])
                self.unity_stats['tests_failed'] = int(numbers[1])
                self.unity_stats['tests_passed'] = self.unity_stats['tests_run'] - self.unity_stats['tests_failed']
                print_status(f'\n✅ Unity test summary detected: {line_clean}')
                self.test_complete = True

        # Secondary completion detection for Unity's standard indicators
        elif line_clean in ['OK', 'FAIL']:
            if line_clean == 'OK':
                print_status('\n✅ Unity test completion detected: OK')
            else:
                print_status('\n❌ Unity test completion detected: FAIL')
            self.test_complete = True

        # Fallback completion detection for custom markers
        elif any(
            marker in line_clean.upper()
            for marker in ['UNITY TEST RUN COMPLETE', 'ALL TESTS PASSED', 'SOME TESTS FAILED', 'END OF TESTS']
        ):
            print_status(f'\n🏁 Completion marker detected: {line_clean}')
            self.test_complete = True

    def _parse_unity_test_line(self, line):
        """Parse Unity test result line and extract test information
        Format: /path/to/file.c:line:test_function_name:PASS/FAIL[:message]
        Returns: dict with test_name, file, line, status, message
        """
        try:
            # Split the line by colons
            parts = line.split(':')
            if len(parts) >= 4:
                file_path = parts[0]
                line_num = parts[1]
                test_name = parts[2]
                status = parts[3]
                message = ':'.join(parts[4:]) if len(parts) > 4 else ''

                # Extract just the filename from the full path
                filename = file_path.split('/')[-1] if '/' in file_path else file_path

                return {
                    'test_name': test_name,
                    'file': filename,
                    'line': line_num,
                    'status': status,
                    'message': message.strip(),
                }
        except (IndexError, ValueError):
            pass
        return None

    def get_test_summary(self):
        """Get a summary of Unity test results"""
        if self.unity_stats['tests_run'] == 0:
            return 'No test results captured'

        summary = '\n' + '=' * 70 + '\n'
        summary += f'{Colors.MAGENTA}UNITY TEST RESULTS SUMMARY{Colors.NC}\n'
        summary += '=' * 70 + '\n'

        # Overall statistics
        summary += f'{Colors.BLUE}Tests Run: {self.unity_stats["tests_run"]}{Colors.NC}\n'
        summary += f'{Colors.GREEN}Passed: {self.unity_stats["tests_passed"]}{Colors.NC}\n'
        summary += f'{Colors.RED}Failed: {self.unity_stats["tests_failed"]}{Colors.NC}\n'

        if self.unity_stats['tests_failed'] == 0:
            summary += f'{Colors.GREEN}🎉 ALL TESTS PASSED!{Colors.NC}\n'
        else:
            summary += f'{Colors.RED}❌ {self.unity_stats["tests_failed"]} TEST(S) FAILED{Colors.NC}\n'

        summary += '-' * 70 + '\n'

        # Detailed results
        summary += f'{Colors.CYAN}DETAILED RESULTS:{Colors.NC}\n'

        # Show passed tests
        for test_info in self.unity_stats['passed_tests']:
            summary += f'{Colors.GREEN}✓ PASS: {test_info["test_name"]}{Colors.NC}\n'

        # Show failed tests with inline details
        for test_info in self.unity_stats['failed_tests']:
            summary += f'{Colors.RED}✗ FAIL: {test_info["test_name"]}{Colors.NC}'
            if test_info.get('message'):
                summary += f' - {test_info["message"]}'
            summary += f' ({test_info["file"]}:{test_info["line"]})\n'

        # Failure details section (if any failures)
        if self.unity_stats['failed_tests']:
            summary += '\n' + '-' * 70 + '\n'
            summary += f'{Colors.RED}FAILURE DETAILS:{Colors.NC}\n'
            for i, test_info in enumerate(self.unity_stats['failed_tests'], 1):
                summary += f'{Colors.RED}❌ {i}. {test_info["test_name"]}:{Colors.NC}\n'
                summary += f'   File: {test_info["file"]}:{test_info["line"]}\n'
                if test_info.get('message'):
                    summary += f'   Error: {test_info["message"]}\n'
                if i < len(self.unity_stats['failed_tests']):
                    summary += '\n'

        summary += '=' * 70
        return summary


def load_and_monitor(chip, port, binary_path, timeout):
    """Load binary to ESP RAM and monitor test execution"""
    if not os.path.exists(binary_path):
        raise FileNotFoundError(f'Binary file not found: {binary_path}')

    with esptool.cmds.detect_chip(port=port, trace_enabled=False) as esp:
        # Handle esptool version compatibility
        esptool_version = tuple(map(int, esptool.__version__.split('.')))

        if esptool_version >= (5, 0, 0):
            esptool.cmds.load_ram(esp, binary_path)
        else:
            # esptool v4 compatibility
            load_args = argparse.Namespace(filename=binary_path)
            esptool.cmds.load_ram(esp, load_args)

        # Monitor test execution
        monitor = SerialMonitor(esp._port, timeout)
        print_status('Monitoring Unity test execution...')

        success = monitor.monitor()
        print(monitor.get_test_summary())

        if success:
            print_status('Unity test execution completed!')
            all_passed = monitor.unity_stats['tests_failed'] == 0 and monitor.unity_stats['tests_run'] > 0
            if all_passed:
                print_status('🎉 All Unity tests passed!')
            else:
                print_warning(f'⚠️  Unity tests completed with {monitor.unity_stats["tests_failed"]} failures')
            return all_passed
        else:
            print_error('❌ Test monitoring timed out or failed to detect completion')
            return False


def main():
    parser = argparse.ArgumentParser(description='Load ESP test binary to RAM and monitor test results')
    parser.add_argument('-c', '--chip', required=True, help='ESP chip type')
    parser.add_argument('-p', '--port', default='/dev/ttyUSB0', help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('-t', '--timeout', type=int, default=5, help='Test timeout in seconds (default: 5)')
    parser.add_argument('-f', '--binary', required=True, help='Path to test binary (./build/*.bin)')

    args = parser.parse_args()

    try:
        success = load_and_monitor(args.chip, args.port, args.binary, args.timeout)
        return 0 if success else 1
    except KeyboardInterrupt:
        return 1


if __name__ == '__main__':
    sys.exit(main())
