#ifndef DATABASE_H
#define DATABASE_H

#include <mutex>
#include <string>
#include <sqlite3.h>

// All public methods are serialized with one recursive mutex, so a single
// Database instance can safely be shared by every client thread.
class Database {
private:
    sqlite3* db;
    std::recursive_mutex mtx;

public:
    Database();
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool connect(const std::string& db_path);
    void disconnect();
    bool executeQuery(const std::string& query);
    bool initializeTables();

    int countUsers();

    // `password` is plaintext here; it is hashed (salted PBKDF2) before storage.
    bool saveUser(const std::string& username, const std::string& password,
                  const std::string& role, const std::string& home_dir = "");

    // On success fills out_role and out_user_id.
    bool authenticateUser(const std::string& username, const std::string& password,
                          std::string& out_role, int& out_user_id);

    std::string getUserRole(const std::string& username);

    bool saveTransfer(int user_id, const std::string& filename, const std::string& operation,
                      int size, const std::string& status);
    bool saveFileRecord(int owner_id, const std::string& filename, const std::string& path,
                        size_t size, const std::string& checksum);
    bool deleteFileRecord(const std::string& path);
    bool renameFileRecord(const std::string& old_path, const std::string& new_path,
                          const std::string& new_filename);

    std::string getTransferHistoryLogs();
};

#endif
