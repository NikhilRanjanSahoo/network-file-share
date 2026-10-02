#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <string>
#include <vector>
#include <sys/stat.h>

class FileManager {
public:
    FileManager();
    ~FileManager();

    
    int openFile(const std::string& filepath, int flags);
    ssize_t readFile(int fd, std::vector<char>& buffer, size_t count);
    ssize_t writeFile(int fd, const std::vector<char>& buffer, size_t count);
    
    
    bool createDir(const std::string& dirPath);
    bool removeDir(const std::string& dirPath);
    
    
    std::string getFileInfo(const std::string& filepath);

    // Concurrency control wrappers
    bool acquireReadLock(int fd);
    bool acquireWriteLock(int fd);
    bool releaseLock(int fd);
};

#endif
