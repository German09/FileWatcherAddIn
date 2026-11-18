#pragma once

#include <functional>
#include <memory>
#include <string>

namespace fw {

enum class WatchAction {
    Created,
    Deleted,
    Modified,
    Renamed,
    Rescan
};

struct WatchEvent
{
    WatchAction action = WatchAction::Modified;
    std::string path;
    std::string oldPath;
    std::string newPath;
    std::string watchRoot;
    size_t maxContentBytes = 1024 * 1024;
    bool isDirectory = false;
    bool isDirectoryKnown = false;
};

using WatchEventCallback = std::function<void(const WatchEvent&)>;

class IWatcher
{
public:
    virtual ~IWatcher() = default;
    virtual bool Start(const std::string& path, bool recursive, std::string& error) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
};

std::unique_ptr<IWatcher> CreateWatcher(WatchEventCallback callback);

} // namespace fw
