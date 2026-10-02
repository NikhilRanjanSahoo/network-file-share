#ifndef NETWORKSERVER_H
#define NETWORKSERVER_H

#include <string>
#include <netinet/in.h>

class NetworkServer {
private:
    int server_fd;
    int port;
    struct sockaddr_in address;

    void handleClient(int client_fd);

public:
    NetworkServer(int port = 8080);
    ~NetworkServer();

    bool start();
    void listenForClients();
    void stop();
};

#endif
