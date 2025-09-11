// store.h
#pragma once
#include <map>
#include <shared_mutex>
#include <string>
#include <time.h>

class NumberStore {
public:
    bool insert(int value, time_t now, std::string &msg);
    bool erase(int value, std::string &msg);
    void eraseAll(std::string &msg);
    std::map<int, time_t> getAll() const;
    size_t size() const;

    bool loadFromFile(const std::string &path);
    bool saveToFile(const std::string &path) const;

private:
    mutable std::shared_mutex mtx;
    std::map<int, time_t> db;
};
