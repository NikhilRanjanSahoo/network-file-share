#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

// Thread-safe store of authenticated sessions, plus a small per-key failed-login
// throttle (key = client IP).
class SessionManager {
public:
    struct Session {
        int user_id = 0;
        std::string username;
        std::string role;
        std::chrono::steady_clock::time_point last_seen;
    };

    // Creates a session and returns its random 32-hex-char token.
    std::string create(int user_id, const std::string& username, const std::string& role);

    // Looks up a token, enforces idle expiry and refreshes last_seen on success.
    bool validate(const std::string& token, Session& out);

    void destroy(const std::string& token);

    // Brute-force protection.
    bool isLockedOut(const std::string& key);
    void recordFailure(const std::string& key);
    void recordSuccess(const std::string& key);

private:
    struct Attempt {
        int failures = 0;
        std::chrono::steady_clock::time_point locked_until{};
    };

    void purgeExpiredLocked();

    std::mutex mtx;
    std::unordered_map<std::string, Session> sessions;
    std::unordered_map<std::string, Attempt> attempts;
};

#endif
