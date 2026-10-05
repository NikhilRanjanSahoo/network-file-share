#ifndef NETWORKCLIENT_H
#define NETWORKCLIENT_H

#include <stdint.h>
#include <string>
#include <netinet/in.h>

class NetworkClient {
private:
    int sock_fd;
    std::string server_ip;
    int port;
    std::string current_role = "Guest";
    std::string session_token;   // issued by the server at login; kept in memory only

    bool sendRequest(uint8_t opcode, const std::string& name, uint32_t payload_size = 0);
    bool readReply(uint32_t& payload_size);   // false on ERROR_REPLY / I/O failure (prints the reason)
    bool readTextPayload(uint32_t size, std::string& out);
    bool simpleCommand(uint8_t opcode, const std::string& name, const std::string& okMessage);
    bool textCommand(uint8_t opcode, const std::string& name, const std::string& title);

public:
    NetworkClient(const std::string& ip, int port);
    ~NetworkClient();
    std::string getRole() const { return current_role; }
    bool connectToServer();
    void disconnect();

    bool authenticate(const std::string& username, const std::string& password);
    bool logout();
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
