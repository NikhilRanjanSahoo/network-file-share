#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../include/FileManager.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <openssl/evp.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

FileManager::FileManager() {}
FileManager::~FileManager() {}

int FileManager::openFile(const std::string& filepath, int flags) {
    return open(filepath.c_str(), flags, 0640);   // was 0666
}

ssize_t FileManager::readFile(int fd, std::vector<char>& buffer, size_t count) {
    if (count > buffer.size()) count = buffer.size();
    return read(fd, buffer.data(), count);
}

ssize_t FileManager::writeFile(int fd, const std::vector<char>& buffer, size_t count) {
    if (count > buffer.size()) count = buffer.size();
    return write(fd, buffer.data(), count);
}

bool FileManager::writeFully(int fd, const char* data, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, data, n);
        if (w > 0) {
            data += w;
            n -= static_cast<size_t>(w);
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool FileManager::acquireReadLock(int fd) {
    struct flock file_lock = {};
    file_lock.l_type = F_RDLCK;
    file_lock.l_whence = SEEK_SET;
    return fcntl(fd, F_OFD_SETLKW, &file_lock) != -1;
}

bool FileManager::acquireWriteLock(int fd) {
    struct flock file_lock = {};
    file_lock.l_type = F_WRLCK;
    file_lock.l_whence = SEEK_SET;
    return fcntl(fd, F_OFD_SETLKW, &file_lock) != -1;
}

bool FileManager::releaseLock(int fd) {
    struct flock file_lock = {};
    file_lock.l_type = F_UNLCK;
    file_lock.l_whence = SEEK_SET;
    return fcntl(fd, F_OFD_SETLK, &file_lock) != -1;
}

std::string FileManager::listDirectory(const std::string& dirPath) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return "[-] Failed to open directory.\n";   // don't leak server paths to clients
    }

    std::ostringstream oss;
    struct dirent* entry;
    int count = 1;

    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        std::string fullPath = dirPath + "/" + name;
        struct stat fileStat;
        if (stat(fullPath.c_str(), &fileStat) != 0) continue;

        // snprintf instead of std::fixed/setprecision on the stream: those flags are
        // sticky and used to turn later "512 B" entries into "512.0 B".
        char sizebuf[48];
        if (S_ISDIR(fileStat.st_mode)) {
            std::snprintf(sizebuf, sizeof(sizebuf), "<DIR>");
        } else {
            double size = static_cast<double>(fileStat.st_size);
            if (size >= 1048576.0)      std::snprintf(sizebuf, sizeof(sizebuf), "%.1f MB", size / 1048576.0);
            else if (size >= 1024.0)    std::snprintf(sizebuf, sizeof(sizebuf), "%.0f KB", size / 1024.0);
            else                        std::snprintf(sizebuf, sizeof(sizebuf), "%lld B", static_cast<long long>(fileStat.st_size));
        }
        oss << count++ << ". " << std::left << std::setw(25) << name << sizebuf << "\n";
    }
    closedir(dir);

    std::string result = oss.str();
    if (result.empty()) return "[Empty Directory]\n";
    return result;
}

bool FileManager::deleteFile(const std::string& filepath) {
    return unlink(filepath.c_str()) == 0;
}

bool FileManager::renameFile(const std::string& oldPath, const std::string& newPath) {
    return rename(oldPath.c_str(), newPath.c_str()) == 0;
}

std::string FileManager::calculateSHA256(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "ERROR_FILE_NOT_FOUND";

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (mdctx == nullptr) return "ERROR_CTX_INIT";

    if (1 != EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr)) {
        EVP_MD_CTX_free(mdctx);
        return "ERROR_DIGEST_INIT";
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer))) {
        EVP_DigestUpdate(mdctx, buffer, static_cast<size_t>(file.gcount()));
    }
    if (file.gcount() > 0) {
        EVP_DigestUpdate(mdctx, buffer, static_cast<size_t>(file.gcount()));
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    EVP_DigestFinal_ex(mdctx, hash, &hashLen);
    EVP_MD_CTX_free(mdctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hashLen; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string FileManager::getFileInfo(const std::string& filepath) {
    struct stat fileStat;
    if (stat(filepath.c_str(), &fileStat) < 0) {
        return "[-] Error: File does not exist or access denied.\n";
    }

    // ctime() uses a shared static buffer (not thread-safe); localtime_r + strftime is.
    char timebuf[64] = "unknown";
    struct tm tmv;
    if (localtime_r(&fileStat.st_mtime, &tmv)) {
        std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tmv);
    }

    std::ostringstream info;
    info << "File: " << std::filesystem::path(filepath).filename().string() << "\n"   // name only, no server path
         << "Size: " << fileStat.st_size << " bytes\n"
         << "Permissions: "
         << ((fileStat.st_mode & S_IRUSR) ? "r" : "-")
         << ((fileStat.st_mode & S_IWUSR) ? "w" : "-")
         << ((fileStat.st_mode & S_IXUSR) ? "x" : "-") << "\n"
         << "Last Modified: " << timebuf << "\n";
    return info.str();
}

bool FileManager::createDir(const std::string& dirpath) {
    if (mkdir(dirpath.c_str(), 0750) == 0) return true;
    perror("[-] mkdir failed");
    return false;
}

bool FileManager::removeDir(const std::string& dirpath) {
    if (rmdir(dirpath.c_str()) == 0) return true;
    perror("[-] rmdir failed");
    return false;
}

std::string FileManager::searchFiles(const std::string& directory, const std::string& query) {
    namespace fs = std::filesystem;
    if (query.empty()) return "[-] Empty search query.\n";

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string q = lower(query);
    const int MAX_RESULTS = 200;

    std::error_code ec;
    fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    if (ec) return "[-] Error accessing storage.\n";

    std::string results;
    int count = 0;
    for (; it != end && count < MAX_RESULTS; it.increment(ec)) {
        if (ec) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        if (lower(it->path().filename().string()).find(q) == std::string::npos) continue;
        results += fs::relative(it->path(), directory, ec2).string() + "\n";   // relative: no server paths leaked
        ++count;
    }

    if (count >= MAX_RESULTS) results += "[!] Results truncated.\n";
    if (results.empty()) return "[-] No files found matching '" + query + "'.\n";
    return results;
}
