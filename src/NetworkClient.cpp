#include "../include/NetworkClient.h"
#include "../include/PathUtils.h"
#include "../include/net_io.h"
#include "../include/protocol.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr int CLIENT_TIMEOUT_SEC = 30;
}

NetworkClient::NetworkClient(const std::string& ip, int port)
    : sock_fd(-1), server_ip(ip), port(port) {}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connectToServer() {
    disconnect();
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "[-] Socket creation error.\n";
        return false;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[-] Invalid address / Address not supported.\n";
        disconnect();
        return false;
    }
    setSocketTimeouts(sock_fd, CLIENT_TIMEOUT_SEC);
    if (connect(sock_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "[-] Connection failed.\n";
        disconnect();
        return false;
    }
    return true;
}

void NetworkClient::disconnect() {
    if (sock_fd != -1) {
        close(sock_fd);
        sock_fd = -1;
    }
}

bool NetworkClient::sendRequest(uint8_t opcode, const std::string& name, uint32_t payload_size) {
    if (name.size() > 255) {
        std::cerr << "[-] Name exceeds the protocol limit (255 bytes).\n";
        return false;
    }
    if (!sendHeader(sock_fd, opcode, static_cast<uint8_t>(name.size()), payload_size, session_token)) {
        std::cerr << "[-] Failed to send request to server.\n";
        return false;
    }
    if (!name.empty() && !writeAll(sock_fd, name.data(), name.size())) {
        std::cerr << "[-] Failed to send request to server.\n";
        return false;
    }
    return true;
}

bool NetworkClient::readTextPayload(uint32_t size, std::string& out) {
    out.clear();
    if (size == 0) return true;
    if (size > MAX_META_PAYLOAD) {
        std::cerr << "[-] Server reply is unreasonably large; ignoring.\n";
        return false;
    }
    out.assign(size, '\0');
    if (!readExact(sock_fd, &out[0], size)) {
        std::cerr << "[-] Connection lost while reading the server reply.\n";
        return false;
    }
    return true;
}

bool NetworkClient::readReply(uint32_t& payload_size) {
    PacketHeader h;
    if (!recvHeader(sock_fd, h)) {
        std::cerr << "[-] No valid response from server (timeout or disconnect).\n";
        return false;
    }
    if (h.opcode == Opcode::ERROR_REPLY) {
        std::string msg;
        readTextPayload(h.payload_size, msg);
        std::cerr << "[-] Server: " << (msg.empty() ? "request failed" : msg) << "\n";
        return false;
    }
    if (h.opcode != Opcode::ACK) {
        std::cerr << "[-] Unexpected reply from server.\n";
        return false;
    }
    payload_size = h.payload_size;
    return true;
}

bool NetworkClient::simpleCommand(uint8_t opcode, const std::string& name, const std::string& okMessage) {
    if (!connectToServer()) return false;
    uint32_t n = 0;
    const bool ok = sendRequest(opcode, name) && readReply(n);
    disconnect();
    if (ok) std::cout << okMessage << "\n";
    return ok;
}

bool NetworkClient::textCommand(uint8_t opcode, const std::string& name, const std::string& title) {
    if (!connectToServer()) return false;
    uint32_t n = 0;
    std::string body;
    const bool ok = sendRequest(opcode, name) && readReply(n) && readTextPayload(n, body);
    disconnect();
    if (ok) std::cout << "\n" << title << (body.empty() ? "[Empty]\n" : body) << "\n";
    return ok;
}

