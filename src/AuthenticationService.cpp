#include "../include/AuthenticationService.h"
#include <iostream>

namespace {
// Keep client-controlled text from forging extra log lines.
std::string printable(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += (c < 0x20 || c == 0x7F) ? '?' : static_cast<char>(c);
    return out;
}
}

AuthenticationService::AuthenticationService(Database& database)
    : db(database), currentUser(""), currentRole("Guest"), currentUserId(0) {}

bool AuthenticationService::authenticate(const std::string& username, const std::string& password) {
    std::string role;
    int id = 0;
    if (db.authenticateUser(username, password, role, id)) {
        currentUser = username;
        currentRole = role;
        currentUserId = id;
        std::cout << "[+] Authentication successful for user '" << printable(username)
                  << "' [Role: " << role << "]\n";
        return true;
    }

    std::cout << "[-] Authentication failed for user '" << printable(username) << "'\n";
    currentUser = "";
    currentRole = "Guest";
    currentUserId = 0;
    return false;
}

std::string AuthenticationService::getUserRole() const {
    return currentRole;
}

std::string AuthenticationService::getCurrentUser() const {
    return currentUser;
}

int AuthenticationService::getUserId() const {
    return currentUserId;
}
