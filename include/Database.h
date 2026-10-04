#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <sqlite3.h>

class Database {
private:
    sqlite3* db;
    bool checkAndBootstrapAdmin(); 

public:
    Database();
    ~Database();
    
    std::string getUserRole(const std::string& username);
    std::string getTransferHistoryLogs();

    bool connect(const std::string& db_path);
    void disconnect();
    bool executeQuery(const std::string& query);
    bool initializeTables();
    
    bool saveUser(const std::string& username, const std::string& password, const std::string& role, const std::string& home_dir = "");
    bool authenticateUser(const std::string& username, const std::string& password, std::string& out_role);
    bool saveTransfer(int user_id, const std::string& filename, const std::string& operation, int size, const std::string& status);
    bool saveFileRecord(int owner_id, const std::string& filename, const std::string& path, size_t size, const std::string& checksum);
    std::string getHistory();
};

#endif
