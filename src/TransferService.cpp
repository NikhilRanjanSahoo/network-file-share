#include "../include/TransferService.h"
#include "../include/protocol.h"
#include "../include/config.h"
#include "../include/net_io.h"
#include "../include/Crypto.h"
#include <algorithm>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

TransferService::TransferService() {}
TransferService::~TransferService() {}

bool TransferService::receiveFile(int socket_fd, const std::string& destPath, uint32_t payload_size) {
    std::string tmpPath;
    int file_fd = -1;
    try {
        tmpPath = std::string(cfg::TEMP_DIR) + "/upload_" + randomHex(8);
        file_fd = fileManager.openFile(tmpPath, O_WRONLY | O_CREAT | O_EXCL);
    } catch (const std::exception& e) {
        std::cerr << "[-] Could not create temp name: " << e.what() << "\n";
        return false;
    }
    if (file_fd < 0) {
        std::cerr << "[-] Failed to open temp file for upload.\n";
        return false;
    }

    fileManager.acquireWriteLock(file_fd);
    std::cout << "[*] Receiving " << payload_size << " bytes for " << destPath << "...\n";

    std::vector<char> buffer(64 * 1024);
    uint32_t total_received = 0;
    bool ok = true;

    while (total_received < payload_size) {
        size_t want = std::min<size_t>(buffer.size(), payload_size - total_received);
        if (!readExact(socket_fd, buffer.data(), want)) { ok = false; break; }
        if (!fileManager.writeFully(file_fd, buffer.data(), want)) { ok = false; break; }
        total_received += static_cast<uint32_t>(want);
    }

    fileManager.releaseLock(file_fd);
    close(file_fd);

    if (ok && !fileManager.renameFile(tmpPath, destPath)) {
        std::cerr << "[-] Could not move upload into place.\n";
        ok = false;
    }
    if (!ok) {
        unlink(tmpPath.c_str());
        return false;
    }

    std::cout << "[+] Saved " << destPath << ".\n";
    return true;
}

bool TransferService::sendFile(int socket_fd, const std::string& filepath, uint32_t* out_sent) {
    if (out_sent) *out_sent = 0;

    int file_fd = fileManager.openFile(filepath, O_RDONLY | O_NOFOLLOW);
    if (file_fd < 0) {
        sendError(socket_fd, "File not found or not accessible.");
        return false;
    }

    struct stat st;
    if (fstat(file_fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(file_fd);
        sendError(socket_fd, "Not a regular file.");
        return false;
    }
    if (static_cast<uint64_t>(st.st_size) > 0xFFFFFFFFULL) {
        close(file_fd);
        sendError(socket_fd, "File too large for this protocol.");
        return false;
    }
    const uint32_t file_size = static_cast<uint32_t>(st.st_size);

    fileManager.acquireReadLock(file_fd);
    std::cout << "[*] Streaming " << file_size << " bytes from " << filepath << "...\n";

    bool ok = sendHeader(socket_fd, Opcode::ACK, 0, file_size);

    std::vector<char> buffer(64 * 1024);
    uint32_t sent = 0;
    while (ok && sent < file_size) {
        ssize_t n = fileManager.readFile(file_fd, buffer, std::min<size_t>(buffer.size(), file_size - sent));
        if (n <= 0) { ok = false; break; }
        if (!writeAll(socket_fd, buffer.data(), static_cast<size_t>(n))) { ok = false; break; }
        sent += static_cast<uint32_t>(n);
    }

    fileManager.releaseLock(file_fd);
    close(file_fd);

    if (out_sent) *out_sent = sent;
    if (ok) std::cout << "[+] Streamed " << filepath << " successfully.\n";
    return ok;
}
