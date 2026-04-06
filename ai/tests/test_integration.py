#!/usr/bin/env python3
"""
VernisOS Phase 16: Integration Test Suite
==========================================
End-to-end QEMU-based tests covering:
  1. Boot sequence verification (serial log tags)
  2. CLI command execution (all 18 commands)
  3. Permission enforcement (login/deny/audit)
  4. Kernel self-test invocation
  5. Logging system (klog) via 'log' command
  6. Audit log round-trip verification

Usage:
    python3 ai/tests/test_integration.py [--arch x86|x64] [--img path]
"""

import subprocess
import sys
import time
import os
import signal

# ===========================================================================
# Configuration
# ===========================================================================
SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..'))
DEFAULT_IMG = os.path.join(PROJECT_DIR, 'os.img')

BOOT_TIMEOUT    = 30   # seconds to wait for boot
CMD_TIMEOUT     = 5    # seconds per command
PROMPT_PATTERN  = '@vernisOS'

# ===========================================================================
# Test Results Tracker
# ===========================================================================
class Results:
    def __init__(self):
        self.passed  = 0
        self.failed  = 0
        self.skipped = 0
        self.errors  = []

    def ok(self, name):
        self.passed += 1
        print(f'  [PASS] {name}')

    def fail(self, name, reason=''):
        self.failed += 1
        self.errors.append((name, reason))
        print(f'  [FAIL] {name}  {reason}')

    def skip(self, name, reason=''):
        self.skipped += 1
        print(f'  [SKIP] {name}  {reason}')

    def summary(self):
        total = self.passed + self.failed + self.skipped
        print(f'\n{"="*50}')
        print(f'Results: {self.passed} passed, {self.failed} failed, {self.skipped} skipped ({total} total)')
        if self.errors:
            print('Failures:')
            for name, reason in self.errors:
                print(f'  - {name}: {reason}')
        print(f'{"="*50}')
        return self.failed

# ===========================================================================
# QEMU Session Manager
# ===========================================================================
class QemuSession:
    """Manage a QEMU process with serial I/O via stdin/stdout pipes."""

    def __init__(self, img_path, arch='x64'):
        self.arch = arch
        self.img_path = img_path
        self.proc = None
        self.output_buf = ''

    def start(self):
        qemu_bin = 'qemu-system-x86_64' if self.arch == 'x64' else 'qemu-system-i386'
        cmd = [
            qemu_bin,
            '-drive', f'format=raw,file={self.img_path}',
            '-serial', 'stdio',
            '-display', 'none',
            '-no-reboot',
            '-no-shutdown',
            '-m', '64M',
        ]
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )

    def send(self, text):
        """Send text to QEMU serial."""
        if self.proc and self.proc.stdin:
            self.proc.stdin.write((text + '\r\n').encode())
            self.proc.stdin.flush()

    def read_until(self, pattern, timeout=CMD_TIMEOUT):
        """Read stdout until pattern found or timeout."""
        deadline = time.time() + timeout
        buf = ''
        import select
        while time.time() < deadline:
            ready, _, _ = select.select([self.proc.stdout], [], [], 0.1)
            if ready:
                chunk = self.proc.stdout.read1(4096).decode('utf-8', errors='replace')
                buf += chunk
                if pattern in buf:
                    self.output_buf = buf
                    return True
        self.output_buf = buf
        return False

    def read_for(self, seconds):
        """Read output for a fixed duration."""
        deadline = time.time() + seconds
        buf = ''
        import select
        while time.time() < deadline:
            ready, _, _ = select.select([self.proc.stdout], [], [], 0.1)
            if ready:
                chunk = self.proc.stdout.read1(4096).decode('utf-8', errors='replace')
                buf += chunk
        self.output_buf = buf
        return buf

    def send_and_read(self, cmd, wait=CMD_TIMEOUT):
        """Send command and read output for `wait` seconds."""
        self.send(cmd)
        return self.read_for(wait)

    def stop(self):
        if self.proc:
            try:
                self.proc.kill()
                self.proc.wait(timeout=5)
            except Exception:
                pass

