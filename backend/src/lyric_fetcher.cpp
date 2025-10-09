#include "lyric_fetcher.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

LyricFetcher::LyricFetcher() : running_(false) {
    dbus_client_ = std::make_unique<DBusClient>();
}

LyricFetcher::~LyricFetcher() {
    stop();
}

bool LyricFetcher::start() {
    if (!dbus_client_->connect()) {
        std::cerr << "Failed to connect to MusicFox" << std::endl;
        return false;
    }
    
    // 设置回调
    dbus_client_->setPropertyChangedCallback(
        [this](const LyricData& data) {
            this->onLyricDataChanged(data);
        }
    );
    
    running_ = true;
    worker_thread_ = std::thread(&LyricFetcher::run, this);
    
    return true;
}

void LyricFetcher::stop() {
    running_ = false;
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    if (dbus_client_) {
        dbus_client_->disconnect();
    }
}

void LyricFetcher::run() {
    std::cout << "MusicFox Lyrics Backend Started" << std::endl;
    
    // 初始数据获取
    if (dbus_client_->isConnected()) {
        LyricData data = dbus_client_->getCurrentLyricData();
        outputLyricData(data);
    }
    
    // 主循环：定期检查连接状态
    while (running_) {
        if (!dbus_client_->isConnected()) {
            // 尝试重新连接
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (running_) {
                dbus_client_->connect();
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    std::cout << "MusicFox Lyrics Backend Stopped" << std::endl;
}

void LyricFetcher::onLyricDataChanged(const LyricData& data) {
    outputLyricData(data);
}

void LyricFetcher::outputLyricData(const LyricData& data) {
    json output = {
        {"is_playing", data.is_playing},
        {"current_lyric", data.current_lyric},
        {"next_lyric", data.next_lyric},
        {"song_title", data.song_title},
        {"artist", data.artist},
        {"progress_ms", data.progress_ms}
    };
    
    std::cout << output.dump() << std::endl;
    std::cout.flush();
}