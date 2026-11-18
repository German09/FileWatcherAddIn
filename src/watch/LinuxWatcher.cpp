#include "Watcher.h"

#if defined(__linux__)

#include "../util/FileText.h"

#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <map>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fw {

class LinuxWatcher : public IWatcher
{
public:
    explicit LinuxWatcher(WatchEventCallback callback)
        : callback_(std::move(callback))
    {
    }

    ~LinuxWatcher() override
    {
        Stop();
    }

    bool Start(const std::string& path, bool recursive, std::string& error) override
    {
        Stop();
        root_ = path;
        recursive_ = recursive;

        inotifyFd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (inotifyFd_ < 0) {
            error = std::string("inotify_init1 failed: ") + std::strerror(errno);
            return false;
        }

        if (pipe(stopPipe_) != 0) {
            error = std::string("pipe failed: ") + std::strerror(errno);
            close(inotifyFd_);
            inotifyFd_ = -1;
            return false;
        }

        if (!AddWatchRecursive(path, error)) {
            CleanupFds();
            return false;
        }

        running_ = true;
        thread_ = std::thread([this] { ThreadMain(); });
        return true;
    }

    void Stop() override
    {
        if (!running_ && inotifyFd_ < 0)
            return;
        running_ = false;
        if (stopPipe_[1] >= 0) {
            char b = 1;
            (void)!write(stopPipe_[1], &b, 1);
        }
        if (thread_.joinable())
            thread_.join();
        for (const auto& kv : wdToPath_)
            inotify_rm_watch(inotifyFd_, kv.first);
        wdToPath_.clear();
        pathToWd_.clear();
        CleanupFds();
    }

    bool IsRunning() const override
    {
        return running_;
    }

private:
    void CleanupFds()
    {
        if (inotifyFd_ >= 0) {
            close(inotifyFd_);
            inotifyFd_ = -1;
        }
        if (stopPipe_[0] >= 0) {
            close(stopPipe_[0]);
            stopPipe_[0] = -1;
        }
        if (stopPipe_[1] >= 0) {
            close(stopPipe_[1]);
            stopPipe_[1] = -1;
        }
    }

    bool AddWatch(const std::string& path, std::string& error)
    {
        const uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM |
                              IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_ONLYDIR;
        const int wd = inotify_add_watch(inotifyFd_, path.c_str(), mask);
        if (wd < 0) {
            error = std::string("inotify_add_watch failed for ") + path + ": " + std::strerror(errno);
            return false;
        }
        wdToPath_[wd] = path;
        pathToWd_[path] = wd;
        return true;
    }

