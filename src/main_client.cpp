#include "../include/NetworkClient.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:\n";
        std::cerr << "  ./file_client auth <username> <password>\n";
        std::cerr << "  ./file_client list\n";
        std::cerr << "  ./file_client <upload|download> <src_filepath> [dst_filepath]\n";
        return 1;
    }

    std::string action = argv[1];
    NetworkClient client("127.0.0.1", 8080);

    if (action == "auth") {
        if (argc != 4) {
            std::cerr << "[-] Missing credentials.\n";
            return 1;
        }
        client.authenticate(argv[2], argv[3]);
    } else if (action == "list") {
        client.listFiles();
    } else if (action == "upload") {
        if (argc < 3) return 1;
        client.upload(argv[2]);
    } else if (action == "download") {
        if (argc < 3) return 1;
        std::string dst = (argc == 4) ? argv[3] : "";
        client.download(argv[2], dst);
    } else {
        std::cerr << "[-] Unknown command.\n";
    }

    return 0;
}
