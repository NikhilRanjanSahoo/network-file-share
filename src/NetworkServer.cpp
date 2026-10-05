#include "../include/NetworkServer.h"
#include "../include/AuthenticationService.h"
#include "../include/PathUtils.h"
#include "../include/PermissionService.h"
#include "../include/SetupWizard.h"
#include "../include/TransferService.h"
#include "../include/config.h"
#include "../include/net_io.h"
#include "../include/protocol.h"

#include <arpa/inet.h>
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

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

// Closes the client socket on every exit path (fixes the old fd leak on bad headers).
struct FdGuard {
    int fd;
    ~FdGuard() { if (fd >= 0) ::close(fd); }
};

std::string storagePath(const std::string& name) {
    return std::string(cfg::STORAGE_ROOT) + "/" + name;   // `name` must already pass isSafeName()
}

std::string printable(const std::string& s) {
    std::string out;
    for (unsigned char c : s) out += (c < 0x20 || c == 0x7F) ? '?' : static_cast<char>(c);
    return out;
}

}  // namespace

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
    std::filesystem::create_directories(cfg::DB_DIR, ec);
    std::filesystem::create_directories(cfg::STORAGE_ROOT, ec);
    std::filesystem::create_directories(cfg::TEMP_DIR, ec);

    if (!db.connect(cfg::DB_PATH)) {
        std::cerr << "[-] Database connection failed.\n";
        return false;
    }
    if (!db.initializeTables()) {
        std::cerr << "[-] Failed to initialize database tables.\n";
        return false;
    }
    if (db.countUsers() == 0 && !runFirstTimeSetup(db)) {
        return false;
    }
    std::cout << "[+] SQLite database initialized successfully.\n";

    // SIGPIPE ignored (writes also use MSG_NOSIGNAL); SIGINT/SIGTERM request a clean
    // shutdown. No SA_RESTART so a blocked poll()/accept() is interrupted.
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
        // poll() with a timeout so a signal that lands just before the call can't
        // leave us blocked forever in accept().
        struct pollfd pfd;
        pfd.fd = server_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 1000);
        if (pr <= 0) continue;   // timeout or EINTR: re-check g_stop

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

        setSocketTimeouts(client_fd, cfg::SOCKET_TIMEOUT_SEC);   // no more slowloris-style hangs

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
    if (!recvHeader(client_fd, header)) return;   // short read or bad magic

    std::string filename(header.filename_len, '\0');
    if (header.filename_len > 0 && !readExact(client_fd, &filename[0], header.filename_len)) return;

    // ---- AUTH: the only request allowed without a session ------------------------
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

    // ---- Everything else requires a valid session -------------------------------
    const std::string token(header.session_token, strnlen(header.session_token, SESSION_TOKEN_LEN));
    SessionManager::Session sess;
    if (!sessions.validate(token, sess)) {
        sendError(client_fd, "Not authenticated or session expired. Please log in again.");
        return;
    }

    auto deny = [&](const char* op) {
        std::cerr << "[-] Security Block: '" << sess.username << "' (" << sess.role
                  << ") attempted unauthorized " << op << ".\n";
        db.saveTransfer(sess.user_id, printable(filename), op, 0, "DENIED_RBAC");
        sendError(client_fd, "Permission denied for your role.");
    };
    auto audit = [&](const std::string& name, const char* op, int size, const char* status) {
        db.saveTransfer(sess.user_id, name, op, size, status);
    };

    TransferService transferService;
    FileManager fm;

    switch (header.opcode) {

        case Opcode::LOGOUT: {
            sessions.destroy(token);
            sendOk(client_fd);
            break;
        }

        case Opcode::LIST: {
            if (!PermissionService::canBrowse(sess.role)) { deny("LIST"); break; }
            sendAckData(client_fd, fm.listDirectory(cfg::STORAGE_ROOT));
            break;
        }

        case Opcode::FILE_INFO: {
            if (!PermissionService::canBrowse(sess.role)) { deny("FILE_INFO"); break; }
            if (!isSafeName(filename)) { sendError(client_fd, "Invalid file name."); break; }
            sendAckData(client_fd, fm.getFileInfo(storagePath(filename)));
            break;
        }

        case Opcode::SEARCH: {
            if (!PermissionService::canBrowse(sess.role)) { deny("SEARCH"); break; }
            if (filename.empty()) { sendError(client_fd, "Empty search query."); break; }
            sendAckData(client_fd, fm.searchFiles(cfg::STORAGE_ROOT, filename));
            break;
        }

        case Opcode::UPLOAD: {
            if (!PermissionService::canUpload(sess.role)) { deny("UPLOAD"); break; }
            if (!isSafeName(filename)) { sendError(client_fd, "Invalid file name."); break; }
            if (header.payload_size > MAX_UPLOAD_SIZE) {
                audit(filename, "UPLOAD", 0, "FAILED_TOO_LARGE");
                sendError(client_fd, "File exceeds the maximum upload size.");
                break;
            }

            const bool ok = transferService.receiveFile(client_fd, filename, header.payload_size);
            if (ok) {
                const std::string path = storagePath(filename);
                const std::string hash = fm.calculateSHA256(path);
                std::cout << "[+] Upload complete. SHA-256: " << hash << "\n";
                db.saveFileRecord(sess.user_id, filename, path, header.payload_size, hash);
                audit(filename, "UPLOAD", static_cast<int>(header.payload_size), "SUCCESS");
                sendOk(client_fd);
            } else {
                audit(filename, "UPLOAD", 0, "FAILED");
                sendError(client_fd, "Upload failed.");
            }
            break;
        }

        case Opcode::DOWNLOAD: {
            if (!PermissionService::canDownload(sess.role)) { deny("DOWNLOAD"); break; }
            if (!isSafeName(filename)) { sendError(client_fd, "Invalid file name."); break; }

            uint32_t sent = 0;
            const bool ok = transferService.sendFile(client_fd, storagePath(filename), &sent);
            audit(filename, "DOWNLOAD", static_cast<int>(sent), ok ? "SUCCESS" : "FAILED");
            break;
        }

        case Opcode::DELETE_FILE: {
            if (!PermissionService::canDelete(sess.role)) { deny("DELETE"); break; }
            if (!isSafeName(filename)) { sendError(client_fd, "Invalid file name."); break; }

            const std::string path = storagePath(filename);
            const bool ok = fm.deleteFile(path);
            if (ok) db.deleteFileRecord(path);
            audit(filename, "DELETE", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Delete failed (file missing or not a regular file).");
            break;
        }

        case Opcode::RENAME_FILE: {
            if (!PermissionService::canRename(sess.role)) { deny("RENAME"); break; }

            const size_t delim = filename.find('|');
            if (delim == std::string::npos) { sendError(client_fd, "Malformed rename request."); break; }
            const std::string old_name = filename.substr(0, delim);
            const std::string new_name = filename.substr(delim + 1);
            if (!isSafeName(old_name) || !isSafeName(new_name)) {
                sendError(client_fd, "Invalid file name.");
                break;
            }

            const std::string old_path = storagePath(old_name);
            const std::string new_path = storagePath(new_name);
            std::error_code ec;
            if (std::filesystem::exists(new_path, ec)) {   // rename() would silently overwrite
                audit(filename, "RENAME", 0, "FAILED");
                sendError(client_fd, "A file with the new name already exists.");
                break;
            }

            const bool ok = fm.renameFile(old_path, new_path);
            if (ok) db.renameFileRecord(old_path, new_path, new_name);
            audit(filename, "RENAME", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Rename failed (source file missing?).");
            break;
        }

        case Opcode::CREATE_DIR: {
            if (!PermissionService::canManageDirs(sess.role)) { deny("MKDIR"); break; }
            if (!isSafeName(filename)) { sendError(client_fd, "Invalid directory name."); break; }

            // Created under the same root that LIST reads (it used to be one level up).
            const bool ok = fm.createDir(storagePath(filename));
            audit(filename, "MKDIR", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Could not create directory (it may already exist).");
            break;
        }

        case Opcode::REMOVE_DIR: {
            if (!PermissionService::canManageDirs(sess.role)) { deny("RMDIR"); break; }
            if (!isSafeName(filename)) { sendError(client_fd, "Invalid directory name."); break; }

            const bool ok = fm.removeDir(storagePath(filename));
            audit(filename, "RMDIR", 0, ok ? "SUCCESS" : "FAILED");
            if (ok) sendOk(client_fd);
            else sendError(client_fd, "Could not remove directory (it must exist and be empty).");
            break;
        }

        case Opcode::HISTORY: {
            // Role comes from the server-side session, never from anything the client claims.
            if (!PermissionService::canViewHistory(sess.role)) { deny("HISTORY"); break; }
            std::cout << "[*] User '" << sess.username << "' (" << sess.role << ") requested transfer history.\n";
            sendAckData(client_fd, db.getTransferHistoryLogs());
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
