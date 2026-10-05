#include "../include/NetworkServer.h"
#include "../include/config.h"
#include <cstdlib>

int main(int argc, char** argv) {
    const int port = (argc > 1) ? std::atoi(argv[1]) : cfg::DEFAULT_PORT;
    NetworkServer server(port);
    if (!server.start()) {
        return 1;
    }
    server.listenForClients();   
    return 0;
}