bool NetworkClient::authenticate(const std::string& username, const std::string& password) {
    session_token.clear();
    current_role = "Guest";

    if (username.empty() || username.size() > 31 || password.empty() || password.size() > 31) {
        std::cerr << "[-] Username and password must be 1-31 characters.\n";
        return false;
    }
    if (!connectToServer()) return false;

    AuthPayload payload;
    std::memset(&payload, 0, sizeof(payload));
    std::memcpy(payload.username, username.data(), username.size());
    std::memcpy(payload.password, password.data(), password.size());

    uint32_t n = 0;
    std::string response;
    const bool ok = sendRequest(Opcode::AUTH, "", sizeof(AuthPayload)) &&
                    writeAll(sock_fd, &payload, sizeof(payload)) &&
                    readReply(n) && readTextPayload(n, response);
    disconnect();
    if (!ok) return false;

    if (response.rfind("SUCCESS:", 0) != 0) return false;
    const std::string rest = response.substr(8);
    const size_t colon = rest.find(':');
    if (colon == std::string::npos) return false;
    const std::string role = rest.substr(0, colon);
    const std::string token = rest.substr(colon + 1);
    if (token.size() != SESSION_TOKEN_LEN) return false;

    current_role = role;
    session_token = token;
    return true;
}

bool NetworkClient::logout() {
    bool ok = true;
    if (!session_token.empty()) {
        ok = simpleCommand(Opcode::LOGOUT, "", "[+] Logged out.");
    }
    session_token.clear();
    current_role = "Guest";
    return ok;
}

bool NetworkClient::upload(const std::string& filepath, const std::string& dest) {
    const std::string base = baseName(filepath);
    if (!isSafeName(base)) {
        std::cerr << "[-] Invalid file name (empty, too long, or contains reserved characters).\n";
        return false;
    }

    std::string remote = dest.empty() ? "home/" : dest;
    if (remote == "home" || remote == "public") remote += "/";
    if (remote.back() == '/') remote += base;
    if (remote.size() > 255) {
        std::cerr << "[-] Destination path is too long.\n";
        return false;
    }

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[-] Failed to open " << filepath << "\n";
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 0) {
        std::cerr << "[-] Could not determine the size of " << filepath << "\n";
        return false;
    }
    if (static_cast<uint64_t>(size) > MAX_UPLOAD_SIZE) {
        std::cerr << "[-] File is larger than the " << (MAX_UPLOAD_SIZE / (1024 * 1024)) << " MiB upload limit.\n";
        return false;
    }
    file.seekg(0, std::ios::beg);

    if (!connectToServer()) return false;
    if (!sendRequest(Opcode::UPLOAD, remote, static_cast<uint32_t>(size))) {
        disconnect();
        return false;
    }

    std::cout << "[*] Uploading to " << remote << " (" << size << " bytes)...\n";

    std::vector<char> buffer(64 * 1024);
    std::streamoff sent = 0;
    bool writeFailed = false;
    while (sent < size) {
        const std::streamsize want = static_cast<std::streamsize>(
            std::min<std::streamoff>(static_cast<std::streamoff>(buffer.size()), size - sent));
        file.read(buffer.data(), want);
        const std::streamsize got = file.gcount();
        if (got <= 0) {
            std::cerr << "[-] Failed while reading the local file.\n";
            disconnect();
            return false;
        }
        if (!writeAll(sock_fd, buffer.data(), static_cast<size_t>(got))) {
            writeFailed = true;
            break;
        }
        sent += got;
    }

    uint32_t n = 0;
    const bool ok = readReply(n);
    disconnect();
    if (ok) std::cout << "[+] Server successfully saved " << remote << "!\n";
    else if (writeFailed) std::cerr << "[-] Connection lost during upload.\n";
    return ok && !writeFailed;
}

bool NetworkClient::download(const std::string& filename, const std::string& destination) {
    if (filename.empty() || filename.size() > 240 || destination.find('|') != std::string::npos) {
        std::cerr << "[-] Invalid path.\n";
        return false;
    }
    if (!connectToServer()) return false;
    uint32_t n = 0;
    std::string saved;
    const bool ok = sendRequest(Opcode::COPY_FILE, destination.empty() ? filename : filename + "|" + destination) &&
                    readReply(n) && readTextPayload(n, saved);
    disconnect();
    if (ok) std::cout << "[+] Saved to your storage: " << saved << "\n";
    return ok;
}

