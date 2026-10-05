#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
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
    int h = 7, w = (COLS - 4 < 100) ? COLS - 4 : 100;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;
    WINDOW* win = newwin(h, w, y, x);
    box(win, 0, 0);

    wattron(win, A_BOLD);
    mvwprintw(win, 1, (w - static_cast<int>(title.length())) / 2, "%s", title.c_str());
    wattroff(win, A_BOLD);

    mvwprintw(win, 3, 3, "%s: ", field_name.c_str());
    wrefresh(win);

    int prompt_len = static_cast<int>(field_name.length()) + 5;
    std::string result = getWindowInput(win, 3, prompt_len, (w - prompt_len - 2 < 255) ? w - prompt_len - 2 : 255, is_password);

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

bool parseFlags(const std::string& s, bool& r, bool& w, bool& d) {
    std::string f;
    for (char c : s) if (c == '0' || c == '1') f += c;
    if (f.size() != 3) return false;
    r = f[0] == '1';
    w = f[1] == '1';
    d = f[2] == '1';
    return true;
}

bool isEnter(int c) {
    return c == 10 || c == 13 || c == KEY_ENTER;
}

void renderUserManagementMenu(NetworkClient& client) {
    std::vector<std::string> sub_options = {
        "1. List Users",
        "2. Add User",
        "3. Remove User",
        "4. Change User Role",
        "5. Show Role Permissions",
        "6. Set Role Permissions",
        "0. Return to Main Dashboard"
    };

    int highlight = 0;
    while (true) {
        clear();
        attron(A_BOLD);
        mvprintw(2, (COLS - 36) / 2, "EFSS USER & ACCESS POLICY MANAGEMENT");
        attroff(A_BOLD);

        for (size_t i = 0; i < sub_options.size(); i++) {
            if (static_cast<int>(i) == highlight) attron(A_REVERSE);
            mvprintw(6 + static_cast<int>(i), (COLS - 36) / 2, "%s", sub_options[i].c_str());
            if (static_cast<int>(i) == highlight) attroff(A_REVERSE);
        }

        int c = getch();
        if (c == KEY_UP && highlight > 0) highlight--;
        else if (c == KEY_DOWN && highlight < static_cast<int>(sub_options.size()) - 1) highlight++;
        else if (isEnter(c)) {
            if (highlight == 0) {
                runConsoleCommand([&]() { client.listUsers(); });
            } else if (highlight == 1) {
                std::string u = promptDialog("ADD USER", "Username");
                if (u.empty()) continue;
                std::string r = promptDialog("ADD USER", "Role (Admin/Faculty/Student)");
                if (r.empty()) continue;
                std::string p = promptDialog("ADD USER", "Password (8-31 chars)", true);
                if (p.empty()) continue;
                runConsoleCommand([&]() { client.addUser(u, r, p); });
            } else if (highlight == 2) {
                std::string u = promptDialog("REMOVE USER", "Username");
                if (!u.empty()) runConsoleCommand([&]() { client.removeUser(u); });
            } else if (highlight == 3) {
                std::string u = promptDialog("CHANGE ROLE", "Username");
                if (u.empty()) continue;
                std::string r = promptDialog("CHANGE ROLE", "New Role (Admin/Faculty/Student)");
                if (!r.empty()) runConsoleCommand([&]() { client.setUserRole(u, r); });
            } else if (highlight == 4) {
                runConsoleCommand([&]() { client.listPolicies(); });
            } else if (highlight == 5) {
                std::string role = promptDialog("SET PERMISSIONS", "Role (Admin/Faculty/Student)");
                if (role.empty()) continue;
                std::string scope = promptDialog("SET PERMISSIONS", "Scope (public/home)");
                if (scope.empty()) continue;
                std::string flags = promptDialog("SET PERMISSIONS", "Read,Write,Delete (e.g. 1,1,0)");
                bool r = false, w = false, d = false;
                if (!parseFlags(flags, r, w, d)) {
                    runConsoleCommand([]() { std::cout << "[-] Enter three flags such as 1,1,0.\n"; });
                    continue;
                }
                runConsoleCommand([&]() { client.setPolicy(role, scope, r, w, d); });
            } else if (highlight == 6) {
                return;
            }
        }
    }
}

