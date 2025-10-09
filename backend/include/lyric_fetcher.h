#ifndef LYRIC_FETCHER_H
#define LYRIC_FETCHER_H

#include "dbus_client.h"
#include <memory>
#include <thread>
#include <atomic>

class LyricFetcher {
public:
    LyricFetcher();
    ~LyricFetcher();
    
    bool start();
    void stop();
    
    void run(); // 主运行循环

private:
    std::unique_ptr<DBusClient> dbus_client_;
    std::thread worker_thread_;
    std::atomic<bool> running_;
    
    void onLyricDataChanged(const LyricData& data);
    void outputLyricData(const LyricData& data);
};

#endif // LYRIC_FETCHER_H