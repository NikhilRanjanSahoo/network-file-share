#ifndef AUTHENTICATIONSERVICE_H
#define AUTHENTICATIONSERVICE_H

#include <string>
#include "Database.h"

class AuthenticationService {
private:
    Database& db;
    std::string currentUser;
    std::string currentRole;

public:
    AuthenticationService(Database& database);
    
    
    bool authenticate(const std::string& username, const std::string& password);
    std::string getUserRole();
    std::string getCurrentUser();
};

#endif 