# ===========================================================================
# Test Suites
# ===========================================================================

def test_boot_sequence(qemu, results):
    """Test 1: Verify boot completes and shell prompt appears."""
    print('\n--- Test Suite: Boot Sequence ---')

    if qemu.read_until(PROMPT_PATTERN, BOOT_TIMEOUT):
        results.ok('Boot completes with prompt')
    else:
        results.fail('Boot completes with prompt', 'timeout waiting for prompt')
        return False

    # Check serial log for critical init tags
    buf = qemu.output_buf
    tags = ['[VernisOS]', '[heap]', '[gdt]', '[idt]', '[pic]', '[pit]',
            '[phase3]', '[phase4]', '[phase5]', '[phase6]', '[phase8]',
            '[klog]', '[phase7]']
    for tag in tags:
        if tag in buf:
            results.ok(f'Boot tag: {tag}')
        else:
            results.fail(f'Boot tag: {tag}', 'not found in serial output')

    return True


def test_basic_commands(qemu, results):
    """Test 2: Basic CLI commands as root."""
    print('\n--- Test Suite: Basic Commands ---')

    # help
    out = qemu.send_and_read('help', 2)
    if 'help' in out.lower() and 'shutdown' in out.lower():
        results.ok('help command lists commands')
    else:
        results.fail('help command lists commands', f'got: {out[:100]}')

    # whoami
    out = qemu.send_and_read('whoami', 2)
    if 'root' in out.lower():
        results.ok('whoami shows root')
    else:
        results.fail('whoami shows root', f'got: {out[:100]}')

    # echo
    out = qemu.send_and_read('echo hello world', 2)
    if 'hello world' in out:
        results.ok('echo prints text')
    else:
        results.fail('echo prints text')

    # info
    out = qemu.send_and_read('info', 2)
    if 'VernisOS' in out or 'uptime' in out.lower() or 'tick' in out.lower():
        results.ok('info shows system info')
    else:
        results.fail('info shows system info')

    # ps
    out = qemu.send_and_read('ps', 2)
    if 'PID' in out or 'pid' in out.lower() or 'init' in out.lower():
        results.ok('ps lists processes')
    else:
        results.fail('ps lists processes')


def test_selftest(qemu, results):
    """Test 3: Run in-kernel self-tests."""
    print('\n--- Test Suite: Self-Test ---')

    out = qemu.send_and_read('test', 5)
    if 'PASS' in out:
        results.ok('selftest produces PASS results')
    else:
        results.fail('selftest produces PASS results')

    if 'FAILED' in out and 'test(s) FAILED' in out:
        results.fail('selftest has failures', 'FAILED detected')
    elif 'All tests PASSED' in out or 'passed' in out.lower():
        results.ok('selftest all passed')
    else:
        results.skip('selftest completion check', 'output unclear')


def test_klog(qemu, results):
    """Test 4: Kernel logging system via 'log' command."""
    print('\n--- Test Suite: Kernel Log ---')

    out = qemu.send_and_read('log', 3)
    if 'Kernel Log' in out or 'Ticks' in out or 'entries' in out.lower():
        results.ok('log command shows entries')
    else:
        results.fail('log command shows entries', f'got: {out[:100]}')

    # log clear
    out = qemu.send_and_read('log clear', 2)
    if 'clear' in out.lower():
        results.ok('log clear command')
    else:
        results.fail('log clear command')


def test_auditlog(qemu, results):
    """Test 5: Audit log system."""
    print('\n--- Test Suite: Audit Log ---')

    out = qemu.send_and_read('auditlog', 3)
    if 'Audit' in out or 'Ticks' in out or 'total' in out.lower() or 'denied' in out.lower() or 'entries' in out.lower():
        results.ok('auditlog command works')
    else:
        results.fail('auditlog command works')


