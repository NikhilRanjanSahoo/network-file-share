#ifndef NETWORKCLIENT_H
#define NETWORKCLIENT_H

#include <string>
#include <netinet/in.h>

class NetworkClient {
private:
    int sock_fd;
    std::string server_ip;
    int port;

public:
    NetworkClient(const std::string& ip, int port);
    ~NetworkClient();

    bool connectToServer();
    void disconnect();
    
    
    bool upload(const std::string& filepath);
    bool download(const std::string& filename, const std::string& dst_filepath);
};

#endif 
