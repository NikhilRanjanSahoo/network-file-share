#ifndef NETWORKSERVER_H
#define NETWORKSERVER_H

#include <atomic>
#include <string>
#include <netinet/in.h>
#include "Database.h"
#include "SessionManager.h"

class NetworkServer {
private:
    int server_fd;
    int port;
    struct sockaddr_in address;
    Database db;
    SessionManager sessions;
    std::atomic<int> active_clients{0};

    void handleClient(int client_fd, const std::string& peer_ip);

public:
    explicit NetworkServer(int port = 8080);
    ~NetworkServer();

    bool start();
    void listenForClients();   // returns after SIGINT/SIGTERM
    void stop();
};

#endif
