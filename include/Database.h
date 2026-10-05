#ifndef DATABASE_H
#define DATABASE_H

#include <mutex>
#include <string>
#include <sqlite3.h>

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
    bool initializePolicyTables();

    int countUsers();

    bool saveUser(const std::string& username, const std::string& password,
                  const std::string& role, const std::string& home_dir = "");

    bool authenticateUser(const std::string& username, const std::string& password,
                          std::string& out_role, int& out_user_id);

    std::string getUserRole(const std::string& username);
    bool deleteUser(const std::string& username);
    bool setUserRole(const std::string& username, const std::string& role);
    std::string listUsers();

    bool getPolicy(const std::string& role, const std::string& scope, bool& can_read, bool& can_write, bool& can_delete);
    bool setPolicy(const std::string& role, const std::string& scope, bool can_read, bool can_write, bool can_delete);
    std::string listPolicies();

    bool saveTransfer(int user_id, const std::string& filename, const std::string& operation,
                      int size, const std::string& status);
    bool saveFileRecord(int owner_id, const std::string& filename, const std::string& path,
                        size_t size, const std::string& checksum);
    bool deleteFileRecord(const std::string& path);
    bool renameFileRecord(const std::string& old_path, const std::string& new_path,
                          const std::string& new_filename);
    int getFileOwner(const std::string& path);

    std::string getTransferHistoryLogs();
};

#endif
