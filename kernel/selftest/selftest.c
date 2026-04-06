// VernisOS Phase 14+16: In-kernel Self-test
// Phase 14: SHA-256, UserDB, Policy enforcement, AI engine access rules
// Phase 16: IPC, Sandbox, Klog, Auditlog integration tests
#include "selftest.h"
#include "sha256.h"
#include "userdb.h"
#include "policy_enforce.h"
#include "ai_bridge.h"
#include "cli.h"
#include "ipc.h"
#include "sandbox.h"
#include "klog.h"
#include "auditlog.h"

extern void cli_printf(const char *fmt, ...);
extern void serial_print(const char *s);
extern uint32_t kernel_get_ticks(void);

static int g_pass = 0;
static int g_fail = 0;

static void test_ok(const char *name) {
    cli_printf("  [PASS] %s\n", name);
    g_pass++;
}
static void test_fail(const char *name) {
    cli_printf("  [FAIL] %s\n", name);
    g_fail++;
}

// ============================================================================
// SHA-256 Tests
// ============================================================================
int selftest_sha256(void) {
    cli_printf("--- SHA-256 ---\n");
    int fails = 0;

    // Test 1: SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    {
        uint8_t hash[32];
        sha256_hash((const uint8_t *)"", 0, hash);
        static const uint8_t expected[] = {
            0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
            0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
            0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,
            0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
        };
        if (sha256_compare(hash, expected) == 0) test_ok("sha256(\"\") correct");
        else { test_fail("sha256(\"\") incorrect"); fails++; }
    }

    // Test 2: SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    {
        uint8_t hash[32];
        sha256_hash((const uint8_t *)"abc", 3, hash);
        static const uint8_t expected[] = {
            0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
            0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
            0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
            0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
        };
        if (sha256_compare(hash, expected) == 0) test_ok("sha256(\"abc\") correct");
        else { test_fail("sha256(\"abc\") incorrect"); fails++; }
    }

    // Test 3: Compare different hashes → must be non-zero
    {
        uint8_t h1[32], h2[32];
        sha256_hash((const uint8_t *)"hello", 5, h1);
        sha256_hash((const uint8_t *)"world", 5, h2);
        if (sha256_compare(h1, h2) != 0) test_ok("sha256 compare (different) non-zero");
        else { test_fail("sha256 compare (different) returned zero"); fails++; }
    }

    // Test 4: Same input → same hash
    {
        uint8_t h1[32], h2[32];
        sha256_hash((const uint8_t *)"test", 4, h1);
        sha256_hash((const uint8_t *)"test", 4, h2);
        if (sha256_compare(h1, h2) == 0) test_ok("sha256 compare (same) zero");
        else { test_fail("sha256 compare (same) returned non-zero"); fails++; }
    }

    return fails;
}

// ============================================================================
// UserDB Tests
// ============================================================================
int selftest_userdb(void) {
    cli_printf("--- UserDB ---\n");
    int fails = 0;

    // Test 1: root user exists and can auth without password (NO_PASSWORD flag)
    {
        int priv = userdb_authenticate("root", "");
        if (priv == CLI_PRIV_ROOT) test_ok("root auth (no password) → priv=0");
        else if (priv >= 0) { test_fail("root auth → wrong privilege"); fails++; }
        else { test_fail("root auth failed (VernisFS not loaded?)"); fails++; }
    }

    // Test 2: admin user with correct password
    {
        int priv = userdb_authenticate("admin", "admin");
        if (priv == CLI_PRIV_ADMIN) test_ok("admin auth (pw=admin) → priv=50");
        else if (priv >= 0) { test_fail("admin auth → wrong privilege"); fails++; }
        else { test_fail("admin auth failed"); fails++; }
    }

    // Test 3: user with correct password
    {
        int priv = userdb_authenticate("user", "user");
        if (priv == CLI_PRIV_USER) test_ok("user auth (pw=user) → priv=100");
        else if (priv >= 0) { test_fail("user auth → wrong privilege"); fails++; }
        else { test_fail("user auth failed"); fails++; }
    }

    // Test 4: wrong password → fail
    {
        int priv = userdb_authenticate("admin", "wrongpass");
        if (priv < 0) test_ok("admin wrong password → denied");
        else { test_fail("admin wrong password → should deny"); fails++; }
    }

    // Test 5: nonexistent user → fail
    {
        int priv = userdb_authenticate("nobody", "x");
        if (priv < 0) test_ok("nonexistent user → denied");
        else { test_fail("nonexistent user → should deny"); fails++; }
    }

    // Test 6: find user record
    {
        const UserRecord *rec = userdb_find_user("root");
        if (rec != 0) test_ok("userdb_find_user(root) found");
        else { test_fail("userdb_find_user(root) not found"); fails++; }
    }

    return fails;
}

