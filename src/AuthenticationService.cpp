#include "../include/AuthenticationService.h"
#include <iostream>

AuthenticationService::AuthenticationService(Database& database)
    : db(database), currentUser(""), currentRole("Guest") {}

bool AuthenticationService::authenticate(const std::string& username, const std::string& password) {
    std::string role;
    if (db.authenticateUser(username, password, role)) {
        currentUser = username;
        currentRole = role;
        std::cout << "[+] Authentication successful for user '" << username 
                  << "' [Role: " << role << "]\n";
        return true;
    }

    std::cout << "[-] Authentication failed for user '" << username << "'\n";
    currentUser = "";
    currentRole = "Guest";
    return false;
}

std::string AuthenticationService::getUserRole() const {
    return currentRole;
}

std::string AuthenticationService::getCurrentUser() const {
    return currentUser;
}
