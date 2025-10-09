
#include "lyric_fetcher.h"
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> should_exit(false);

void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    should_exit = true;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "Starting MusicFox Lyrics Backend..." << std::endl;
    
    LyricFetcher fetcher;
    
    if (!fetcher.start()) {
        std::cerr << "Failed to start lyric fetcher" << std::endl;
        return 1;
    }
    
    // 等待退出信号
    while (!should_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    fetcher.stop();
    return 0;
}