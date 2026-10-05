#include "../include/Database.h"
#include "../include/Crypto.h"
#include <iostream>

namespace {

typedef std::lock_guard<std::recursive_mutex> Guard;

bool isValidRole(const std::string& r) {
    return r == "Admin" || r == "Faculty" || r == "Student";
}

std::string colText(sqlite3_stmt* stmt, int col) {
    const unsigned char* t = sqlite3_column_text(stmt, col);
    return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
}

}  // namespace

Database::Database() : db(nullptr) {}

Database::~Database() {
    disconnect();
}

bool Database::connect(const std::string& db_path) {
    Guard lock(mtx);
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[-] Can't open database: " << (db ? sqlite3_errmsg(db) : "out of memory") << "\n";
        if (db) sqlite3_close(db);
        db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(db, 5000);
    executeQuery("PRAGMA foreign_keys = ON;");   // otherwise FOREIGN KEY clauses are ignored
    executeQuery("PRAGMA journal_mode = WAL;");
    return true;
}

void Database::disconnect() {
    Guard lock(mtx);
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Database::executeQuery(const std::string& query) {
    Guard lock(mtx);
    if (!db) return false;
    char* errMsg = nullptr;
    if (sqlite3_exec(db, query.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "[-] SQL Error: " << (errMsg ? errMsg : "unknown") << "\n";
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::initializeTables() {
    const std::string createUsers =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT UNIQUE NOT NULL, "
        "password TEXT NOT NULL, "
        "role TEXT NOT NULL, "
        "home_directory TEXT, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

    const std::string createFiles =
        "CREATE TABLE IF NOT EXISTS files ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "owner_id INTEGER, "
        "filename TEXT NOT NULL, "
        "path TEXT NOT NULL, "
        "size INTEGER, "
        "checksum TEXT, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "modified_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "FOREIGN KEY(owner_id) REFERENCES users(id));";

    const std::string createTransfers =
        "CREATE TABLE IF NOT EXISTS transfers ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER, "
        "filename TEXT NOT NULL, "
        "operation TEXT NOT NULL, "
        "size INTEGER, "
        "status TEXT, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "FOREIGN KEY(user_id) REFERENCES users(id));";

    return executeQuery(createUsers) && executeQuery(createFiles) && executeQuery(createTransfers);
}

int Database::countUsers() {
    Guard lock(mtx);
    if (!db) return -1;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users;", -1, &stmt, nullptr) != SQLITE_OK) return -1;
    int count = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

bool Database::saveUser(const std::string& username, const std::string& password,
                        const std::string& role, const std::string& home_dir) {
    if (username.empty() || password.empty() || !isValidRole(role)) return false;

    const std::string stored = hashPassword(password);   // slow on purpose; done outside the DB lock

    Guard lock(mtx);
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO users (username, password, role, home_directory) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, stored.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, home_dir.c_str(), -1, SQLITE_TRANSIENT);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool Database::authenticateUser(const std::string& username, const std::string& password,
                                std::string& out_role, int& out_user_id) {
    // Hash computed once, used when the username doesn't exist so that
    // "unknown user" and "wrong password" take about the same time.
    static const std::string kDummyHash = hashPassword("dummy-password-for-timing");

    int id = 0;
    std::string stored, role;
    bool found = false;
    {
        Guard lock(mtx);
        if (!db) return false;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, password, role FROM users WHERE username = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = sqlite3_column_int(stmt, 0);
            stored = colText(stmt, 1);
            role = colText(stmt, 2);
            found = true;
        }
        sqlite3_finalize(stmt);
    }   // DB lock released: the expensive hash below must not block other threads

    const bool ok = verifyPassword(password, found ? stored : kDummyHash);
    if (!found || !ok) return false;

    // Transparently upgrade old plaintext rows to a salted hash.
    if (isLegacyPlaintext(stored)) {
        const std::string upgraded = hashPassword(password);
        Guard lock(mtx);
        sqlite3_stmt* stmt = nullptr;
        if (db && sqlite3_prepare_v2(db, "UPDATE users SET password = ? WHERE id = ?;", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, upgraded.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    out_role = role;
    out_user_id = id;
    return true;
}

std::string Database::getUserRole(const std::string& username) {
    Guard lock(mtx);
    std::string role = "Guest";
    if (!db) return role;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT role FROM users WHERE username = ? LIMIT 1;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string r = colText(stmt, 0);
            if (!r.empty()) role = r;
        }
        sqlite3_finalize(stmt);
    }
    return role;
}

bool Database::saveTransfer(int user_id, const std::string& filename, const std::string& operation,
                            int size, const std::string& status) {
    Guard lock(mtx);
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO transfers (user_id, filename, operation, size, status) VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[-] Failed to prepare audit log statement.\n";
        return false;
    }
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, operation.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, size);
    sqlite3_bind_text(stmt, 5, status.c_str(), -1, SQLITE_TRANSIENT);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool Database::saveFileRecord(int owner_id, const std::string& filename, const std::string& path,
                              size_t size, const std::string& checksum) {
    Guard lock(mtx);   // held across UPDATE+INSERT so the pair is atomic w.r.t. other threads
    if (!db) return false;

    // Re-upload of an existing path updates the row instead of adding a duplicate.
    {
        sqlite3_stmt* stmt = nullptr;
        const char* upd = "UPDATE files SET owner_id = ?, filename = ?, size = ?, checksum = ?, "
                          "modified_at = CURRENT_TIMESTAMP WHERE path = ?;";
        if (sqlite3_prepare_v2(db, upd, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "[-] Failed to prepare file record update.\n";
            return false;
        }
        sqlite3_bind_int(stmt, 1, owner_id);
        sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(size));
        sqlite3_bind_text(stmt, 4, checksum.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, path.c_str(), -1, SQLITE_TRANSIENT);
        bool stepped = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (stepped && sqlite3_changes(db) > 0) return true;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* ins = "INSERT INTO files (owner_id, filename, path, size, checksum) VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, ins, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[-] Failed to prepare file record statement.\n";
        return false;
    }
    sqlite3_bind_int(stmt, 1, owner_id);
    sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(size));
    sqlite3_bind_text(stmt, 5, checksum.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool Database::deleteFileRecord(const std::string& path) {
    Guard lock(mtx);
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM files WHERE path = ?;", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool Database::renameFileRecord(const std::string& old_path, const std::string& new_path,
                                const std::string& new_filename) {
    Guard lock(mtx);
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE files SET path = ?, filename = ?, modified_at = CURRENT_TIMESTAMP WHERE path = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, new_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, new_filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, old_path.c_str(), -1, SQLITE_TRANSIENT);
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::string Database::getTransferHistoryLogs() {
    Guard lock(mtx);
    std::string logs;
    if (!db) return "[-] Database not connected.\n";

    // LEFT JOIN so rows are never silently dropped if a user row is missing.
    const char* sql = "SELECT t.timestamp, u.username, t.operation, t.filename, t.status "
                      "FROM transfers t "
                      "LEFT JOIN users u ON t.user_id = u.id "
                      "ORDER BY t.id DESC LIMIT 50;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return "[-] Failed to fetch transfer audit logs.\n";
    }

    logs += "======================================================================\n";
    logs += "TIMESTAMP           | USERNAME   | OPERATION | FILENAME       | STATUS\n";
    logs += "======================================================================\n";
    bool any = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        any = true;
        auto orDash = [](const std::string& s) { return s.empty() ? std::string("-") : s; };
        logs += orDash(colText(stmt, 0)) + " | " + orDash(colText(stmt, 1)) + " | " +
                orDash(colText(stmt, 2)) + " | " + orDash(colText(stmt, 3)) + " | " +
                orDash(colText(stmt, 4)) + "\n";
    }
    sqlite3_finalize(stmt);

    if (!any) return "[No transfer history recorded yet.]\n";
    return logs;
}
