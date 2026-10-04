#ifndef NETWORKCLIENT_H
#define NETWORKCLIENT_H

#include <string>
#include <netinet/in.h>

class NetworkClient {
private:
    int sock_fd;
    std::string server_ip;
    int port;
    std::string current_role = "Guest";

public:
    NetworkClient(const std::string& ip, int port);
    ~NetworkClient();
    std::string getRole() const { return current_role; }
    bool connectToServer();
    void disconnect();
    
    bool authenticate(const std::string& username, const std::string& password);
    bool listFiles();
    bool searchFiles(const std::string& query);
    bool upload(const std::string& filepath);
    bool download(const std::string& filename, const std::string& dst_filepath);
    bool deleteRemoteFile(const std::string& filename);
    bool renameRemoteFile(const std::string& oldName, const std::string& newName);
    bool getFileInfo(const std::string& filename);
    bool createDirectory(const std::string& dirname);
    bool removeDirectory(const std::string& dirname);
    bool getHistory();
};

#endif 
