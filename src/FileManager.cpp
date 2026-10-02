#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../include/FileManager.h"
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <sys/stat.h>

FileManager::FileManager() {}
FileManager::~FileManager() {}

int FileManager::openFile(const std::string& filepath, int flags) {
    
    return open(filepath.c_str(), flags, 0666);
}

ssize_t FileManager::readFile(int fd, std::vector<char>& buffer, size_t count) {
    return read(fd, buffer.data(), count);
}

ssize_t FileManager::writeFile(int fd, const std::vector<char>& buffer, size_t count) {
    return write(fd, buffer.data(), count);
}

bool FileManager::createDir(const std::string& dirPath) {
    
    return mkdir(dirPath.c_str(), 0777) == 0;
}

bool FileManager::removeDir(const std::string& dirPath) {
    return rmdir(dirPath.c_str()) == 0;
}

std::string FileManager::getFileInfo(const std::string& filepath) {
    struct stat fileStat;
    if (stat(filepath.c_str(), &fileStat) < 0) {
        return "File not found.";
    }
    return "Size: " + std::to_string(fileStat.st_size) + " bytes";
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