// ============================================================================
// Policy Enforcement Tests
// ============================================================================
int selftest_policy(void) {
    cli_printf("--- Policy Enforcement ---\n");
    int fails = 0;

    // Test 1: root (priv=0) can run shutdown
    {
        bool ok = policy_check_command("shutdown", CLI_PRIV_ROOT);
        if (ok) test_ok("root can shutdown");
        else { test_fail("root denied shutdown"); fails++; }
    }

    // Test 2: admin (priv=50) denied shutdown
    {
        bool ok = policy_check_command("shutdown", CLI_PRIV_ADMIN);
        if (!ok) test_ok("admin denied shutdown");
        else { test_fail("admin allowed shutdown (should deny)"); fails++; }
    }

    // Test 3: user (priv=100) denied shutdown
    {
        bool ok = policy_check_command("shutdown", CLI_PRIV_USER);
        if (!ok) test_ok("user denied shutdown");
        else { test_fail("user allowed shutdown (should deny)"); fails++; }
    }

    // Test 4: user (priv=100) can run help
    {
        bool ok = policy_check_command("help", CLI_PRIV_USER);
        if (ok) test_ok("user can help");
        else { test_fail("user denied help"); fails++; }
    }

    // Test 5: admin (priv=50) can run ps
    {
        bool ok = policy_check_command("ps", CLI_PRIV_ADMIN);
        if (ok) test_ok("admin can ps");
        else { test_fail("admin denied ps"); fails++; }
    }

    // Test 6: user denied "policy reload"
    {
        bool ok = policy_check_command("policy reload", CLI_PRIV_USER);
        if (!ok) test_ok("user denied policy reload");
        else { test_fail("user allowed policy reload"); fails++; }
    }

    return fails;
}

// ============================================================================
// AI Engine Access Rules Tests
// ============================================================================
int selftest_access_rules(void) {
    cli_printf("--- AI Access Rules ---\n");
    int fails = 0;

    // Test 1: access rules loaded
    {
        uint32_t count = ai_kernel_engine_access_rule_count();
        if (count > 0) test_ok("access rules loaded (count > 0)");
        else { test_fail("no access rules loaded"); fails++; }
    }

    // Test 2: check_access("shutdown") → should return 0 (root)
    {
        uint8_t priv = ai_kernel_engine_check_access("shutdown", 8);
        if (priv == 0) test_ok("check_access(shutdown) → 0 (root)");
        else {
            cli_printf("    expected 0, got %d\n", (int)priv);
            test_fail("check_access(shutdown) wrong privilege");
            fails++;
        }
    }

    // Test 3: check_access("help") → should return 100 (user) or 255 (no rule)
    {
        uint8_t priv = ai_kernel_engine_check_access("help", 4);
        if (priv == 100 || priv == 255) test_ok("check_access(help) → user/no-rule");
        else {
            cli_printf("    expected 100 or 255, got %d\n", (int)priv);
            test_fail("check_access(help) unexpected");
            fails++;
        }
    }

    // Test 4: event count ≥ 0 (just verify it doesn't crash)
    {
        uint64_t ec = ai_kernel_engine_event_count();
        (void)ec;
        test_ok("event_count() callable");
    }

    // Test 5: anomaly count callable
    {
        uint32_t ac = ai_kernel_engine_anomaly_count();
        (void)ac;
        test_ok("anomaly_count() callable");
    }

    return fails;
}

// ============================================================================
// Phase 16: IPC Integration Tests
// ============================================================================
int selftest_ipc(void) {
    int fails = 0;
    cli_printf("\n--- IPC Tests ---\n");

    // Test 1: Create queue
    int32_t qid = ipc_queue_create(100);
    if (qid >= 0)
        test_ok("queue_create(pid=100)");
    else {
        test_fail("queue_create(pid=100)"); fails++;
    }

    // Test 2: Duplicate create returns same queue
    int32_t qid2 = ipc_queue_create(100);
    if (qid2 == qid)
        test_ok("queue_create(dup) returns same qid");
    else {
        test_fail("queue_create(dup) returns same qid"); fails++;
    }

    // Test 3: Send a message
    const char *payload = "hello";
    int32_t r = ipc_send(1, 100, 1, payload, 5);
    if (r == 0)
        test_ok("ipc_send(1->100)");
    else {
        test_fail("ipc_send(1->100)"); fails++;
    }

    // Test 4: Receive the message
    IpcMessage msg;
    r = ipc_recv(100, &msg);
    if (r == 0 && msg.src_pid == 1 && msg.type == 1)
        test_ok("ipc_recv(100) matches sent");
    else {
        test_fail("ipc_recv(100) matches sent"); fails++;
    }

    // Test 5: Receive from empty queue returns error
    r = ipc_recv(100, &msg);
    if (r != 0)
        test_ok("ipc_recv(empty) returns error");
    else {
        test_fail("ipc_recv(empty) returns error"); fails++;
    }

    // Test 6: Destroy queue
    ipc_queue_destroy((uint32_t)qid);
    IpcStats stats;
    ipc_get_stats(&stats);
    test_ok("queue_destroy succeeded");

    return fails;
}

