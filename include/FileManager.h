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
    
    
    
    bool deleteFile(const std::string& filepath);
    bool renameFile(const std::string& oldPath, const std::string& newPath);
    bool acquireReadLock(int fd);
    bool acquireWriteLock(int fd);
    bool releaseLock(int fd);
    bool createDir(const std::string& dirpath);
    bool removeDir(const std::string& dirpath);
    
    std::string getFileInfo(const std::string& filepath);
    std::string listDirectory(const std::string& dirPath);
    std::string calculateSHA256(const std::string& filepath);
    std::string searchFiles(const std::string& directory, const std::string& query);
    

};

#endif
