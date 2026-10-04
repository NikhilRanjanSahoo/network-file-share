#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <functional>
#include <ncurses.h>
#include "../include/NetworkClient.h"

void runConsoleCommand(std::function<void()> cmd);
std::string getWindowInput(WINDOW* win, int y, int x, int max_len, bool hidden = false) {
    char buffer[256] = {0};
    curs_set(1);
    if (hidden) noecho(); else echo();
    mvwgetnstr(win, y, x, buffer, max_len);
    noecho();
    curs_set(0);
    return std::string(buffer);
}


std::string promptDialog(const std::string& title, const std::string& field_name, bool is_password = false) {
    int h = 7, w = 60;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;
    WINDOW* win = newwin(h, w, y, x);
    box(win, 0, 0);

    wattron(win, A_BOLD);
    mvwprintw(win, 1, (w - title.length()) / 2, "%s", title.c_str());
    wattroff(win, A_BOLD);

    mvwprintw(win, 3, 3, "%s: ", field_name.c_str());
    wrefresh(win);

    int prompt_len = field_name.length() + 5;
    std::string result = getWindowInput(win, 3, prompt_len, 45, is_password);

    delwin(win);
    touchwin(stdscr);
    refresh();
    return result;
}


void runConsoleCommand(std::function<void()> cmd) {
    def_prog_mode();
    endwin();

    std::cout << "\n======================================================\n";
    cmd();
    std::cout << "\n======================================================\n";
    std::cout << "[Press ENTER to return to Dashboard]";
    std::cin.clear();
    std::cin.ignore(1000, '\n');

    reset_prog_mode();
    touchwin(stdscr);
    refresh();
}


void renderUserManagementMenu(NetworkClient& client) {
    std::vector<std::string> sub_options = {
        "1. Provision New User",
        "2. Deactivate / Remove User",
        "0. Return to Main Dashboard"
    };

    int highlight = 0;
    while (true) {
        clear();
        attron(A_BOLD);
        mvprintw(2, (COLS - 36) / 2, "EFSS USER & ACCESS POLICY MANAGEMENT");
        attroff(A_BOLD);

        for (size_t i = 0; i < sub_options.size(); i++) {
            if ((int)i == highlight) attron(A_REVERSE);
            mvprintw(6 + i, (COLS - 36) / 2, "%s", sub_options[i].c_str());
            if ((int)i == highlight) attroff(A_REVERSE);
        }

        int c = getch();
        if (c == KEY_UP && highlight > 0) highlight--;
        else if (c == KEY_DOWN && highlight < (int)sub_options.size() - 1) highlight++;
        else if (c == 10) {
            if (highlight == 0) { 
                std::string new_user = promptDialog("PROVISION NEW ACCOUNT", "New Username");
                if (!new_user.empty()) {
                    std::string new_pass = promptDialog("PROVISION NEW ACCOUNT", "New Password", true);
                    std::string new_role = promptDialog("PROVISION NEW ACCOUNT", "Role (Admin/Faculty/Student)");
                    
                    runConsoleCommand([&]() {
                        std::cout << "[*] Provisioning user '" << new_user << "' with role [" << new_role << "]...\n";
                        
                        std::cout << "[+] Account provision request submitted.\n";
                    });
                }
            } else if (highlight == 1) { 
                std::string target_user = promptDialog("REVOKE ACCOUNT ACCESS", "Target Username");
                if (!target_user.empty()) {
                    runConsoleCommand([&]() {
                        std::cout << "[*] Deleting user '" << target_user << "'...\n";
                        
                        std::cout << "[+] Account revocation request submitted.\n";
                    });
                }
            } else if (highlight == 2) { 
                return;
            }
        }
    }
}