    bool AddWatchRecursive(const std::string& path, std::string& error)
    {
        if (!AddWatch(path, error))
            return false;
        if (!recursive_)
            return true;

        DIR* dir = opendir(path.c_str());
        if (!dir)
            return true;
        while (dirent* ent = readdir(dir)) {
            if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0)
                continue;
            const std::string child = JoinPath(path, ent->d_name);
            struct stat st{};
            if (stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                if (!AddWatchRecursive(child, error)) {
                    closedir(dir);
                    return false;
                }
            }
        }
        closedir(dir);
        return true;
    }

    void NotifyRescan()
    {
        WatchEvent event;
        event.action = WatchAction::Rescan;
        event.path = root_;
        event.isDirectoryKnown = true;
        event.isDirectory = true;
        callback_(event);
    }

    void ThreadMain()
    {
        std::unordered_map<uint32_t, std::pair<std::string, std::string>> pendingMoves;
        // cookie -> (dir, name)

        alignas(struct inotify_event) char buffer[64 * 1024];

        while (running_) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(inotifyFd_, &fds);
            FD_SET(stopPipe_[0], &fds);
            const int maxFd = std::max(inotifyFd_, stopPipe_[0]);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            const int ready = select(maxFd + 1, &fds, nullptr, nullptr, &tv);
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (!running_)
                break;
            if (FD_ISSET(stopPipe_[0], &fds))
                break;

            // Unmatched MOVED_FROM without MOVED_TO => deleted / moved out.
            if (!pendingMoves.empty() && (!FD_ISSET(inotifyFd_, &fds) || ready == 0)) {
                for (const auto& kv : pendingMoves) {
                    WatchEvent ev;
                    ev.action = WatchAction::Deleted;
                    ev.path = JoinPath(kv.second.first, kv.second.second);
                    if (callback_)
                        callback_(ev);
                }
                pendingMoves.clear();
            }

            if (!FD_ISSET(inotifyFd_, &fds))
                continue;

            for (;;) {
                const ssize_t len = read(inotifyFd_, buffer, sizeof(buffer));
                if (len < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    if (errno == EINTR)
                        continue;
                    if (running_.exchange(false))
                        NotifyRescan();
                    return;
                }
                if (len == 0)
                    break;

                ssize_t offset = 0;
                while (offset < len) {
                    auto* event = reinterpret_cast<struct inotify_event*>(buffer + offset);
                    offset += static_cast<ssize_t>(sizeof(struct inotify_event) + event->len);

                    if (event->mask & IN_Q_OVERFLOW) {
                        pendingMoves.clear();
                        if (running_.exchange(false))
                            NotifyRescan();
                        return;
                    }
                    auto wdIt = wdToPath_.find(event->wd);
                    if (wdIt == wdToPath_.end())
                        continue;
                    const std::string& dir = wdIt->second;
                    const std::string name = event->len > 0 ? std::string(event->name) : std::string();
                    const std::string fullPath = name.empty() ? dir : JoinPath(dir, name);
                    const bool isDir = (event->mask & IN_ISDIR) != 0;

                    if (event->mask & IN_CREATE) {
                        if (recursive_ && isDir) {
                            std::string err;
                            AddWatchRecursive(fullPath, err);
                        }
                        WatchEvent ev;
                        ev.action = WatchAction::Created;
                        ev.path = fullPath;
                        ev.isDirectoryKnown = true;
                        ev.isDirectory = isDir;
                        if (callback_)
                            callback_(ev);
                    }
                    if (event->mask & IN_DELETE) {
                        if (isDir) {
                            auto pit = pathToWd_.find(fullPath);
                            if (pit != pathToWd_.end()) {
                                inotify_rm_watch(inotifyFd_, pit->second);
                                wdToPath_.erase(pit->second);
                                pathToWd_.erase(pit);
                            }
                        }
                        WatchEvent ev;
                        ev.action = WatchAction::Deleted;
                        ev.path = fullPath;
                        ev.isDirectoryKnown = true;
                        ev.isDirectory = isDir;
                        if (callback_)
                            callback_(ev);
                    }
                    if ((event->mask & IN_MODIFY) || (event->mask & IN_CLOSE_WRITE) ||
                        (event->mask & IN_ATTRIB)) {
                        WatchEvent ev;
                        ev.action = WatchAction::Modified;
                        ev.path = fullPath;
                        ev.isDirectoryKnown = true;
                        ev.isDirectory = isDir;
                        if (callback_)
                            callback_(ev);
                    }
                    if (event->mask & IN_MOVED_FROM) {
                        pendingMoves[event->cookie] = {dir, name};
                    }
                    if (event->mask & IN_MOVED_TO) {
                        auto it = pendingMoves.find(event->cookie);
                        if (it != pendingMoves.end()) {
                            WatchEvent ev;
                            ev.action = WatchAction::Renamed;
                            ev.oldPath = JoinPath(it->second.first, it->second.second);
                            ev.newPath = fullPath;
                            ev.path = fullPath;
                            ev.isDirectoryKnown = true;
                            ev.isDirectory = isDir;
                            pendingMoves.erase(it);
                            if (recursive_ && isDir) {
                                auto pit = pathToWd_.find(ev.oldPath);
                                if (pit != pathToWd_.end()) {
                                    inotify_rm_watch(inotifyFd_, pit->second);
                                    wdToPath_.erase(pit->second);
                                    pathToWd_.erase(pit);
                                }
                                std::string err;
                                AddWatchRecursive(fullPath, err);
                            }
                            if (callback_)
                                callback_(ev);
                        } else {
                            if (recursive_ && isDir) {
                                std::string err;
                                AddWatchRecursive(fullPath, err);
                            }
                            WatchEvent ev;
                            ev.action = WatchAction::Created;
                            ev.path = fullPath;
                            ev.isDirectoryKnown = true;
                            ev.isDirectory = isDir;
                            if (callback_)
                                callback_(ev);
                        }
                    }
                }
            }
        }

        if (running_.exchange(false))
            NotifyRescan();
        for (const auto& kv : pendingMoves) {
            WatchEvent ev;
            ev.action = WatchAction::Deleted;
            ev.path = JoinPath(kv.second.first, kv.second.second);
            if (callback_)
                callback_(ev);
        }
    }

    WatchEventCallback callback_;
    std::string root_;
    bool recursive_ = false;
    std::atomic<bool> running_{false};
    int inotifyFd_ = -1;
    int stopPipe_[2] = {-1, -1};
    std::unordered_map<int, std::string> wdToPath_;
    std::unordered_map<std::string, int> pathToWd_;
    std::thread thread_;
};

std::unique_ptr<IWatcher> CreateWatcher(WatchEventCallback callback)
{
    return std::make_unique<LinuxWatcher>(std::move(callback));
}

} // namespace fw

#elif !defined(_WIN32)

namespace fw {

class StubWatcher : public IWatcher
{
public:
    explicit StubWatcher(WatchEventCallback) {}
    bool Start(const std::string&, bool, std::string& error) override
    {
        error = "FileWatcher is supported only on Windows and Linux";
        return false;
    }
    void Stop() override {}
    bool IsRunning() const override { return false; }
};

std::unique_ptr<IWatcher> CreateWatcher(WatchEventCallback callback)
{
    return std::make_unique<StubWatcher>(std::move(callback));
}

} // namespace fw

#endif
