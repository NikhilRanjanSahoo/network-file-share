#include "../include/NetworkServer.h"
#include "../include/AuthenticationService.h"
#include "../include/PathUtils.h"
#include "../include/PermissionService.h"
#include "../include/SetupWizard.h"
#include "../include/TransferService.h"
#include "../include/Crypto.h"
#include "../include/config.h"
#include "../include/net_io.h"
#include "../include/protocol.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

struct FdGuard {
    int fd;
    ~FdGuard() { if (fd >= 0) ::close(fd); }
};

std::string printable(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += (c < 0x20 || c == 0x7F) ? '?' : static_cast<char>(c);
    return out;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t pos = s.find(delim, start);
        out.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return out;
}

struct Target {
    bool ok = false;
    bool isRoot = false;
    std::string scope;
    std::string root;
    std::string real;
};

Target resolve(const std::string& vpath, const std::string& username) {
    Target t;
    std::string p = vpath;
    if (!p.empty() && p.back() == '/') p.pop_back();
    const std::vector<std::string> parts = split(p, '/');

    t.scope = parts[0];
    if (t.scope == "public") {
        t.root = cfg::STORAGE_ROOT;
    } else if (t.scope == "home") {
        if (!isSafeName(username)) return t;
        t.root = std::string(cfg::USERS_DIR) + "/" + username;
        std::error_code ec;
        fs::create_directories(t.root, ec);
    } else {
        return t;
    }

    t.real = t.root;
    for (size_t i = 1; i < parts.size(); ++i) {
        if (!isSafeName(parts[i])) return t;
        t.real += "/" + parts[i];
    }
    t.isRoot = (parts.size() == 1);

    std::error_code e1, e2;
    const fs::path croot = fs::weakly_canonical(t.root, e1);
    const fs::path creal = fs::weakly_canonical(t.real, e2);
    if (e1 || e2) return t;
    const fs::path rel = creal.lexically_relative(croot);
    if (rel.empty() || rel.begin()->string() == "..") return t;

    t.ok = true;
    return t;
}

struct Perm {
    bool r = false;
    bool w = false;
    bool d = false;
};

Perm permFor(Database& db, const std::string& role, const std::string& scope) {
    Perm p;
    db.getPolicy(role, scope, p.r, p.w, p.d);
    return p;
}

std::string searchScope(const std::string& root, const std::string& prefix, const std::string& query) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string q = lower(query);
    const int maxResults = 200;

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) return "";

    std::string out;
    int count = 0;
    for (; it != end && count < maxResults; it.increment(ec)) {
        if (ec) break;
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        if (lower(it->path().filename().string()).find(q) == std::string::npos) continue;
        out += prefix + fs::relative(it->path(), root, e2).string() + "\n";
        ++count;
    }
    if (count >= maxResults) out += "[!] Results truncated.\n";
    return out;
}

std::string homeDest(const std::string& dest, const std::string& src) {
    std::string d = dest;
    if (d.empty()) d = "home/";
    else if (d != "home" && d.rfind("home/", 0) != 0) d = "home/" + d;
    if (d == "home") d += "/";
    if (d.back() == '/') d += baseName(src);
    return d;
}

bool isFlag(const std::string& s) { return s == "0" || s == "1"; }

}

NetworkServer::NetworkServer(int port) : server_fd(-1), port(port), address{} {
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = INADDR_ANY;
}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start() {
    std::error_code ec;
    fs::create_directories(cfg::DB_DIR, ec);
    fs::create_directories(cfg::STORAGE_ROOT, ec);
    fs::create_directories(cfg::TEMP_DIR, ec);
    fs::create_directories(cfg::USERS_DIR, ec);

    if (!db.connect(cfg::DB_PATH)) {
        std::cerr << "[-] Database connection failed.\n";
        return false;
    }
    if (!db.initializeTables() || !db.initializePolicyTables()) {
        std::cerr << "[-] Failed to initialize database tables.\n";
        return false;
    }
    if (db.countUsers() == 0 && !runFirstTimeSetup(db)) {
        return false;
    }
    std::cout << "[+] SQLite database initialized successfully.\n";

    std::signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "[-] Failed to create socket.\n";
        return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "[-] Bind failed on port " << port << ".\n";
        return false;
    }
    if (listen(server_fd, 64) < 0) {
        std::cerr << "[-] Listen failed.\n";
        return false;
    }

    std::cout << "[*] EFSS server listening on port " << port << "...\n";
    return true;
}