bool NetworkClient::downloadToLocal(const std::string& filename, const std::string& dst_filepath) {
    if (filename.empty() || filename.size() > 255) {
        std::cerr << "[-] Invalid remote path.\n";
        return false;
    }
    if (!connectToServer()) return false;

    uint32_t incoming_size = 0;
    if (!sendRequest(Opcode::DOWNLOAD, filename) || !readReply(incoming_size)) {
        disconnect();
        return false;
    }
    std::cout << "[+] Incoming file size: " << incoming_size << " bytes.\n";

    std::string final_dest = dst_filepath;
    if (final_dest.empty()) {
        mkdir("downloads", 0755);
        final_dest = "downloads/copy_" + baseName(filename);
    }

    std::ofstream outfile(final_dest, std::ios::binary | std::ios::trunc);
    if (!outfile.is_open()) {
        std::cerr << "[-] Failed to create local file: " << final_dest << "\n";
        disconnect();
        return false;
    }

    std::vector<char> buffer(64 * 1024);
    uint32_t total_received = 0;
    while (total_received < incoming_size) {
        const size_t want = std::min<size_t>(buffer.size(), incoming_size - total_received);
        if (!readExact(sock_fd, buffer.data(), want)) break;
        outfile.write(buffer.data(), static_cast<std::streamsize>(want));
        total_received += static_cast<uint32_t>(want);
    }
    outfile.close();
    disconnect();

    if (total_received != incoming_size || outfile.fail()) {
        std::remove(final_dest.c_str());
        std::cerr << "[-] Download incomplete (" << total_received << "/" << incoming_size
                  << " bytes); partial file removed.\n";
        return false;
    }

    std::cout << "[+] Successfully saved to " << final_dest << "\n";
    return true;
}

bool NetworkClient::listFiles(const std::string& path) {
    return textCommand(Opcode::LIST, path, "Server files:\n");
}

bool NetworkClient::searchFiles(const std::string& query) {
    return textCommand(Opcode::SEARCH, query, "[+] Search Results:\n");
}

bool NetworkClient::getFileInfo(const std::string& filename) {
    return textCommand(Opcode::FILE_INFO, filename, "");
}

bool NetworkClient::getHistory() {
    return textCommand(Opcode::HISTORY, "", "");
}

bool NetworkClient::deleteRemoteFile(const std::string& filename) {
    return simpleCommand(Opcode::DELETE_FILE, filename, "[+] Deleted " + filename + " successfully.");
}

bool NetworkClient::renameRemoteFile(const std::string& oldName, const std::string& newName) {
    return simpleCommand(Opcode::RENAME_FILE, oldName + "|" + newName,
                         "[+] Renamed to " + newName + " successfully.");
}

bool NetworkClient::createDirectory(const std::string& dirname) {
    return simpleCommand(Opcode::CREATE_DIR, dirname, "[+] Directory '" + dirname + "' created successfully.");
}

bool NetworkClient::removeDirectory(const std::string& dirname) {
    return simpleCommand(Opcode::REMOVE_DIR, dirname, "[+] Directory '" + dirname + "' removed successfully.");
}

bool NetworkClient::listUsers() {
    return textCommand(Opcode::LIST_USERS, "", "");
}

bool NetworkClient::addUser(const std::string& user, const std::string& role, const std::string& password) {
    return simpleCommand(Opcode::ADD_USER, user + "|" + role + "|" + password, "[+] User '" + user + "' created.");
}

bool NetworkClient::removeUser(const std::string& user) {
    return simpleCommand(Opcode::REMOVE_USER, user, "[+] User '" + user + "' removed.");
}

bool NetworkClient::setUserRole(const std::string& user, const std::string& role) {
    return simpleCommand(Opcode::SET_ROLE, user + "|" + role, "[+] Role of '" + user + "' set to " + role + ".");
}

bool NetworkClient::listPolicies() {
    return textCommand(Opcode::LIST_POLICY, "", "");
}

bool NetworkClient::setPolicy(const std::string& role, const std::string& scope, bool r, bool w, bool d) {
    return simpleCommand(Opcode::SET_POLICY,
                         role + "|" + scope + "|" + (r ? "1" : "0") + "|" + (w ? "1" : "0") + "|" + (d ? "1" : "0"),
                         "[+] Policy updated.");
}
