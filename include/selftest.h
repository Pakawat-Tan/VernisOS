// VernisOS Phase 14: In-kernel Self-test
// Verifies: SHA-256, UserDB auth, Policy enforcement, AI engine access rules
#ifndef VERNISOS_SELFTEST_H
#define VERNISOS_SELFTEST_H

#include <stdint.h>

// Run all self-tests. Prints PASS/FAIL per test case.
// Returns number of failures (0 = all passed).
int selftest_run_all(void);

// Individual test suites
int selftest_sha256(void);
int selftest_userdb(void);
int selftest_policy(void);
int selftest_access_rules(void);

// Phase 16: Integration test suites
int selftest_ipc(void);
int selftest_sandbox(void);
int selftest_klog(void);
int selftest_auditlog(void);

#endif // VERNISOS_SELFTEST_H
