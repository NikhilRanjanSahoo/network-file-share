#include "../include/SetupWizard.h"
#include "../include/PathUtils.h"
#include "../include/config.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace {

bool readLine(std::string& out) {
    return static_cast<bool>(std::getline(std::cin, out));
}

// Reads a line with terminal echo disabled (when stdin is a terminal).
bool readSecret(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
    struct termios oldt;
    const bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0;
    if (tty) {
        struct termios newt = oldt;
        newt.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }
    const bool ok = readLine(out);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cout << "\n";
    }
    return ok;
}

bool promptUsername(std::string& out) {
    for (;;) {
        std::cout << "[*] Enter Username: " << std::flush;
        if (!readLine(out)) return false;
        if (isSafeName(out) && out.size() <= static_cast<size_t>(cfg::MAX_CRED_LEN)) return true;
        std::cout << "[!] Invalid username (1-" << cfg::MAX_CRED_LEN
                  << " chars; no '/', '\\', '|', '..' or control characters).\n";
    }
}

bool promptNewPassword(std::string& out) {
    for (;;) {
        std::string again;
        if (!readSecret("[*] Enter Password: ", out)) return false;
        if (out.size() < static_cast<size_t>(cfg::MIN_PASSWORD_LEN) ||
            out.size() > static_cast<size_t>(cfg::MAX_CRED_LEN)) {
            std::cout << "[!] Password must be " << cfg::MIN_PASSWORD_LEN << "-" << cfg::MAX_CRED_LEN
                      << " characters.\n";
            continue;
        }
        if (!readSecret("[*] Confirm Password: ", again)) return false;
        if (again == out) return true;
        std::cout << "[!] Passwords did not match.\n";
    }
}

void createHomeDir(const std::string& username) {
    std::error_code ec;
    std::filesystem::create_directories(std::string(cfg::USERS_DIR) + "/" + username, ec);
}

}  // namespace

bool runFirstTimeSetup(Database& db) {
    std::cout << "\n====================================================\n";
    std::cout << "     EFSS INITIALIZATION & POLICY SETUP WIZARD      \n";
    std::cout << "====================================================\n";
    std::cout << "[!] Database is empty. Root Administrator required.\n";

    std::string admin_user, admin_pass;
    std::cout << "[*] Root Admin account\n";
    if (!promptUsername(admin_user) || !promptNewPassword(admin_pass)) {
        std::cerr << "[-] Setup aborted: input ended before an admin account was created.\n";
        return false;
    }

    const std::string admin_home = std::string(cfg::USERS_DIR) + "/" + admin_user;
    if (!db.saveUser(admin_user, admin_pass, "Admin", admin_home)) {
        std::cerr << "[-] Failed to create the admin account.\n";
        return false;
    }
    createHomeDir(admin_user);
    std::cout << "[+] Root Admin account provisioned successfully!\n";

    for (;;) {
        std::cout << "\nWould you like to add another user profile (Faculty/Student)? (y/n): " << std::flush;
        std::string choice;
        if (!readLine(choice) || choice.empty() || choice[0] == 'n' || choice[0] == 'N') break;

        std::string u, p, r;
        if (!promptUsername(u) || !promptNewPassword(p)) break;
        std::cout << "[*] Enter Role (Admin / Faculty / Student): " << std::flush;
        if (!readLine(r)) break;

        if (r != "Admin" && r != "Faculty" && r != "Student") {
            r = "Student";   // least-privileged fallback
            std::cout << "[!] Invalid role specified. Defaulting to 'Student'.\n";
        }

        if (db.saveUser(u, p, r, std::string(cfg::USERS_DIR) + "/" + u)) {
            createHomeDir(u);
            std::cout << "[+] Policy updated: User '" << u << "' created with role [" << r << "].\n";
        } else {
            std::cout << "[-] Could not create user '" << u << "' (name already taken?).\n";
        }
    }

    std::cout << "====================================================\n";
    std::cout << "[+] Setup complete. Starting server engine...\n\n";
    return true;
}