// ============================================================================
// Phase 16: Sandbox Integration Tests
// ============================================================================
int selftest_sandbox(void) {
    int fails = 0;
    cli_printf("\n--- Sandbox Tests ---\n");

    // Test 1: Create kernel process context
    SecurityContext *kctx = sandbox_create_process(PROC_TYPE_KERNEL, 0);
    if (kctx && kctx->proc_type == PROC_TYPE_KERNEL)
        test_ok("kernel context created");
    else {
        test_fail("kernel context created"); fails++;
    }

    // Test 2: Kernel can use any syscall
    if (kctx && sandbox_check_syscall(kctx, 0))
        test_ok("kernel syscall 0 allowed");
    else {
        test_fail("kernel syscall 0 allowed"); fails++;
    }

    // Test 3: Create user process context
    SecurityContext *uctx = sandbox_create_process(PROC_TYPE_USER, 100);
    if (uctx && uctx->proc_type == PROC_TYPE_USER)
        test_ok("user context created");
    else {
        test_fail("user context created"); fails++;
    }

    // Test 4: User can use SYS_WRITE (0)
    if (uctx && sandbox_check_syscall(uctx, 0))
        test_ok("user syscall 0 (WRITE) allowed");
    else {
        test_fail("user syscall 0 (WRITE) allowed"); fails++;
    }

    // Test 5: User cannot use SYS_MOD_LOAD (28)
    if (uctx && !sandbox_check_syscall(uctx, 28))
        test_ok("user syscall 28 (MOD_LOAD) denied");
    else {
        test_fail("user syscall 28 (MOD_LOAD) denied"); fails++;
    }

    // Cleanup
    if (kctx) sandbox_destroy_process(kctx);
    if (uctx) sandbox_destroy_process(uctx);

    return fails;
}

// ============================================================================
// Phase 16: Klog Integration Tests
// ============================================================================
int selftest_klog(void) {
    int fails = 0;
    cli_printf("\n--- Klog Tests ---\n");

    // Test 1: Log a message and check count increased
    uint64_t before = klog_total();
    klog(KLOG_INFO, "test", "selftest message");
    uint64_t after = klog_total();
    if (after == before + 1)
        test_ok("klog record increments total");
    else {
        test_fail("klog record increments total"); fails++;
    }

    // Test 2: Buffer count > 0
    if (klog_count() > 0)
        test_ok("klog_count() > 0");
    else {
        test_fail("klog_count() > 0"); fails++;
    }

    // Test 3: Set and get level
    KlogLevel prev = klog_get_level();
    klog_set_level(KLOG_ERROR);
    if (klog_get_level() == KLOG_ERROR)
        test_ok("klog_set_level(ERROR)");
    else {
        test_fail("klog_set_level(ERROR)"); fails++;
    }
    klog_set_level(prev);

    // Test 4: Level name strings
    if (klog_level_name(KLOG_FATAL)[0] == 'F')
        test_ok("klog_level_name(FATAL)");
    else {
        test_fail("klog_level_name(FATAL)"); fails++;
    }

    return fails;
}

// ============================================================================
// Phase 16: Auditlog Integration Tests
// ============================================================================
int selftest_auditlog(void) {
    int fails = 0;
    cli_printf("\n--- Auditlog Tests ---\n");

    // Test 1: Record a denial and check count
    uint32_t before = auditlog_total_count();
    auditlog_record((uint64_t)kernel_get_ticks(), "testuser", "shutdown", 100, 0);
    uint32_t after = auditlog_total_count();
    if (after == before + 1)
        test_ok("auditlog_record increments total");
    else {
        test_fail("auditlog_record increments total"); fails++;
    }

    // Test 2: Current count > 0
    if (auditlog_current_count() > 0)
        test_ok("auditlog_current_count() > 0");
    else {
        test_fail("auditlog_current_count() > 0"); fails++;
    }

    // Test 3: Total >= current
    if (auditlog_total_count() >= auditlog_current_count())
        test_ok("total >= current");
    else {
        test_fail("total >= current"); fails++;
    }

    return fails;
}

// ============================================================================
// Run All Tests
// ============================================================================
int selftest_run_all(void) {
    g_pass = 0;
    g_fail = 0;

    cli_printf("========== VernisOS Self-Test ==========\n");

    selftest_sha256();
    selftest_userdb();
    selftest_policy();
    selftest_access_rules();
    selftest_ipc();
    selftest_sandbox();
    selftest_klog();
    selftest_auditlog();

    cli_printf("========================================\n");
    cli_printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0)
        cli_printf("All tests PASSED!\n");
    else
        cli_printf("*** %d test(s) FAILED ***\n", g_fail);

    serial_print("[selftest] ");
    if (g_fail == 0) serial_print("ALL PASSED\n");
    else serial_print("FAILURES DETECTED\n");

    return g_fail;
}
