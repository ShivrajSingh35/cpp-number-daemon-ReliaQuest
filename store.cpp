// store.cpp
#include "store.h"
#include <fstream>
#include <sstream>

bool NumberStore::insert(int value, time_t now, std::string &msg) {
    if (value <= 0) {
        msg = "ERROR Only positive integers allowed";
        return false;
    }
    std::unique_lock lock(mtx);
    if (db.find(value) != db.end()) {
        msg = "ERROR Duplicate entry not permitted";
        return false;
    }
    db[value] = now;
    std::ostringstream oss;
    oss << "OK Inserted " << value << " " << (long long)now;
    msg = oss.str();
    return true;
}

bool NumberStore::erase(int value, std::string &msg) {
    std::unique_lock lock(mtx);
    auto it = db.find(value);
    if (it == db.end()) {
        msg = "ERROR Not found";
        return false;
    }
    time_t ts = it->second;
    db.erase(it);
    std::ostringstream oss;
    oss << "OK Deleted " << value << " " << (long long)ts;
    msg = oss.str();
    return true;
}

void NumberStore::eraseAll(std::string &msg) {
    std::unique_lock lock(mtx);
    size_t n = db.size();
    db.clear();
    std::ostringstream oss;
    oss << "OK DeletedAll " << n;
    msg = oss.str();
}

std::map<int, time_t> NumberStore::getAll() const {
    std::shared_lock lock(mtx);
    return db;
}

size_t NumberStore::size() const {
    std::shared_lock lock(mtx);
    return db.size();
}

bool NumberStore::loadFromFile(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    std::unique_lock lock(mtx);
    db.clear();
    int num;
    long long ts;
    while (ifs >> num >> ts) {
        if (num > 0) db[num] = static_cast<time_t>(ts);
    }
    return true;
}

bool NumberStore::saveToFile(const std::string &path) const {
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open()) return false;
    std::shared_lock lock(mtx);
    for (const auto &p : db) {
        ofs << p.first << ' ' << (long long)p.second << '\n';
    }
    return true;
}
