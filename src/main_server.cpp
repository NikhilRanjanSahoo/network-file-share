#include "../include/NetworkServer.h"

int main() {
    NetworkServer server(8080);
    if (server.start()) {
        server.listenForClients();
    }
    return 0;
}
