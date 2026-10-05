#include "../include/Database.h"
#include <sstream>

namespace {

typedef std::lock_guard<std::recursive_mutex> Guard;

bool isValidRole(const std::string& r) {
    return r == "Admin" || r == "Faculty" || r == "Student";
}

bool isValidScope(const std::string& s) {
    return s == "public" || s == "home";
}

std::string colText(sqlite3_stmt* stmt, int col) {
    const unsigned char* t = sqlite3_column_text(stmt, col);
    return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
}

bool runBound(sqlite3* db, const char* sql, const std::string& arg) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, arg.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

}

bool Database::initializePolicyTables() {
    const std::string create =
        "CREATE TABLE IF NOT EXISTS role_policy ("
        "role TEXT NOT NULL, "
        "scope TEXT NOT NULL, "
        "can_read INTEGER NOT NULL, "
        "can_write INTEGER NOT NULL, "
        "can_delete INTEGER NOT NULL, "
        "PRIMARY KEY(role, scope));";
    const std::string seed =
        "INSERT OR IGNORE INTO role_policy (role, scope, can_read, can_write, can_delete) VALUES "
        "('Admin','public',1,1,1),('Admin','home',1,1,1),"
        "('Faculty','public',1,1,1),('Faculty','home',1,1,1),"
        "('Student','public',1,1,0),('Student','home',1,1,1);";
    return executeQuery(create) && executeQuery(seed);
}

bool Database::getPolicy(const std::string& role, const std::string& scope,
                         bool& can_read, bool& can_write, bool& can_delete) {
    Guard lock(mtx);
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT can_read, can_write, can_delete FROM role_policy WHERE role = ? AND scope = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, scope.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    if (found) {
        can_read = sqlite3_column_int(stmt, 0) != 0;
        can_write = sqlite3_column_int(stmt, 1) != 0;
        can_delete = sqlite3_column_int(stmt, 2) != 0;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::setPolicy(const std::string& role, const std::string& scope,
                         bool can_read, bool can_write, bool can_delete) {
    if (!isValidRole(role) || !isValidScope(scope)) return false;
    Guard lock(mtx);
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO role_policy (role, scope, can_read, can_write, can_delete) "
                      "VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, scope.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, can_read ? 1 : 0);
    sqlite3_bind_int(stmt, 4, can_write ? 1 : 0);
    sqlite3_bind_int(stmt, 5, can_delete ? 1 : 0);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::string Database::listPolicies() {
    Guard lock(mtx);
    if (!db) return "[-] Database not connected.\n";
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT role, scope, can_read, can_write, can_delete FROM role_policy ORDER BY role, scope;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return "[-] Failed to read policies.\n";
    std::ostringstream out;
    out << "ROLE      | SCOPE  | READ | WRITE | DELETE\n";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out << colText(stmt, 0) << " | " << colText(stmt, 1) << " | "
            << sqlite3_column_int(stmt, 2) << " | " << sqlite3_column_int(stmt, 3) << " | "
            << sqlite3_column_int(stmt, 4) << "\n";
    }
    sqlite3_finalize(stmt);
    return out.str();
}

std::string Database::listUsers() {
    Guard lock(mtx);
    if (!db) return "[-] Database not connected.\n";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id, username, role FROM users ORDER BY id;", -1, &stmt, nullptr) != SQLITE_OK) {
        return "[-] Failed to read users.\n";
    }
    std::ostringstream out;
    out << "ID | USERNAME | ROLE\n";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out << sqlite3_column_int(stmt, 0) << " | " << colText(stmt, 1) << " | " << colText(stmt, 2) << "\n";
    }
    sqlite3_finalize(stmt);
    return out.str();
}

bool Database::setUserRole(const std::string& username, const std::string& role) {
    if (!isValidRole(role)) return false;
    Guard lock(mtx);
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "UPDATE users SET role = ? WHERE username = ?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE) && sqlite3_changes(db) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteUser(const std::string& username) {
    Guard lock(mtx);
    if (!db) return false;
    if (!executeQuery("BEGIN;")) return false;
    bool ok = runBound(db, "UPDATE files SET owner_id = NULL WHERE owner_id IN (SELECT id FROM users WHERE username = ?);", username) &&
              runBound(db, "UPDATE transfers SET user_id = NULL WHERE user_id IN (SELECT id FROM users WHERE username = ?);", username) &&
              runBound(db, "DELETE FROM users WHERE username = ?;", username);
    ok = ok && sqlite3_changes(db) > 0;
    executeQuery(ok ? "COMMIT;" : "ROLLBACK;");
    return ok;
}

int Database::getFileOwner(const std::string& path) {
    Guard lock(mtx);
    if (!db) return -1;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT owner_id FROM files WHERE path = ? LIMIT 1;", -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    int owner = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        owner = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return owner;
}
