// cli.cpp
// Build: g++ -std=c++17 -O2 cli.cpp -o numctl
// Uses same length-prefixed protocol as daemon.

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

const char *SOCK_PATH = "/tmp/numd.sock";

ssize_t read_exact(int fd, void *buf, size_t count) {
    size_t left = count; char *p = (char*)buf;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return 0;
        p += r; left -= r;
    }
    return count;
}

ssize_t write_exact(int fd, const void *buf, size_t count) {
    const char *p = (const char*)buf; size_t left = count;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w; left -= w;
    }
    return count;
}

bool send_payload(int fd, const std::string &payload) {
    uint32_t len = htonl((uint32_t)payload.size());
    if (write_exact(fd, &len, sizeof(len)) != sizeof(len)) return false;
    if (!payload.empty()) {
        if (write_exact(fd, payload.data(), payload.size()) != (ssize_t)payload.size()) return false;
    }
    return true;
}

bool recv_payload(int fd, std::string &out) {
    uint32_t len_net;
    ssize_t r = read_exact(fd, &len_net, sizeof(len_net));
    if (r == 0) return false;
    if (r != sizeof(len_net)) return false;
    uint32_t len = ntohl(len_net);
    out.clear();
    if (len == 0) return true;
    out.resize(len);
    if (read_exact(fd, &out[0], len) != (ssize_t)len) return false;
    return true;
}

long long prompt_positive_int() {
    while (true) {
        std::cout << "Enter positive integer: ";
        long long v;
        if (!(std::cin >> v)) {
            std::cin.clear();
            std::string dummy; std::getline(std::cin, dummy);
            std::cout << "Invalid input. Please enter an integer.\n";
            continue;
        }
        std::string dummy; std::getline(std::cin, dummy);
        if (v <= 0) { std::cout << "Only positive integers allowed.\n"; continue; }
        return v;
    }
}

int main() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_un addr; memset(&addr,0,sizeof(addr));
    addr.sun_family = AF_UNIX; strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); close(fd); return 1; }

    while (true) {
        std::cout << "\nChoose:\n1) Insert a number\n2) Delete a number\n3) Print all numbers\n4) Delete all numbers\n5) Exit\nEnter choice: ";
        int choice;
        if (!(std::cin >> choice)) {
            std::cin.clear(); std::string dummy; std::getline(std::cin, dummy);
            std::cout << "Invalid input. Try again.\n"; continue;
        }
        std::string cmd;
        std::string rep;
        switch (choice) {
            case 1: {
                long long v = prompt_positive_int();
                cmd = "INSERT " + std::to_string(v);
                send_payload(fd, cmd);
                if (!recv_payload(fd, rep)) { std::cout << "No response (server closed)\n"; close(fd); return 1; }
                std::cout << rep << "\n";
                break;
            }
            case 2: {
                long long v = prompt_positive_int();
                cmd = "DELETE " + std::to_string(v);
                send_payload(fd, cmd);
                if (!recv_payload(fd, rep)) { std::cout << "No response\n"; close(fd); return 1; }
                std::cout << rep << "\n";
                break;
            }
            case 3: {
                cmd = "PRINT";
                send_payload(fd, cmd);
                if (!recv_payload(fd, rep)) { std::cout << "No response\n"; close(fd); return 1; }
                // parse reply
                std::istringstream iss(rep);
                std::string line;
                bool headerPrinted = false;
                while (std::getline(iss, line)) {
                    if (line.rfind("OK BEGIN_LIST",0) == 0) {
                        std::cout << "Stored items:\n";
                        headerPrinted = true;
                        continue;
                    } else if (line.rfind("OK END_LIST",0) == 0) {
                        break;
                    } else if (line.rfind("OK",0) == 0 || line.rfind("ERROR",0) == 0) {
                        std::cout << line << "\n";
                    } else if (!line.empty()) {
                        std::istringstream l2(line);
                        int num; long long ts;
                        if (l2 >> num >> ts) {
                            std::cout << num << "  (timestamp: " << ts << ")\n";
                        }
                    }
                }
                if (!headerPrinted && rep.rfind("OK",0) != 0) std::cout << rep << "\n";
                break;
            }
            case 4: {
                cmd = "DELETEALL";
                send_payload(fd, cmd);
                if (!recv_payload(fd, rep)) { std::cout << "No response\n"; close(fd); return 1; }
                std::cout << rep << "\n";
                break;
            }
            case 5: {
                cmd = "QUIT";
                send_payload(fd, cmd);
                if (recv_payload(fd, rep)) std::cout << rep << "\n";
                close(fd);
                return 0;
            }
            default:
                std::cout << "Invalid choice\n"; break;
        }
    }

    close(fd);
    return 0;
}
