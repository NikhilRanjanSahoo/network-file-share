#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <sqlite3.h>

class Database {
private:
    sqlite3* db;
    bool executeQuery(const std::string& query);

public:
    Database();
    ~Database();
    
    bool connect(const std::string& db_path);
    void disconnect();
    bool initializeTables();
    
    
    bool saveUser(const std::string& username, const std::string& password, const std::string& role, const std::string& home_dir);
    bool authenticateUser(const std::string& username, const std::string& password, std::string& out_role);
    bool saveTransfer(int user_id, const std::string& filename, const std::string& operation, int size, const std::string& status);
};

#endif 
