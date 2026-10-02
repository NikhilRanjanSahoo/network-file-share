#include "../include/AuthenticationService.h"
#include <iostream>

AuthenticationService::AuthenticationService(Database& database) : db(database) {}

bool AuthenticationService::authenticate(const std::string& username, const std::string& password) {
    if (db.authenticateUser(username, password, currentRole)) {
        currentUser = username;
        std::cout << "[+] User '" << username << "' authenticated successfully. Role: " << currentRole << "\n";
        return true;
    }
    std::cerr << "[-] Authentication failed for user: " << username << "\n";
    return false;
}

std::string AuthenticationService::getUserRole() {
    return currentRole;
}

std::string AuthenticationService::getCurrentUser() {
    return currentUser;
}
