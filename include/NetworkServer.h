#ifndef NETWORKSERVER_H
#define NETWORKSERVER_H
#include <unordered_map>
#include <mutex>
#include <string>
#include <netinet/in.h>
#include "Database.h"

class NetworkServer {
private:
    int server_fd;
    int port;
    struct sockaddr_in address;
    Database db;

    void handleClient(int client_fd);
    std::unordered_map<std::string, std::string> active_sessions; 
    std::mutex session_mutex;                                     
    std::string generateSessionToken();

public:
    NetworkServer(int port = 8080);
    ~NetworkServer();

    bool start();
    void listenForClients();
    void stop();
};

#endif 