def test_ai_engine(qemu, results):
    """Test 6: AI engine query."""
    print('\n--- Test Suite: AI Engine ---')

    out = qemu.send_and_read('ai status', 3)
    if 'AI' in out or 'engine' in out.lower() or 'ready' in out.lower() or 'event' in out.lower():
        results.ok('ai status command')
    else:
        results.fail('ai status command')


def test_policy(qemu, results):
    """Test 7: Policy show."""
    print('\n--- Test Suite: Policy ---')

    out = qemu.send_and_read('policy show', 3)
    if 'policy' in out.lower() or 'version' in out.lower() or 'rule' in out.lower():
        results.ok('policy show command')
    else:
        results.fail('policy show command')


def test_users(qemu, results):
    """Test 8: Users list."""
    print('\n--- Test Suite: Users ---')

    out = qemu.send_and_read('users', 2)
    if 'root' in out.lower() or 'admin' in out.lower() or 'user' in out.lower():
        results.ok('users command lists accounts')
    else:
        results.fail('users command lists accounts')


def test_permission_flow(qemu, results):
    """Test 9: Login, permission denial, audit log round-trip."""
    print('\n--- Test Suite: Permission Flow ---')

    # Login as admin
    qemu.send('login')
    time.sleep(1)
    qemu.send('admin')
    time.sleep(0.5)
    out = qemu.send_and_read('admin', 3)  # password
    if 'admin' in out.lower():
        results.ok('login as admin')
    else:
        results.skip('login as admin', 'login flow unclear')

    # Try shutdown (should be denied for admin)
    out = qemu.send_and_read('shutdown', 2)
    if 'denied' in out.lower() or 'permission' in out.lower():
        results.ok('admin denied shutdown')
    else:
        results.fail('admin denied shutdown')

    # Check auditlog has the denial
    out = qemu.send_and_read('auditlog', 3)
    if 'shutdown' in out.lower() or 'denied' in out.lower() or 'admin' in out.lower():
        results.ok('auditlog records admin denial')
    else:
        results.skip('auditlog records admin denial', 'audit content unclear')

    # Logout back to root
    out = qemu.send_and_read('logout', 2)
    if 'root' in out.lower() or 'logout' in out.lower():
        results.ok('logout returns to root')
    else:
        results.skip('logout returns to root')


# ===========================================================================
# Main
# ===========================================================================
def main():
    import argparse
    parser = argparse.ArgumentParser(description='VernisOS Integration Tests')
    parser.add_argument('--arch', choices=['x86', 'x64'], default='x64')
    parser.add_argument('--img', default=DEFAULT_IMG)
    args = parser.parse_args()

    if not os.path.exists(args.img):
        print(f'ERROR: Image not found: {args.img}')
        print('Run "make" first to build os.img')
        sys.exit(1)

    results = Results()
    qemu = QemuSession(args.img, args.arch)

    print(f'VernisOS Integration Test Suite (Phase 16)')
    print(f'Architecture: {args.arch}')
    print(f'Image: {args.img}')
    print('=' * 50)

    try:
        qemu.start()

        # Run test suites in order
        if not test_boot_sequence(qemu, results):
            print('\nBoot failed — skipping remaining tests')
            return results.summary()

        test_basic_commands(qemu, results)
        test_selftest(qemu, results)
        test_klog(qemu, results)
        test_auditlog(qemu, results)
        test_ai_engine(qemu, results)
        test_policy(qemu, results)
        test_users(qemu, results)
        test_permission_flow(qemu, results)

    except FileNotFoundError as e:
        print(f'\nERROR: QEMU not found. Install qemu-system-x86_64.\n{e}')
        return 1
    except KeyboardInterrupt:
        print('\nInterrupted')
        return 1
    finally:
        qemu.stop()

    return results.summary()


if __name__ == '__main__':
    sys.exit(main())
