#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// Central place for server-side settings that used to be hardcoded in 10+ places.
namespace cfg {
    constexpr const char* DB_DIR       = "database";
    constexpr const char* DB_PATH      = "database/file_sharing.db";
    constexpr const char* STORAGE_ROOT = "server_storage/public";
    constexpr const char* TEMP_DIR     = "server_storage/tmp";   // same filesystem as STORAGE_ROOT (atomic rename)
    constexpr const char* USERS_DIR    = "server_storage/users";

    constexpr int DEFAULT_PORT        = 8080;
    constexpr int MAX_CLIENTS         = 64;     // concurrent connections
    constexpr int SOCKET_TIMEOUT_SEC  = 30;     // per recv()/send() call

    constexpr int SESSION_IDLE_SEC    = 30 * 60;  // sliding expiry
    constexpr int MAX_LOGIN_FAILURES  = 5;        // per client IP
    constexpr int LOCKOUT_SEC         = 60;
    constexpr int FAILED_LOGIN_DELAY_MS = 300;

    constexpr int MIN_PASSWORD_LEN    = 8;
    constexpr int MAX_CRED_LEN        = 31;       // AuthPayload fields are 32 bytes incl. NUL
}

#endif