void executeCommand(NetworkClient& client, int choice, const std::string& role) {
    if (choice == 1) {
        runConsoleCommand([&]() { client.listFiles(); });
    } else if (choice == 2) {
        std::string p1 = promptDialog("UPLOAD FILE", "Local File Path");
        if (!p1.empty()) {
            runConsoleCommand([&]() { client.upload(p1); });
        }
    } else if (choice == 3) {
        std::string p1 = promptDialog("DOWNLOAD FILE", "Server Filename");
        if (!p1.empty()) {
            std::string p2 = promptDialog("DOWNLOAD FILE", "Save As (Optional Path)");
            runConsoleCommand([&]() { client.download(p1, p2); });
        }
    } else if (choice == 4) {
        std::string query = promptDialog("SEARCH REPOSITORY", "Search Query");
        if (!query.empty()) {
            runConsoleCommand([&]() { client.searchFiles(query); });
        }
    } else if (choice == 5) {
        std::string p1 = promptDialog("FILE ATTRIBUTES", "Server Filename");
        if (!p1.empty()) {
            runConsoleCommand([&]() { client.getFileInfo(p1); });
        }
    } else if (choice == 6 && (role == "Admin" || role == "Faculty")) {
        std::string dir = promptDialog("DIRECTORY MANAGEMENT", "New Directory Name");
        if (!dir.empty()) {
            runConsoleCommand([&]() { client.createDirectory(dir); });
        }
    } else if (choice == 7 && (role == "Admin" || role == "Faculty")) {
        std::string dir = promptDialog("DIRECTORY MANAGEMENT", "Directory Name To Remove");
        if (!dir.empty()) {
            runConsoleCommand([&]() { client.removeDirectory(dir); });
        }
    } else if (choice == 8 && (role == "Admin" || role == "Faculty")) {
        runConsoleCommand([&]() { client.getHistory(); });
    } else if (choice == 9 && role == "Admin") {
        renderUserManagementMenu(client);
    }
}

bool renderLogin(NetworkClient& client) {
    clear();
    int h = 10, w = 50;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;
    WINDOW* login_win = newwin(h, w, y, x);
    box(login_win, 0, 0);

    wattron(login_win, A_BOLD);
    mvwprintw(login_win, 1, (w - 20) / 2, "EFSS SECURE LOGIN");
    wattroff(login_win, A_BOLD);

    mvwprintw(login_win, 3, 5, "Username: ");
    mvwprintw(login_win, 5, 5, "Password: ");
    wrefresh(login_win);

    std::string user = getWindowInput(login_win, 3, 15, 30, false);
    std::string pass = getWindowInput(login_win, 5, 15, 30, true);

    delwin(login_win);

    return client.authenticate(user, pass);
}

void renderDashboard(NetworkClient& client) {
    std::string role = client.getRole();
    int highlight = 0;

    std::vector<std::pair<int, std::string>> menu_items = {
        {1, "1. List Files (Current Dir)"},
        {2, "2. Upload File"},
        {3, "3. Download File"},
        {4, "4. Search Files"},
        {5, "5. View File Info"}
    };

    if (role == "Admin" || role == "Faculty") {
        menu_items.push_back({6, "6. Create Directory (mkdir)"});
        menu_items.push_back({7, "7. Remove Directory (rmdir)"});
        menu_items.push_back({8, "8. View Transfer Audit Log"});
    }

    if (role == "Admin") {
        menu_items.push_back({9, "9. Manage User Accounts"});
    }

    menu_items.push_back({0, "0. Logout"});

    while (true) {
        clear();
        attron(A_BOLD);
        mvprintw(2, (COLS - 40) / 2, "ENTERPRISE FILE SHARING SYSTEM (EFSS)");
        attroff(A_BOLD);
        mvprintw(3, (COLS - 24) / 2, "Active Role: [%s]", role.c_str());

        for (size_t i = 0; i < menu_items.size(); i++) {
            if ((int)i == highlight) attron(A_REVERSE);
            mvprintw(6 + i, (COLS - 36) / 2, "%s", menu_items[i].second.c_str());
            if ((int)i == highlight) attroff(A_REVERSE);
        }

        int c = getch();
        if (c == KEY_UP && highlight > 0) highlight--;
        else if (c == KEY_DOWN && highlight < (int)menu_items.size() - 1) highlight++;
        else if (c == 10) { 
            int cmd_id = menu_items[highlight].first;
            if (cmd_id == 0) return; 
            executeCommand(client, cmd_id, role);
        }
    }
}

int main() {
    NetworkClient client("127.0.0.1", 8080);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    while (true) {
        if (renderLogin(client)) {
            renderDashboard(client);
        } else {
            clear();
            mvprintw(LINES / 2, (COLS - 48) / 2, "[-] Authentication Failed. Press any key to retry...");
            getch();
        }
    }

    endwin();
    return 0;
}
