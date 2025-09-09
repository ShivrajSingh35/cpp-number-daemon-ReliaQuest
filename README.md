# cpp-number-daemon-ReliaQuest

A simple C++ **daemon + CLI** with IPC to manage numbers (insert, delete, print, clear).  
Each number is stored with a timestamp. Duplicate entries are rejected.  
Multiple CLIs can connect simultaneously.

## Features
- Daemon stores numbers in-memory (`std::map<int,time_t>`) and persists to `/var/tmp/numd.db`.
- CLI menu to interact with daemon over Unix domain socket (`/tmp/numd.sock`).
- Safe concurrent access with `std::shared_mutex`.
- Tested with **GoogleTest**.
- Continuous Integration with **GitHub Actions**.

## Build
```bash
sudo apt-get install g++ cmake libgtest-dev
cmake -S . -B build
cmake --build build