void executeCommand(NetworkClient& client, int choice, const std::string& role) {
    const bool staff = (role == "Admin" || role == "Faculty");

    if (choice == 1) {
        std::string path = promptDialog("LIST FILES", "Path (blank = public + home)");
        runConsoleCommand([&]() { client.listFiles(path); });
    } else if (choice == 2) {
        std::string p1 = promptDialog("UPLOAD FILE", "Local File Path");
        if (!p1.empty()) {
            std::string p2 = promptDialog("UPLOAD FILE", "Dest (public/ home/ home/dir/)");
            runConsoleCommand([&]() { client.upload(p1, p2); });
        }
    } else if (choice == 3) {
        std::string p1 = promptDialog("DOWNLOAD TO HOME", "Remote Path (public/x | home/x)");
        if (!p1.empty()) {
            std::string p2 = promptDialog("DOWNLOAD FILE", "Save In Home (blank = home/)");
            runConsoleCommand([&]() { client.download(p1, p2); });
        }
    } else if (choice == 4) {
        std::string query = promptDialog("SEARCH REPOSITORY", "Search Query");
        if (!query.empty()) {
            runConsoleCommand([&]() { client.searchFiles(query); });
        }
    } else if (choice == 5) {
        std::string p1 = promptDialog("FILE ATTRIBUTES", "Remote Path");
        if (!p1.empty()) {
            runConsoleCommand([&]() { client.getFileInfo(p1); });
        }
    } else if (choice == 6) {
        std::string dir = promptDialog("CREATE DIRECTORY", "New Directory (e.g. home/docs)");
        if (!dir.empty()) {
            runConsoleCommand([&]() { client.createDirectory(dir); });
        }
    } else if (choice == 7) {
        std::string dir = promptDialog("REMOVE DIRECTORY", "Directory To Remove");
        if (!dir.empty()) {
            runConsoleCommand([&]() { client.removeDirectory(dir); });
        }
    } else if (choice == 8) {
        std::string name = promptDialog("DELETE FILE", "Remote Path");
        if (!name.empty()) {
            runConsoleCommand([&]() { client.deleteRemoteFile(name); });
        }
    } else if (choice == 9) {
        std::string from = promptDialog("RENAME / MOVE FILE", "Current Path");
        if (!from.empty()) {
            std::string to = promptDialog("RENAME / MOVE FILE", "New Path");
            if (!to.empty()) {
                runConsoleCommand([&]() { client.renameRemoteFile(from, to); });
            }
        }
    } else if (choice == 10 && staff) {
        runConsoleCommand([&]() { client.getHistory(); });
    } else if (choice == 11 && role == "Admin") {
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
    mvwprintw(login_win, 1, (w - 17) / 2, "EFSS SECURE LOGIN");
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
        {1, "1. List Files (public + home)"},
        {2, "2. Upload File"},
        {3, "3. Download File (to home)"},
        {4, "4. Search Files"},
        {5, "5. View File Info"},
        {6, "6. Create Directory (mkdir)"},
        {7, "7. Remove Directory (rmdir)"},
        {8, "8. Delete File"},
        {9, "9. Rename / Move File"}
    };

    if (role == "Admin" || role == "Faculty") {
        menu_items.push_back({10, "10. View Transfer Audit Log"});
    }

    if (role == "Admin") {
        menu_items.push_back({11, "11. Manage Users & Permissions"});
    }

    menu_items.push_back({0, "0. Logout"});

    while (true) {
        clear();
        attron(A_BOLD);
        mvprintw(2, (COLS - 37) / 2, "ENTERPRISE FILE SHARING SYSTEM (EFSS)");
        attroff(A_BOLD);
        mvprintw(3, (COLS - 24) / 2, "Active Role: [%s]", role.c_str());

        for (size_t i = 0; i < menu_items.size(); i++) {
            if (static_cast<int>(i) == highlight) attron(A_REVERSE);
            mvprintw(6 + static_cast<int>(i), (COLS - 36) / 2, "%s", menu_items[i].second.c_str());
            if (static_cast<int>(i) == highlight) attroff(A_REVERSE);
        }

        int c = getch();
        if (c == KEY_UP && highlight > 0) highlight--;
        else if (c == KEY_DOWN && highlight < static_cast<int>(menu_items.size()) - 1) highlight++;
        else if (isEnter(c)) {
            int cmd_id = menu_items[highlight].first;
            if (cmd_id == 0) {
                client.logout();
                return;
            }
            executeCommand(client, cmd_id, role);
        }
    }
}

int main(int argc, char** argv) {
    const std::string host = (argc > 1) ? argv[1] : "127.0.0.1";
    const int port = (argc > 2) ? std::atoi(argv[2]) : 8080;
    NetworkClient client(host, port);

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
            mvprintw(LINES / 2, (COLS - 55) / 2, "[-] Authentication Failed. Press any key to retry, or 'q' to quit...");
            int ch = getch();
            if (ch == 'q' || ch == 'Q') break;
        }
    }

    endwin();
    return 0;
}
