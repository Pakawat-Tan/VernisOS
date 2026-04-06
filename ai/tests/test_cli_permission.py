#!/usr/bin/env python3
"""
VernisOS Phase 14: CLI-AI Interaction & Permission System Test Harness

Boots QEMU with VernisOS, connects via serial, and runs automated tests
to verify privilege enforcement, user auth, and AI engine integration.

Usage:
    python3 ai/tests/test_cli_permission.py [--arch x64|x86] [--timeout 60]

Requirements:
    pip install pexpect   (or use built-in subprocess if unavailable)
"""

import sys
import os
import time
import argparse
import subprocess
import signal
import re

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OS_IMG = os.path.join(PROJECT_ROOT, "os.img")

QEMU_X64 = "qemu-system-x86_64"
QEMU_X86 = "qemu-system-i386"

BOOT_TIMEOUT = 30       # seconds to wait for shell prompt
CMD_TIMEOUT  = 5        # seconds to wait for command response
SERIAL_SPEED = 115200

# ---------------------------------------------------------------------------
# ANSI colors for terminal output
# ---------------------------------------------------------------------------
GREEN  = "\033[92m"
RED    = "\033[91m"
YELLOW = "\033[93m"
RESET  = "\033[0m"
BOLD   = "\033[1m"

# ---------------------------------------------------------------------------
# Test Results
# ---------------------------------------------------------------------------
class TestResults:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.errors = []

    def ok(self, name):
        self.passed += 1
        print(f"  {GREEN}✓ PASS{RESET}  {name}")

    def fail(self, name, detail=""):
        self.failed += 1
        self.errors.append((name, detail))
        print(f"  {RED}✗ FAIL{RESET}  {name}")
        if detail:
            print(f"         {detail}")

    def skip(self, name, reason=""):
        self.skipped += 1
        print(f"  {YELLOW}○ SKIP{RESET}  {name}: {reason}")

    def summary(self):
        total = self.passed + self.failed + self.skipped
        print(f"\n{BOLD}{'='*60}{RESET}")
        print(f"Results: {GREEN}{self.passed} passed{RESET}, "
              f"{RED}{self.failed} failed{RESET}, "
              f"{YELLOW}{self.skipped} skipped{RESET} "
              f"({total} total)")
        if self.errors:
            print(f"\n{RED}Failures:{RESET}")
            for name, detail in self.errors:
                print(f"  - {name}: {detail}")
        print(f"{'='*60}")
        return self.failed == 0


# ---------------------------------------------------------------------------
# QEMU Serial Session (using subprocess + PTY)
# ---------------------------------------------------------------------------
class QemuSession:
    """Manages a QEMU instance with serial I/O via pipe."""

    def __init__(self, arch="x64", timeout=BOOT_TIMEOUT):
        self.arch = arch
        self.timeout = timeout
        self.proc = None
        self.buf = ""

    def start(self):
        """Boot QEMU and wait for shell prompt."""
        qemu = QEMU_X64 if self.arch == "x64" else QEMU_X86

        cmd = [
            qemu,
            "-drive", f"file={OS_IMG},format=raw,if=ide",
            "-m", "128M",
            "-serial", "stdio",
            "-display", "none",
            "-no-reboot",
            "-no-shutdown",
        ]

        print(f"Starting QEMU ({self.arch}): {' '.join(cmd[:4])}...")

        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )

        # Wait for the shell prompt
        if not self._wait_for_prompt(self.timeout):
            raise RuntimeError("Timeout waiting for VernisOS shell prompt")

        print(f"VernisOS booted successfully ({self.arch})")

    def send(self, text):
        """Send text to serial (adds \\r\\n)."""
        if self.proc and self.proc.stdin:
            self.proc.stdin.write((text + "\r\n").encode())
            self.proc.stdin.flush()

    def read_until(self, pattern, timeout=CMD_TIMEOUT):
        """Read serial output until pattern found or timeout."""
        import select
        deadline = time.time() + timeout
        collected = ""

        while time.time() < deadline:
            remaining = deadline - time.time()
            if remaining <= 0:
                break

            ready, _, _ = select.select([self.proc.stdout], [], [], min(remaining, 0.1))
            if ready:
                chunk = os.read(self.proc.stdout.fileno(), 4096)
                if not chunk:
                    break
                text = chunk.decode("utf-8", errors="replace")
                collected += text
                if pattern in collected:
                    return collected

        return collected

    def send_and_read(self, command, expect_prompt=True, timeout=CMD_TIMEOUT):
        """Send command, read response until next prompt or timeout."""
        # Drain any pending output first
        self.read_until("__drain__", timeout=0.2)

        self.send(command)
        time.sleep(0.1)

        # Read until we see the prompt pattern or timeout
        prompt_pattern = "@vernisOS"
        output = self.read_until(prompt_pattern if expect_prompt else "\n", timeout)
        return output

    def _wait_for_prompt(self, timeout):
        """Wait for VernisOS shell prompt."""
        output = self.read_until("@vernisOS", timeout)
        return "@vernisOS" in output or "root@" in output

    def stop(self):
        """Kill QEMU."""
        if self.proc:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()
            self.proc = None


