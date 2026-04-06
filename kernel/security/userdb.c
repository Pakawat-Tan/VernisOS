// userdb.c — VernisOS User Database implementation
//
// Phase 13: Loads user records from VernisFS, authenticates with SHA-256.

#include "userdb.h"
#include "sha256.h"
#include "vfs.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void serial_print(const char *s);
extern void serial_print_dec(uint32_t val);

// In-memory user DB
static UserRecord g_users[USERDB_MAX_USERS];
static uint8_t    g_user_count = 0;
static bool       g_userdb_ready = false;

// Helper: string compare
static int udb_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

// Helper: string length
static size_t udb_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

bool userdb_init(void) {
    serial_print("[userdb] Loading user database...\n");

    // Read /etc/shadow from VernisFS
    uint8_t buf[USERDB_MAX_USERS * sizeof(UserRecord)];
    int bytes = kfs_read_file(USERDB_PATH, buf, sizeof(buf));

    if (bytes <= 0) {
        serial_print("[userdb] No user database found\n");
        g_user_count = 0;
        g_userdb_ready = false;
        return false;
    }

    // Parse user records
    g_user_count = 0;
    size_t offset = 0;
    while (offset + sizeof(UserRecord) <= (size_t)bytes &&
           g_user_count < USERDB_MAX_USERS) {
        const UserRecord *rec = (const UserRecord *)(buf + offset);

        // Skip empty records
        if (rec->username[0] == 0) {
            offset += sizeof(UserRecord);
            continue;
        }

        g_users[g_user_count] = *rec;
        g_user_count++;
        offset += sizeof(UserRecord);
    }

    serial_print("[userdb] Loaded ");
    serial_print_dec(g_user_count);
    serial_print(" users\n");

    g_userdb_ready = true;
    return true;
}

int userdb_authenticate(const char *username, const char *password) {
    if (!g_userdb_ready) return -1;

    const UserRecord *user = userdb_find_user(username);
    if (!user) return -1;

    // Check if user is active
    if (!(user->flags & USER_FLAG_ACTIVE)) return -1;

    // Check if locked
    if (user->flags & USER_FLAG_LOCKED) return -1;

    // No-password users can authenticate without a password
    if (user->flags & USER_FLAG_NO_PASSWORD) {
        return (int)user->privilege;
    }

    // Password required
    if (!password) return -1;

    // Hash the provided password and compare
    uint8_t hash[SHA256_DIGEST_SIZE];
    size_t pw_len = udb_strlen(password);
    sha256_hash((const uint8_t *)password, pw_len, hash);

    if (sha256_compare(hash, user->password_hash) == 0) {
        return (int)user->privilege;
    }

    return -1; // Wrong password
}

const UserRecord *userdb_find_user(const char *username) {
    if (!g_userdb_ready) return NULL;
    for (uint8_t i = 0; i < g_user_count; i++) {
        if (udb_strcmp(g_users[i].username, username) == 0) {
            return &g_users[i];
        }
    }
    return NULL;
}

uint8_t userdb_user_count(void) {
    return g_user_count;
}

const UserRecord *userdb_get_user(uint8_t index) {
    if (index >= g_user_count) return NULL;
    return &g_users[index];
}

int userdb_find_uid(const char *username) {
    if (!g_userdb_ready || !username) return -1;
    for (uint8_t i = 0; i < g_user_count; i++) {
        if (udb_strcmp(g_users[i].username, username) == 0)
            return (int)i;
    }
    return -1;
}
