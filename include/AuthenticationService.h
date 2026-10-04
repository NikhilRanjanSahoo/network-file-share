#ifndef AUTHENTICATION_SERVICE_H
#define AUTHENTICATION_SERVICE_H

#include <string>
#include "Database.h"

class AuthenticationService {
private:
    Database& db;
    std::string currentUser;
    std::string currentRole;

public:
    explicit AuthenticationService(Database& database);

    
    bool authenticate(const std::string& username, const std::string& password);

    std::string getUserRole() const;
    std::string getCurrentUser() const;
};

#endif 