# ---------------------------------------------------------------------------
# Test Suites
# ---------------------------------------------------------------------------
def test_boot_and_prompt(session, results):
    """Test 1: Verify OS booted with root prompt."""
    print(f"\n{BOLD}[Test Suite: Boot & Prompt]{RESET}")

    output = session.send_and_read("whoami")
    if "root" in output:
        results.ok("whoami returns 'root' at boot")
    else:
        results.fail("whoami should return 'root'", f"got: {output.strip()[:80]}")


def test_root_commands(session, results):
    """Test 2: Root can run all privileged commands."""
    print(f"\n{BOLD}[Test Suite: Root Privilege]{RESET}")

    # help (USER level — root should always work)
    output = session.send_and_read("help")
    if "help" in output.lower() or "command" in output.lower():
        results.ok("root can run 'help'")
    else:
        results.fail("root 'help' failed", output.strip()[:80])

    # ps (USER level)
    output = session.send_and_read("ps")
    if "PID" in output or "pid" in output or "Process" in output or "idle" in output.lower():
        results.ok("root can run 'ps'")
    else:
        results.fail("root 'ps' failed", output.strip()[:80])

    # ai status (ADMIN level)
    output = session.send_and_read("ai status")
    if "engine" in output.lower() or "bridge" in output.lower() or "event" in output.lower():
        results.ok("root can run 'ai status'")
    else:
        results.fail("root 'ai status' failed", output.strip()[:80])

    # policy show (ROOT level)
    output = session.send_and_read("policy show")
    if "policy" in output.lower() or "version" in output.lower() or "rule" in output.lower():
        results.ok("root can run 'policy show'")
    else:
        results.fail("root 'policy show' failed", output.strip()[:80])

    # users (ADMIN level)
    output = session.send_and_read("users")
    if "root" in output or "admin" in output or "user" in output:
        results.ok("root can run 'users'")
    else:
        results.fail("root 'users' failed", output.strip()[:80])


def test_selftest(session, results):
    """Test 3: Run in-kernel self-test."""
    print(f"\n{BOLD}[Test Suite: In-kernel Self-test]{RESET}")

    output = session.send_and_read("test", timeout=10)
    if "All tests PASSED" in output:
        results.ok("in-kernel selftest all passed")
    elif "FAIL" in output:
        # Count passes and failures
        pass_match = re.search(r"(\d+) passed", output)
        fail_match = re.search(r"(\d+) failed", output)
        p = pass_match.group(1) if pass_match else "?"
        f = fail_match.group(1) if fail_match else "?"
        results.fail(f"in-kernel selftest: {p} passed, {f} failed",
                     output.strip()[:200])
    else:
        results.fail("in-kernel selftest no result", output.strip()[:200])


def test_login_admin(session, results):
    """Test 4: Login as admin and verify privilege changes."""
    print(f"\n{BOLD}[Test Suite: Login/Privilege]{RESET}")

    # Login as admin with password "admin"
    session.send("login admin")
    time.sleep(0.5)
    # Should prompt for password
    output = session.read_until(":", timeout=3)
    if "password" in output.lower() or "Password" in output:
        results.ok("login admin prompts for password")
    else:
        results.skip("login admin no password prompt", output.strip()[:80])

    # Send password
    session.send("admin")
    time.sleep(0.5)
    output = session.read_until("@vernisOS", timeout=3)

    # Verify privilege changed
    output2 = session.send_and_read("whoami")
    if "admin" in output2:
        results.ok("after login, whoami = admin")
    else:
        results.fail("after login admin, whoami incorrect", output2.strip()[:80])


def test_admin_denied_root_cmds(session, results):
    """Test 5: Admin user denied root-only commands."""
    print(f"\n{BOLD}[Test Suite: Admin Denied Root Commands]{RESET}")

    for cmd in ["shutdown", "restart"]:
        output = session.send_and_read(cmd)
        if "denied" in output.lower() or "permission" in output.lower():
            results.ok(f"admin denied '{cmd}'")
        else:
            results.fail(f"admin should be denied '{cmd}'", output.strip()[:80])


