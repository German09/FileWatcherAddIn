#pragma once

#include "Watcher.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

namespace fw {

class EventDispatcher
{
public:
    using EmitFn = std::function<void(const std::string& watchRoot, const std::string& json)>;

    EventDispatcher();
    ~EventDispatcher();

    void SetEmit(EmitFn emit);
    void SetMaxContentBytes(size_t value);
    void SetDebounceMs(int ms);

    void Enqueue(const WatchEvent& event);
    void Stop();

private:
    void WorkerLoop();
    void FlushDebounced();
    void EmitEnriched(const WatchEvent& event);
    void RememberKind(const std::string& path, bool isDirectory);
    bool LookupKind(const std::string& path, bool& isDirectory) const;

    EmitFn emit_;
    size_t maxContentBytes_ = 1024 * 1024;
    int debounceMs_ = 100;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<WatchEvent> queue_;
    std::unordered_map<std::string, WatchEvent> pendingModified_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> modifiedAt_;
    std::unordered_map<std::string, bool> kindCache_;
    bool stop_ = false;
    std::thread worker_;
};

} // namespace fw
