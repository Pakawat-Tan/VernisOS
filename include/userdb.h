// userdb.h — VernisOS User Database
//
// Phase 13: User authentication with SHA-256 password hashing.
// Stores user records in VernisFS at /etc/shadow.

#ifndef VERNISOS_USERDB_H
#define VERNISOS_USERDB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define USERDB_MAX_USERS    8
#define USERDB_USERNAME_LEN 32
#define USERDB_HASH_LEN     32    // SHA-256 digest
#define USERDB_PATH         "/etc/shadow"

// User flags
#define USER_FLAG_ACTIVE      0x01
#define USER_FLAG_LOCKED      0x02
#define USER_FLAG_NO_PASSWORD 0x04

// User record (66 bytes on disk)
typedef struct {
    char     username[USERDB_USERNAME_LEN]; // Null-terminated
    uint8_t  password_hash[USERDB_HASH_LEN]; // SHA-256
    uint8_t  privilege;                      // CLI privilege level (0/50/100)
    uint8_t  flags;                          // USER_FLAG_*
} __attribute__((packed)) UserRecord;

// Initialize user DB — load from VernisFS (/etc/shadow)
// Returns true if loaded successfully
bool userdb_init(void);

// Authenticate a user. Returns privilege level (0/50/100) or -1 on failure.
// password can be NULL if user has USER_FLAG_NO_PASSWORD.
int userdb_authenticate(const char *username, const char *password);

// Look up a user record by name. Returns NULL if not found.
const UserRecord *userdb_find_user(const char *username);

// Get the number of loaded users
uint8_t userdb_user_count(void);

// Get user record by index (for listing)
const UserRecord *userdb_get_user(uint8_t index);

// Phase 47: Get uid for a username (index in user table, 0=root)
// Returns -1 if user not found.
int userdb_find_uid(const char *username);

#endif // VERNISOS_USERDB_H
