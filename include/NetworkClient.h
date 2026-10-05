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
    std::string session_token;

    bool sendRequest(uint8_t opcode, const std::string& name, uint32_t payload_size = 0);
    bool readReply(uint32_t& payload_size);
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
    bool listFiles(const std::string& path = "");
    bool searchFiles(const std::string& query);
    bool upload(const std::string& filepath, const std::string& dest = "");
    bool download(const std::string& filename, const std::string& destination);
    bool downloadToLocal(const std::string& filename, const std::string& dst_filepath);
    bool deleteRemoteFile(const std::string& filename);
    bool renameRemoteFile(const std::string& oldName, const std::string& newName);
    bool getFileInfo(const std::string& filename);
    bool createDirectory(const std::string& dirname);
    bool removeDirectory(const std::string& dirname);
    bool getHistory();

    bool listUsers();
    bool addUser(const std::string& user, const std::string& role, const std::string& password);
    bool removeUser(const std::string& user);
    bool setUserRole(const std::string& user, const std::string& role);
    bool listPolicies();
    bool setPolicy(const std::string& role, const std::string& scope, bool r, bool w, bool d);
};

#endif
