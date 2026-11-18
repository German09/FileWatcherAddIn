#include "EventQueue.h"

#include "../util/FileText.h"
#include "../util/JsonEscape.h"

#include <utility>
#include <vector>

namespace fw {

namespace {

const char* ActionToString(WatchAction action)
{
    switch (action) {
    case WatchAction::Created:
        return "created";
    case WatchAction::Deleted:
        return "deleted";
    case WatchAction::Modified:
        return "modified";
    case WatchAction::Renamed:
        return "renamed";
    case WatchAction::Rescan:
        return "rescan";
    }
    return "modified";
}

} // namespace

EventDispatcher::EventDispatcher()
{
    worker_ = std::thread([this] { WorkerLoop(); });
}

EventDispatcher::~EventDispatcher()
{
    Stop();
}

void EventDispatcher::SetEmit(EmitFn emit)
{
    std::lock_guard<std::mutex> lock(mutex_);
    emit_ = std::move(emit);
}

void EventDispatcher::SetMaxContentBytes(size_t value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    maxContentBytes_ = value == 0 ? 1024 * 1024 : value;
}

void EventDispatcher::SetDebounceMs(int ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    debounceMs_ = ms < 0 ? 0 : ms;
}

void EventDispatcher::Enqueue(const WatchEvent& event)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_)
            return;
        queue_.push(event);
    }
    cv_.notify_one();
}

void EventDispatcher::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_)
            return;
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void EventDispatcher::RememberKind(const std::string& path, bool isDirectory)
{
    kindCache_[path] = isDirectory;
}

bool EventDispatcher::LookupKind(const std::string& path, bool& isDirectory) const
{
    const auto it = kindCache_.find(path);
    if (it == kindCache_.end())
        return false;
    isDirectory = it->second;
    return true;
}

void EventDispatcher::FlushDebounced()
{
    // Caller must NOT hold mutex_. Collect under lock, emit outside.
    std::vector<WatchEvent> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pendingModified_.empty())
            return;
        const auto now = std::chrono::steady_clock::now();
        for (auto it = pendingModified_.begin(); it != pendingModified_.end();) {
            const auto atIt = modifiedAt_.find(it->first);
            if (atIt == modifiedAt_.end() ||
                now - atIt->second >= std::chrono::milliseconds(debounceMs_)) {
                ready.push_back(it->second);
                modifiedAt_.erase(it->first);
                it = pendingModified_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto& ev : ready)
        EmitEnriched(ev);
}

void EventDispatcher::EmitEnriched(const WatchEvent& event)
{
    WatchEvent ev = event;
    std::string path = ev.path;
    if (ev.action == WatchAction::Renamed) {
        path = ev.newPath.empty() ? ev.path : ev.newPath;
        ev.path = path;
    }

    size_t maxBytes = 0;
    EmitFn emitCopy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        maxBytes = ev.maxContentBytes > 0 ? ev.maxContentBytes : maxContentBytes_;
        emitCopy = emit_;
    }

    bool isDirectory = false;
    bool known = ev.isDirectoryKnown;
    if (known) {
        isDirectory = ev.isDirectory;
    } else {
        const ItemKind kind = DetectItemKind(path);
        if (kind == ItemKind::Directory) {
            isDirectory = true;
            known = true;
        } else if (kind == ItemKind::File) {
            isDirectory = false;
            known = true;
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            if (LookupKind(path, isDirectory) ||
                (!ev.oldPath.empty() && LookupKind(ev.oldPath, isDirectory))) {
                known = true;
            } else {
                isDirectory = false;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ev.action == WatchAction::Created || ev.action == WatchAction::Modified ||
            ev.action == WatchAction::Renamed) {
            RememberKind(path, isDirectory);
            if (ev.action == WatchAction::Renamed && !ev.oldPath.empty())
                kindCache_.erase(ev.oldPath);
        } else if (ev.action == WatchAction::Deleted) {
            kindCache_.erase(path);
        }
    }

    std::string content;
    bool truncated = false;
    bool skipped = false;
    std::string date;

    if (ev.action == WatchAction::Deleted) {
        date = FormatNowLocal();
    } else if (isDirectory) {
        date = FormatFileMtime(path);
    } else if (ev.action == WatchAction::Created || ev.action == WatchAction::Modified ||
               ev.action == WatchAction::Renamed) {
        date = FormatFileMtime(path);
        FileTextResult text = ReadFileText(path, maxBytes);
        content = std::move(text.text);
        truncated = text.truncated;
        skipped = text.skipped;
    } else {
        date = FormatNowLocal();
    }

    const std::string json = BuildEventJson(
        ActionToString(ev.action),
        isDirectory ? "directory" : "file",
        path,
        ev.oldPath,
        ev.newPath,
        date,
        content,
        truncated,
        skipped);

    if (emitCopy)
        emitCopy(ev.watchRoot, json);
}

void EventDispatcher::WorkerLoop()
{
    while (true) {
        WatchEvent event;
        bool hasEvent = false;
        bool stopping = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(25), [this] {
                return stop_ || !queue_.empty();
            });
            stopping = stop_ && queue_.empty();
            if (!queue_.empty()) {
                event = queue_.front();
                queue_.pop();
                hasEvent = true;
            }
        }

        FlushDebounced();

        if (stopping)
            break;

        if (!hasEvent)
            continue;

        if (event.action == WatchAction::Modified) {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingModified_[event.path] = event;
            modifiedAt_[event.path] = std::chrono::steady_clock::now();
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingModified_.erase(event.path);
            modifiedAt_.erase(event.path);
            if (event.action == WatchAction::Renamed) {
                pendingModified_.erase(event.oldPath);
                modifiedAt_.erase(event.oldPath);
                pendingModified_.erase(event.newPath);
                modifiedAt_.erase(event.newPath);
            }
        }

        EmitEnriched(event);
    }

    // Final flush of remaining debounced events.
    {
        std::vector<WatchEvent> remaining;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& kv : pendingModified_)
                remaining.push_back(kv.second);
            pendingModified_.clear();
            modifiedAt_.clear();
        }
        for (const auto& ev : remaining)
            EmitEnriched(ev);
    }
}

} // namespace fw