void NetworkServer::listenForClients() {
    while (!g_stop) {
        struct pollfd pfd;
        pfd.fd = server_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 1000);
        if (pr <= 0) continue;

        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        std::memset(&peer, 0, sizeof(peer));
        int client_fd = accept(server_fd, reinterpret_cast<struct sockaddr*>(&peer), &peer_len);
        if (client_fd < 0) {
            if (errno != EINTR) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (active_clients.load() >= cfg::MAX_CLIENTS) {
            sendError(client_fd, "Server busy. Try again shortly.");
            ::close(client_fd);
            continue;
        }

        setSocketTimeouts(client_fd, cfg::SOCKET_TIMEOUT_SEC);

        char ip[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        const std::string peer_ip(ip);

        active_clients++;
        std::thread([this, client_fd, peer_ip]() {
            handleClient(client_fd, peer_ip);
            active_clients--;
        }).detach();
    }

    std::cout << "\n[*] Shutting down, waiting for active clients...\n";
    for (int i = 0; i < 100 && active_clients.load() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void NetworkServer::handleClient(int client_fd, const std::string& peer_ip) {
    FdGuard guard{client_fd};

    PacketHeader header;
    if (!recvHeader(client_fd, header)) return;

    std::string filename(header.filename_len, '\0');
    if (header.filename_len > 0 && !readExact(client_fd, &filename[0], header.filename_len)) return;

    if (header.opcode == Opcode::AUTH) {
        if (header.payload_size != sizeof(AuthPayload)) {
            sendError(client_fd, "Malformed authentication request.");
            return;
        }
        AuthPayload payload;
        std::memset(&payload, 0, sizeof(payload));
        if (!readExact(client_fd, &payload, sizeof(payload))) return;
        payload.username[sizeof(payload.username) - 1] = '\0';
        payload.password[sizeof(payload.password) - 1] = '\0';

        if (sessions.isLockedOut(peer_ip)) {
            std::cerr << "[-] Login attempt from locked-out address " << peer_ip << ".\n";
            sendError(client_fd, "Too many failed attempts. Try again later.");
            return;
        }

        AuthenticationService auth(db);
        if (auth.authenticate(payload.username, payload.password)) {
            sessions.recordSuccess(peer_ip);
            try {
                std::string token = sessions.create(auth.getUserId(), auth.getCurrentUser(), auth.getUserRole());
                sendAckData(client_fd, "SUCCESS:" + auth.getUserRole() + ":" + token);
            } catch (const std::exception& e) {
                std::cerr << "[-] Could not create session: " << e.what() << "\n";
                sendError(client_fd, "Server error creating session.");
            }
        } else {
            sessions.recordFailure(peer_ip);
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg::FAILED_LOGIN_DELAY_MS));
            sendError(client_fd, "Invalid credentials.");
        }
        return;
    }

    const std::string token(header.session_token, strnlen(header.session_token, SESSION_TOKEN_LEN));
    SessionManager::Session sess;
    if (!sessions.validate(token, sess)) {
        sendError(client_fd, "Not authenticated or session expired. Please log in again.");
        return;
    }
    sess.role = db.getUserRole(sess.username);
    if (!PermissionService::isKnownRole(sess.role)) {
        sessions.destroy(token);
        sendError(client_fd, "Account no longer exists.");
        return;
    }

    auto deny = [&](const char* op, const std::string& name) {
        std::cerr << "[-] Security Block: '" << sess.username << "' (" << sess.role
                  << ") attempted unauthorized " << op << ".\n";
        db.saveTransfer(sess.user_id, printable(name), op, 0, "DENIED_RBAC");
        sendError(client_fd, "Permission denied for your role.");
    };
    auto audit = [&](const std::string& name, const char* op, int size, const char* status) {
        db.saveTransfer(sess.user_id, name, op, size, status);
    };
    auto locate = [&](const std::string& vpath, Target& t) -> bool {
        t = resolve(vpath, sess.username);
        if (!t.ok) {
            sendError(client_fd, "Invalid path. Use public/<path> or home/<path>.");
            return false;
        }
        return true;
    };
    auto allowed = [&](const Target& t, char kind, const char* op, const std::string& name) -> bool {
        const Perm p = permFor(db, sess.role, t.scope);
        const bool ok = (kind == 'r') ? p.r : (kind == 'w') ? p.w : p.d;
        if (!ok) deny(op, name);
        return ok;
    };
    auto adminOnly = [&](const char* op) -> bool {
        if (sess.role == "Admin") return true;
        deny(op, "");
        return false;
    };

    TransferService transferService;
    FileManager fm;
    Target t;

    switch (header.opcode) {

        case Opcode::LOGOUT: {
            sessions.destroy(token);
            sendOk(client_fd);
            break;
        }

        case Opcode::LIST: {
            if (filename.empty()) {
                std::string out;
                for (const char* sc : {"public", "home"}) {
                    const Target root = resolve(sc, sess.username);
                    out += std::string("[") + sc + "/]\n";
                    if (!root.ok) out += "[-] Unavailable.\n";
                    else if (!permFor(db, sess.role, sc).r) out += "[-] Read access denied for your role.\n";
                    else out += fm.listDirectory(root.real);
                }
                sendAckData(client_fd, out);
                break;
            }
            if (!locate(filename, t)) break;
            if (!allowed(t, 'r', "LIST", filename)) break;
            sendAckData(client_fd, fm.listDirectory(t.real));
            break;
        }

        case Opcode::FILE_INFO: {
            if (!locate(filename, t)) break;
            if (!allowed(t, 'r', "FILE_INFO", filename)) break;
            sendAckData(client_fd, fm.getFileInfo(t.real));
            break;
        }

        case Opcode::SEARCH: {
            if (filename.empty()) { sendError(client_fd, "Empty search query."); break; }
            std::string out;
            for (const char* sc : {"public", "home"}) {
                const Target root = resolve(sc, sess.username);
                if (!root.ok || !permFor(db, sess.role, sc).r) continue;
                out += searchScope(root.real, std::string(sc) + "/", filename);
            }
            if (out.empty()) out = "[-] No files found matching '" + printable(filename) + "'.\n";
            sendAckData(client_fd, out);
            break;
        }

        case Opcode::UPLOAD: {
            if (!locate(filename, t)) break;
            if (!allowed(t, 'w', "UPLOAD", filename)) break;
            if (t.isRoot) { sendError(client_fd, "Give a file name after the folder."); break; }
            if (header.payload_size > MAX_UPLOAD_SIZE) {
                audit(filename, "UPLOAD", 0, "FAILED_TOO_LARGE");
                sendError(client_fd, "File exceeds the maximum upload size.");
                break;
            }

            std::error_code ec;
            if (!fs::is_directory(fs::path(t.real).parent_path(), ec)) {
                audit(filename, "UPLOAD", 0, "FAILED");
                sendError(client_fd, "Destination folder does not exist.");
                break;
            }
            if (fs::is_directory(t.real, ec)) {
                audit(filename, "UPLOAD", 0, "FAILED");
                sendError(client_fd, "Destination is a folder; end the path with '/' to keep the file name.");
                break;
            }
            if (fs::exists(t.real, ec) && !permFor(db, sess.role, t.scope).d &&
                db.getFileOwner(t.real) != sess.user_id) {
                audit(filename, "UPLOAD", 0, "FAILED_NO_OVERWRITE");
                sendError(client_fd, "A file with that name exists and you are not its owner.");
                break;
            }

            const bool ok = transferService.receiveFile(client_fd, t.real, header.payload_size);
            if (ok) {
                const std::string hash = fm.calculateSHA256(t.real);
                std::cout << "[+] Upload complete. SHA-256: " << hash << "\n";
                db.saveFileRecord(sess.user_id, baseName(t.real), t.real, header.payload_size, hash);
                audit(filename, "UPLOAD", static_cast<int>(header.payload_size), "SUCCESS");
                sendOk(client_fd);
            } else {
                audit(filename, "UPLOAD", 0, "FAILED");
                sendError(client_fd, "Upload failed.");
            }
            break;
        }

        case Opcode::DOWNLOAD: {
            if (!locate(filename, t)) break;
            if (!allowed(t, 'r', "DOWNLOAD", filename)) break;

            uint32_t sent = 0;
            const bool ok = transferService.sendFile(client_fd, t.real, &sent);
            audit(filename, "DOWNLOAD", static_cast<int>(sent), ok ? "SUCCESS" : "FAILED");
            break;
        }

        case Opcode::COPY_FILE: {
            const size_t delim = filename.find('|');
            const std::string srcPath = filename.substr(0, delim);
            const std::string destArg = (delim == std::string::npos) ? "" : filename.substr(delim + 1);

            Target src, dst;
            if (!locate(srcPath, src)) break;
            if (!allowed(src, 'r', "DOWNLOAD", filename)) break;
            if (src.isRoot) { sendError(client_fd, "Give a file name after the folder."); break; }

            std::string dvirt = homeDest(destArg, srcPath);
            if (!locate(dvirt, dst)) break;
            if (!allowed(dst, 'w', "DOWNLOAD", filename)) break;

            std::error_code ec;
            if (!fs::is_regular_file(src.real, ec)) { sendError(client_fd, "Source is not a regular file."); break; }
            if (fs::is_directory(dst.real, ec)) {
                dvirt += "/" + baseName(srcPath);
                if (!locate(dvirt, dst)) break;
            }
            if (dst.isRoot) { sendError(client_fd, "Give a file name after the folder."); break; }
            if (src.real == dst.real) { sendError(client_fd, "Source and destination are the same file."); break; }
            if (!fs::is_directory(fs::path(dst.real).parent_path(), ec)) {
                sendError(client_fd, "Destination folder does not exist in your home.");
                break;
            }
            if (fs::exists(dst.real, ec) && !permFor(db, sess.role, dst.scope).d &&
                db.getFileOwner(dst.real) != sess.user_id) {
                audit(filename, "DOWNLOAD", 0, "FAILED_NO_OVERWRITE");
                sendError(client_fd, "A file with that name exists and you are not its owner.");
                break;
            }

            std::string tmp;
            try {
                tmp = std::string(cfg::TEMP_DIR) + "/copy_" + randomHex(8);
            } catch (const std::exception&) {
                sendError(client_fd, "Server error creating temp file.");
                break;
            }
            fs::copy_file(src.real, tmp, fs::copy_options::none, ec);
            const bool ok = !ec && fm.renameFile(tmp, dst.real);
            if (!ok) {
                unlink(tmp.c_str());
                audit(filename, "DOWNLOAD", 0, "FAILED");
                sendError(client_fd, "Copy to your home failed.");
                break;
            }

            std::error_code ec2;
            const uintmax_t size = fs::file_size(dst.real, ec2);
            db.saveFileRecord(sess.user_id, baseName(dst.real), dst.real, static_cast<size_t>(size),
                              fm.calculateSHA256(dst.real));
            audit(filename, "DOWNLOAD", static_cast<int>(size), "SUCCESS");
            sendAckData(client_fd, "home/" + fs::relative(dst.real, dst.root, ec2).string());
            break;
        }

        case Opcode::DELETE_FILE: {
            if (!locate(filename, t)) break;
            if (!allowed(t, 'd', "DELETE", filename)) break;
            if (t.isRoot) { sendError(client_fd, "Cannot delete a root folder."); break; }

            const bool ok = fm.deleteFile(t.real);
            if (ok) db.deleteFileRecord(t.real);
            audit(filename, "DELETE", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Delete failed (file missing or not a regular file).");
            break;
        }

        case Opcode::RENAME_FILE: {
            const size_t delim = filename.find('|');
            if (delim == std::string::npos) { sendError(client_fd, "Malformed rename request."); break; }
            Target src, dst;
            if (!locate(filename.substr(0, delim), src) || !locate(filename.substr(delim + 1), dst)) break;
            if (src.isRoot || dst.isRoot) { sendError(client_fd, "Cannot rename a root folder."); break; }
            if (!allowed(src, 'w', "RENAME", filename)) break;
            if (src.scope != dst.scope &&
                (!allowed(src, 'd', "RENAME", filename) || !allowed(dst, 'w', "RENAME", filename))) break;

            std::error_code ec;
            if (fs::exists(dst.real, ec)) {
                audit(filename, "RENAME", 0, "FAILED");
                sendError(client_fd, "A file with the new name already exists.");
                break;
            }

            const bool ok = fm.renameFile(src.real, dst.real);
            if (ok) db.renameFileRecord(src.real, dst.real, baseName(dst.real));
            audit(filename, "RENAME", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Rename failed (source missing or destination folder missing).");
            break;
        }

        case Opcode::CREATE_DIR: {
            if (!locate(filename, t)) break;
            if (!allowed(t, 'w', "MKDIR", filename)) break;
            if (t.isRoot) { sendError(client_fd, "Give a folder name after the root."); break; }

            const bool ok = fm.createDir(t.real);
            audit(filename, "MKDIR", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Could not create directory (parent missing or it already exists).");
            break;
        }

        case Opcode::REMOVE_DIR: {
            if (!locate(filename, t)) break;
            if (!allowed(t, 'd', "RMDIR", filename)) break;
            if (t.isRoot) { sendError(client_fd, "Cannot remove a root folder."); break; }

            const bool ok = fm.removeDir(t.real);
            audit(filename, "RMDIR", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Could not remove directory (it must exist and be empty).");
            break;
        }

        case Opcode::HISTORY: {
            if (!PermissionService::canViewHistory(sess.role)) { deny("HISTORY", ""); break; }
            std::cout << "[*] User '" << sess.username << "' (" << sess.role << ") requested transfer history.\n";
            sendAckData(client_fd, db.getTransferHistoryLogs());
            break;
        }

        case Opcode::LIST_USERS: {
            if (!adminOnly("LIST_USERS")) break;
            sendAckData(client_fd, db.listUsers());
            break;
        }

        case Opcode::ADD_USER: {
            if (!adminOnly("ADD_USER")) break;
            const size_t a = filename.find('|');
            const size_t b = (a == std::string::npos) ? a : filename.find('|', a + 1);
            if (b == std::string::npos) { sendError(client_fd, "Malformed request."); break; }
            const std::string user = filename.substr(0, a);
            const std::string role = filename.substr(a + 1, b - a - 1);
            const std::string pass = filename.substr(b + 1);

            if (!isSafeName(user) || user.size() > static_cast<size_t>(cfg::MAX_CRED_LEN)) {
                sendError(client_fd, "Invalid username.");
                break;
            }
            if (!PermissionService::isKnownRole(role)) {
                sendError(client_fd, "Role must be Admin, Faculty or Student.");
                break;
            }
            if (pass.size() < static_cast<size_t>(cfg::MIN_PASSWORD_LEN) ||
                pass.size() > static_cast<size_t>(cfg::MAX_CRED_LEN)) {
                sendError(client_fd, "Password length is out of range.");
                break;
            }

            const std::string home = std::string(cfg::USERS_DIR) + "/" + user;
            const bool ok = db.saveUser(user, pass, role, home);
            if (ok) {
                std::error_code ec;
                fs::create_directories(home, ec);
            }
            audit(user, "ADD_USER", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Could not create user (name already taken?).");
            break;
        }

        case Opcode::REMOVE_USER: {
            if (!adminOnly("REMOVE_USER")) break;
            if (filename == sess.username) { sendError(client_fd, "You cannot remove your own account."); break; }
            const bool ok = db.deleteUser(filename);
            audit(filename, "REMOVE_USER", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "User not found.");
            break;
        }

        case Opcode::SET_ROLE: {
            if (!adminOnly("SET_ROLE")) break;
            const std::vector<std::string> parts = split(filename, '|');
            if (parts.size() != 2 || !PermissionService::isKnownRole(parts[1])) {
                sendError(client_fd, "Usage: user and role (Admin, Faculty or Student).");
                break;
            }
            if (parts[0] == sess.username) { sendError(client_fd, "You cannot change your own role."); break; }
            const bool ok = db.setUserRole(parts[0], parts[1]);
            audit(filename, "SET_ROLE", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "User not found.");
            break;
        }

        case Opcode::LIST_POLICY: {
            if (!adminOnly("LIST_POLICY")) break;
            sendAckData(client_fd, db.listPolicies());
            break;
        }

        case Opcode::SET_POLICY: {
            if (!adminOnly("SET_POLICY")) break;
            const std::vector<std::string> p = split(filename, '|');
            if (p.size() != 5 || !isFlag(p[2]) || !isFlag(p[3]) || !isFlag(p[4])) {
                sendError(client_fd, "Usage: role, scope (public/home) and read/write/delete flags 0 or 1.");
                break;
            }
            const bool ok = db.setPolicy(p[0], p[1], p[2] == "1", p[3] == "1", p[4] == "1");
            audit(filename, "SET_POLICY", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Invalid role or scope.");
            break;
        }

        default: {
            std::cerr << "[-] Unknown opcode received: " << static_cast<int>(header.opcode) << "\n";
            sendError(client_fd, "Unknown request.");
            break;
        }
    }
}

void NetworkServer::stop() {
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
        std::cout << "[*] Server shut down.\n";
    }
}
