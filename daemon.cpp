// daemon.cpp
// Build: g++ -std=c++17 -O2 -pthread daemon.cpp -o numd
// Protocol: each message is uint32_t (network byte order) length followed by payload bytes (UTF-8 text).
// Commands (payload text, single line):
//   INSERT <n>
//   DELETE <n>
//   PRINT
//   DELETEALL
//   QUIT
// Replies are plain text payloads (also length-prefixed).
// Persistence file path: /var/tmp/numd.db (one "num timestamp" per line)

#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <fstream>

const char *SOCK_PATH = "/tmp/numd.sock";
const char *DB_PATH = "/var/tmp/numd.db";

static int listen_fd = -1;
static bool running = true;

std::map<int, time_t> db;
std::shared_mutex db_mutex; // readers/writers

void log_stdout(const std::string &s) {
    std::cerr << s << std::endl;
}

// ---------- persistence ----------
bool load_db_from_file(const char *path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    std::unique_lock lock(db_mutex);
    db.clear();
    int num;
    long long ts;
    while (ifs >> num >> ts) {
        if (num > 0) db[num] = static_cast<time_t>(ts);
    }
    return true;
}

bool save_db_to_file(const char *path) {
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open()) return false;
    std::shared_lock lock(db_mutex);
    for (const auto &p : db) {
        ofs << p.first << ' ' << (long long)p.second << '\n';
    }
    return true;
}

// ---------- util: read/write exact ----------
ssize_t read_exact(int fd, void *buf, size_t count) {
    size_t left = count;
    char *p = (char*)buf;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return 0; // EOF
        p += r; left -= r;
    }
    return count;
}

ssize_t write_exact(int fd, const void *buf, size_t count) {
    const char *p = (const char*)buf;
    size_t left = count;
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
    if (r == 0) return false; // peer closed
    if (r != sizeof(len_net)) return false;
    uint32_t len = ntohl(len_net);
    out.clear();
    if (len == 0) return true;
    out.resize(len);
    if (read_exact(fd, &out[0], len) != (ssize_t)len) return false;
    return true;
}

// ---------- command handling ----------
std::vector<std::string> split_ws(const std::string &s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string w;
    while (iss >> w) out.push_back(w);
    return out;
}

std::string handle_command(const std::string &cmdline) {
    auto parts = split_ws(cmdline);
    if (parts.empty()) return "ERROR Empty command";
    std::string cmd = parts[0];
    for (auto &c : cmd) c = toupper((unsigned char)c);

    if (cmd == "INSERT") {
        if (parts.size() != 2) return "ERROR Usage: INSERT <positive_integer>";
        long long v = 0;
        try { v = std::stoll(parts[1]); } catch (...) { return "ERROR Invalid integer"; }
        if (v <= 0) return "ERROR Only positive integers allowed";
        int val = (int)v;
        {
            std::unique_lock lock(db_mutex);
            if (db.find(val) != db.end()) return "ERROR Duplicate entry not permitted";
            time_t now = time(nullptr);
            db[val] = now;
            save_db_to_file(DB_PATH); // persist
            std::ostringstream oss; oss << "OK Inserted " << val << " " << (long long)now;
            log_stdout(oss.str());
            return oss.str();
        }
    } else if (cmd == "DELETE") {
        if (parts.size() != 2) return "ERROR Usage: DELETE <positive_integer>";
        long long v = 0;
        try { v = std::stoll(parts[1]); } catch (...) { return "ERROR Invalid integer"; }
        if (v <= 0) return "ERROR Only positive integers allowed";
        int val = (int)v;
        {
            std::unique_lock lock(db_mutex);
            auto it = db.find(val);
            if (it == db.end()) return "ERROR Not found";
            time_t ts = it->second;
            db.erase(it);
            save_db_to_file(DB_PATH);
            std::ostringstream oss; oss << "OK Deleted " << val << " " << (long long)ts;
            log_stdout(oss.str());
            return oss.str();
        }
    } else if (cmd == "PRINT") {
        std::shared_lock lock(db_mutex);
        std::ostringstream oss;
        oss << "OK BEGIN_LIST " << db.size() << "\n";
        for (const auto &p : db) oss << p.first << " " << (long long)p.second << "\n";
        oss << "OK END_LIST";
        return oss.str();
    } else if (cmd == "DELETEALL") {
        std::unique_lock lock(db_mutex);
        size_t n = db.size();
        db.clear();
        save_db_to_file(DB_PATH);
        std::ostringstream oss; oss << "OK DeletedAll " << n;
        log_stdout(oss.str());
        return oss.str();
    } else if (cmd == "QUIT" || cmd == "EXIT") {
        return "OK BYE";
    } else {
        return "ERROR Unknown command";
    }
}

// ---------- client worker ----------
void client_worker(int client_fd) {
    std::string req;
    while (true) {
        bool ok = recv_payload(client_fd, req);
        if (!ok) break;
        if (req.empty()) continue;
        std::string reply = handle_command(req);
        if (!send_payload(client_fd, reply)) break;
        auto parts = split_ws(req);
        if (!parts.empty()) {
            std::string c = parts[0];
            for (auto &ch : c) ch = toupper((unsigned char)ch);
            if (c == "QUIT" || c == "EXIT") break;
        }
    }
    close(client_fd);
}

// ---------- signal handling ----------
void shutdown_handler(int) {
    running = false;
    if (listen_fd >= 0) close(listen_fd);
    save_db_to_file(DB_PATH);
    unlink(SOCK_PATH);
    log_stdout("Daemon shutting down, DB saved.");
    exit(0);
}

int main() {
    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);

    // load DB if present
    load_db_from_file(DB_PATH);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    unlink(SOCK_PATH);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, 32) < 0) {
        perror("listen");
        close(listen_fd);
        unlink(SOCK_PATH);
        return 1;
    }
    log_stdout(std::string("Daemon running. Socket: ") + SOCK_PATH + " DB: " + DB_PATH);

    std::vector<std::thread> workers;
    while (running) {
        int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        // set close-on-exec
        int flags = fcntl(client_fd, F_GETFD);
        fcntl(client_fd, F_SETFD, flags | FD_CLOEXEC);
        workers.emplace_back(client_worker, client_fd);
        // detach finished threads periodically to avoid growth
        for (auto it = workers.begin(); it != workers.end();) {
            if (it->joinable()) {
                it->detach();
                it = workers.erase(it);
            } else ++it;
        }
    }

    save_db_to_file(DB_PATH);
    close(listen_fd);
    unlink(SOCK_PATH);
    return 0;
}
