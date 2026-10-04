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

    bool success = executeQuery(createUsers) && executeQuery(createFiles) && executeQuery(createTransfers);
    
    if (success) {
        checkAndBootstrapAdmin();
    }
    return success;
}

bool Database::checkAndBootstrapAdmin() {
    std::string count_sql = "SELECT COUNT(*) FROM users;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, count_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            
            if (count == 0) {
                std::cout << "\n====================================================\n";
                std::cout << "     EFSS INITIALIZATION & POLICY SETUP WIZARD      \n";
                std::cout << "====================================================\n";
                std::cout << "[!] Database is empty. Root Administrator required.\n";
                
                std::string admin_user, admin_pass;
                std::cout << "[*] Enter Root Admin Username: ";
                std::cin >> admin_user;
                std::cout << "[*] Enter Root Admin Password: ";
                std::cin >> admin_pass;
                
                saveUser(admin_user, admin_pass, "Admin", "./server_storage/admin");
                std::cout << "[+] Root Admin account provisioned successfully!\n";
                char choice = 'y';
                while (true) {
                    std::cout << "\nWould you like to add another user profile (Faculty/Student)? (y/n): ";
                    std::cin >> choice;
                    if (choice == 'n' || choice == 'N') break;
                    
                    std::string u, p, r;
                    std::cout << "[*] Enter Username: ";
                    std::cin >> u;
                    std::cout << "[*] Enter Password: ";
                    std::cin >> p;
                    std::cout << "[*] Enter Role (Admin / Faculty / Student): ";
                    std::cin >> r;
                    
                    if (r != "Admin" && r != "Faculty" && r != "Student") {
                        r = "Student"; // Default fallback policy
                        std::cout << "[!] Invalid role specified. Defaulting to 'Student'.\n";
                    }
                    
                    saveUser(u, p, r, "./server_storage/" + u);
                    std::cout << "[+] Policy updated: User '" << u << "' created with role [" << r << "].\n";
                }
                std::cout << "====================================================\n";
                std::cout << "[+] Setup complete. Starting server engine...\n\n";
                return true;
            }
        }
    }
    return true;
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

bool Database::saveFileRecord(int owner_id, const std::string& filename, const std::string& path, size_t size, const std::string& checksum) {
    std::string sql = "INSERT INTO files (owner_id, filename, path, size, checksum) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[-] Failed to prepare file record statement.\n";
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, owner_id);
    sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, size);
    sqlite3_bind_text(stmt, 5, checksum.c_str(), -1, SQLITE_TRANSIENT);
    
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    
    return success;
}

std::string Database::getHistory() {
    std::string sql = "SELECT id, user_id, filename, operation, status, timestamp FROM transfers ORDER BY id DESC LIMIT 15;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return "[-] Failed to fetch history.\n";
    }

    std::string result = "Transfer Audit Log:\n------------------------------------------------------------\n";
    result += "ID | User | File | Op | Status | Timestamp\n";
    result += "------------------------------------------------------------\n";

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result += std::to_string(sqlite3_column_int(stmt, 0)) + " | ";
        result += std::to_string(sqlite3_column_int(stmt, 1)) + " | ";
        result += reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)); result += " | ";
        result += reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)); result += " | ";
        result += reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)); result += " | ";
        result += reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)); result += "\n";
    }
    
    sqlite3_finalize(stmt);
    return result.empty() ? "[-] No transfers found.\n" : result;
}

std::string Database::getUserRole(const std::string& username) {
    std::string role = "Guest";
    std::string sql = "SELECT role FROM users WHERE username = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) {
                role = reinterpret_cast<const char*>(val);
            }
        }
        sqlite3_finalize(stmt);
    }
    return role;
}

std::string Database::getTransferHistoryLogs() {
    std::string logs;
    
    std::string sql = "SELECT t.timestamp, u.username, t.operation, t.filename, t.status "
                      "FROM transfers t "
                      "JOIN users u ON t.user_id = u.id "
                      "ORDER BY t.id DESC LIMIT 50;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        logs += "======================================================================\n";
        logs += "TIMESTAMP           | USERNAME   | OPERATION | FILENAME       | STATUS\n";
        logs += "======================================================================\n";
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* time_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* user_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* op_val   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            const char* file_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            const char* stat_val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

            logs += std::string(time_val ? time_val : "-") + " | " +
                    std::string(user_val ? user_val : "-") + " | " +
                    std::string(op_val   ? op_val   : "-") + " | " +
                    std::string(file_val ? file_val : "-") + " | " +
                    std::string(stat_val ? stat_val : "-") + "\n";
        }
        sqlite3_finalize(stmt);
    } else {
        logs = "[-] Failed to fetch transfer audit logs.\n";
    }

    if (logs.empty() || logs.find('|') == std::string::npos) {
        logs = "[No transfer history recorded yet.]\n";
    }

    return logs;
}