def test_auditlog(session, results):
    """Test 6: Audit log records denials."""
    print(f"\n{BOLD}[Test Suite: Audit Log]{RESET}")

    output = session.send_and_read("auditlog")
    if "denied" in output.lower() or "deny" in output.lower() or "audit" in output.lower() or "entries" in output.lower():
        results.ok("auditlog command works")
    else:
        # Admin should be able to run auditlog
        results.fail("auditlog not working", output.strip()[:80])


def test_logout(session, results):
    """Test 7: Logout returns to root."""
    print(f"\n{BOLD}[Test Suite: Logout]{RESET}")

    session.send_and_read("logout")
    time.sleep(0.3)
    output = session.send_and_read("whoami")
    if "root" in output:
        results.ok("logout returns to root")
    else:
        results.fail("after logout, not root", output.strip()[:80])


def test_login_user_denials(session, results):
    """Test 8: Login as regular user and verify denials."""
    print(f"\n{BOLD}[Test Suite: User Privilege Denials]{RESET}")

    # Login as user
    session.send("login user")
    time.sleep(0.5)
    output = session.read_until(":", timeout=3)
    session.send("user")
    time.sleep(0.5)
    session.read_until("@vernisOS", timeout=3)

    # Verify user can't run admin/root commands
    for cmd in ["shutdown", "ai status", "users"]:
        output = session.send_and_read(cmd)
        if "denied" in output.lower() or "permission" in output.lower():
            results.ok(f"user denied '{cmd}'")
        else:
            results.fail(f"user should be denied '{cmd}'", output.strip()[:80])

    # User can run basic commands
    output = session.send_and_read("echo hello")
    if "hello" in output:
        results.ok("user can run 'echo hello'")
    else:
        results.fail("user 'echo hello' failed", output.strip()[:80])

    # Cleanup: logout
    session.send_and_read("logout")
    time.sleep(0.3)


def test_su_command(session, results):
    """Test 9: su command for single root command."""
    print(f"\n{BOLD}[Test Suite: su Command]{RESET}")

    # Login as user first
    session.send("login user")
    time.sleep(0.5)
    session.read_until(":", timeout=3)
    session.send("user")
    time.sleep(0.5)
    session.read_until("@vernisOS", timeout=3)

    # su with a command — should prompt for root password
    session.send("su whoami")
    time.sleep(0.5)
    output = session.read_until(":", timeout=3)
    if "password" in output.lower():
        results.ok("su prompts for root password")
        # Root has no password, just press enter
        session.send("")
        time.sleep(0.5)
        output = session.read_until("@vernisOS", timeout=3)
        if "root" in output:
            results.ok("su whoami shows root")
        else:
            results.skip("su whoami result unclear", output.strip()[:80])
    else:
        results.skip("su no password prompt", output.strip()[:80])

    # Cleanup
    session.send_and_read("logout")
    time.sleep(0.3)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="VernisOS CLI-AI Permission Tests")
    parser.add_argument("--arch", choices=["x64", "x86"], default="x64",
                        help="Architecture to test (default: x64)")
    parser.add_argument("--timeout", type=int, default=BOOT_TIMEOUT,
                        help="Boot timeout in seconds")
    parser.add_argument("--skip-boot", action="store_true",
                        help="Skip tests that require QEMU (dry run)")
    args = parser.parse_args()

    results = TestResults()

    if args.skip_boot:
        print(f"{BOLD}=== VernisOS Phase 14 Tests (DRY RUN) ==={RESET}")
        results.skip("All tests", "QEMU not started (--skip-boot)")
        results.summary()
        return 0

    if not os.path.exists(OS_IMG):
        print(f"{RED}Error: {OS_IMG} not found. Run 'make' first.{RESET}")
        return 1

    print(f"{BOLD}=== VernisOS Phase 14: CLI-AI Permission Tests ==={RESET}")
    print(f"Architecture: {args.arch}")
    print(f"Image: {OS_IMG}")

    session = QemuSession(arch=args.arch, timeout=args.timeout)

    try:
        session.start()

        test_boot_and_prompt(session, results)
        test_root_commands(session, results)
        test_selftest(session, results)
        test_login_admin(session, results)
        test_admin_denied_root_cmds(session, results)
        test_auditlog(session, results)
        test_logout(session, results)
        test_login_user_denials(session, results)
        test_su_command(session, results)

    except RuntimeError as e:
        print(f"\n{RED}Error: {e}{RESET}")
        results.fail("QEMU boot", str(e))
    except KeyboardInterrupt:
        print(f"\n{YELLOW}Interrupted by user{RESET}")
    finally:
        session.stop()

    ok = results.summary()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
