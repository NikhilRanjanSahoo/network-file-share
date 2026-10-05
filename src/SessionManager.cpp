#include "../include/SessionManager.h"
#include "../include/Crypto.h"
#include "../include/config.h"

using Clock = std::chrono::steady_clock;

void SessionManager::purgeExpiredLocked() {
    const auto now = Clock::now();
    const auto ttl = std::chrono::seconds(cfg::SESSION_IDLE_SEC);
    for (auto it = sessions.begin(); it != sessions.end();) {
        if (now - it->second.last_seen > ttl) it = sessions.erase(it);
        else ++it;
    }
    for (auto it = attempts.begin(); it != attempts.end();) {
        if (it->second.failures >= cfg::MAX_LOGIN_FAILURES && now >= it->second.locked_until) {
            it = attempts.erase(it);
        } else {
            ++it;
        }
    }
}

std::string SessionManager::create(int user_id, const std::string& username, const std::string& role) {
    std::string token = randomHex(16);   // 128 bits -> 32 hex chars == SESSION_TOKEN_LEN

    Session s;
    s.user_id = user_id;
    s.username = username;
    s.role = role;
    s.last_seen = Clock::now();

    std::lock_guard<std::mutex> lock(mtx);
    purgeExpiredLocked();
    sessions[token] = s;
    return token;
}

bool SessionManager::validate(const std::string& token, Session& out) {
    if (token.empty()) return false;

    std::lock_guard<std::mutex> lock(mtx);
    auto it = sessions.find(token);
    if (it == sessions.end()) return false;

    const auto now = Clock::now();
    if (now - it->second.last_seen > std::chrono::seconds(cfg::SESSION_IDLE_SEC)) {
        sessions.erase(it);
        return false;
    }
    it->second.last_seen = now;
    out = it->second;
    return true;
}

void SessionManager::destroy(const std::string& token) {
    std::lock_guard<std::mutex> lock(mtx);
    sessions.erase(token);
}

bool SessionManager::isLockedOut(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = attempts.find(key);
    if (it == attempts.end()) return false;
    if (it->second.failures < cfg::MAX_LOGIN_FAILURES) return false;
    if (Clock::now() < it->second.locked_until) return true;
    attempts.erase(it);   // lockout elapsed: start fresh
    return false;
}

void SessionManager::recordFailure(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    Attempt& a = attempts[key];
    ++a.failures;
    if (a.failures >= cfg::MAX_LOGIN_FAILURES) {
        a.locked_until = Clock::now() + std::chrono::seconds(cfg::LOCKOUT_SEC);
    }
}

void SessionManager::recordSuccess(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);
    attempts.erase(key);
}
