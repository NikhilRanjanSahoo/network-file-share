#include "../include/Database.h"
#include <iostream>

Database::Database() : db(nullptr) {}

Database::~Database() {
    disconnect();
}

bool Database::connect(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[-] Can't open database: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    return true;
}

void Database::disconnect() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Database::executeQuery(const std::string& query) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db, query.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "[-] SQL Error: " << errMsg << "\n";
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::initializeTables() {
    
    std::string createUsers = "CREATE TABLE IF NOT EXISTS users ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                              "username TEXT UNIQUE NOT NULL, "
                              "password TEXT NOT NULL, "
                              "role TEXT NOT NULL, "
                              "home_directory TEXT, "
                              "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

    
    std::string createFiles = "CREATE TABLE IF NOT EXISTS files ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                              "owner_id INTEGER, "
                              "filename TEXT NOT NULL, "
                              "path TEXT NOT NULL, "
                              "size INTEGER, "
                              "checksum TEXT, "
                              "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                              "modified_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                              "FOREIGN KEY(owner_id) REFERENCES users(id));";

    
    std::string createTransfers = "CREATE TABLE IF NOT EXISTS transfers ("
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


bool Database::saveUser(const std::string& username, const std::string& password, const std::string& role, const std::string& home_dir) {
    std::string sql = "INSERT INTO users (username, password, role, home_directory) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, home_dir.c_str(), -1, SQLITE_TRANSIENT);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool Database::authenticateUser(const std::string& username, const std::string& password, std::string& out_role) {
    std::string sql = "SELECT role FROM users WHERE username = ? AND password = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);

    bool success = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        
        out_role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        success = true;
    }
    
    sqlite3_finalize(stmt);
    return success;
}

bool Database::saveTransfer(int user_id, const std::string& filename, const std::string& operation, int size, const std::string& status) {
    std::string sql = "INSERT INTO transfers (user_id, filename, operation, size, status) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
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
