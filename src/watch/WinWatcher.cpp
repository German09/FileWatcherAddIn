#include "Watcher.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../util/FileText.h"
#include "../util/StringConv.h"

#include <atomic>
#include <thread>
#include <vector>

namespace fw {

class WinWatcher : public IWatcher
{
public:
    explicit WinWatcher(WatchEventCallback callback)
        : callback_(std::move(callback))
    {
    }

    ~WinWatcher() override
    {
        Stop();
    }

    bool Start(const std::string& path, bool recursive, std::string& error) override
    {
        Stop();
        root_ = path;
        recursive_ = recursive;

        const std::wstring wpath = Utf8ToWide(path);
        dirHandle_ = CreateFileW(
            wpath.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (dirHandle_ == INVALID_HANDLE_VALUE) {
            error = "CreateFileW failed, GetLastError=" + std::to_string(GetLastError());
            return false;
        }

        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent_) {
            error = "CreateEventW failed";
            CloseHandle(dirHandle_);
            dirHandle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        running_ = true;
        thread_ = std::thread([this] { ThreadMain(); });
        return true;
    }

    void Stop() override
    {
        running_ = false;
        if (stopEvent_)
            SetEvent(stopEvent_);
        if (thread_.joinable())
            thread_.join();
        if (dirHandle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(dirHandle_);
            dirHandle_ = INVALID_HANDLE_VALUE;
        }
        if (stopEvent_) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
    }

    bool IsRunning() const override
    {
        return running_;
    }

private:
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
        alignas(FILE_NOTIFY_INFORMATION) BYTE buffer[64 * 1024];
        OVERLAPPED overlapped{};
        HANDLE events[2];

        std::wstring pendingOldName;

        while (running_) {
            ZeroMemory(&overlapped, sizeof(overlapped));
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent)
                break;

            const BOOL ok = ReadDirectoryChangesW(
                dirHandle_,
                buffer,
                sizeof(buffer),
                recursive_ ? TRUE : FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                nullptr,
                &overlapped,
                nullptr);

            if (!ok) {
                CloseHandle(overlapped.hEvent);
                break;
            }

            events[0] = stopEvent_;
            events[1] = overlapped.hEvent;
            const DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) {
                CancelIoEx(dirHandle_, &overlapped);
                DWORD cancelledBytes = 0;
                GetOverlappedResult(dirHandle_, &overlapped, &cancelledBytes, TRUE);
                CloseHandle(overlapped.hEvent);
                break;
            }

            DWORD bytes = 0;
            const BOOL completed = GetOverlappedResult(dirHandle_, &overlapped, &bytes, FALSE);
            const DWORD completionError = completed ? ERROR_SUCCESS : GetLastError();
            CloseHandle(overlapped.hEvent);
            if (completionError == ERROR_NOTIFY_ENUM_DIR || (completed && bytes == 0)) {
                pendingOldName.clear();
                NotifyRescan();
                continue;
            }
            if (!completed)
                break;

            DWORD offset = 0;
            for (;;) {
                auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer + offset);
                const std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
                const std::string utf8Name = WideToUtf8(name.c_str());
                const std::string fullPath = JoinPath(root_, utf8Name);

                switch (info->Action) {
                case FILE_ACTION_ADDED: {
                    WatchEvent ev;
                    ev.action = WatchAction::Created;
                    ev.path = fullPath;
                    const ItemKind kind = DetectItemKind(fullPath);
                    if (kind != ItemKind::Unknown) {
                        ev.isDirectoryKnown = true;
                        ev.isDirectory = (kind == ItemKind::Directory);
                    }
                    if (callback_)
                        callback_(ev);
                    break;
                }
                case FILE_ACTION_REMOVED: {
                    WatchEvent ev;
                    ev.action = WatchAction::Deleted;
                    ev.path = fullPath;
                    if (callback_)
                        callback_(ev);
                    break;
                }
                case FILE_ACTION_MODIFIED: {
                    WatchEvent ev;
                    ev.action = WatchAction::Modified;
                    ev.path = fullPath;
                    const ItemKind kind = DetectItemKind(fullPath);
                    if (kind != ItemKind::Unknown) {
                        ev.isDirectoryKnown = true;
                        ev.isDirectory = (kind == ItemKind::Directory);
                    }
                    if (callback_)
                        callback_(ev);
                    break;
                }
                case FILE_ACTION_RENAMED_OLD_NAME:
                    pendingOldName = name;
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME: {
                    WatchEvent ev;
                    ev.action = WatchAction::Renamed;
                    ev.oldPath = JoinPath(root_, WideToUtf8(pendingOldName.c_str()));
                    ev.newPath = fullPath;
                    ev.path = fullPath;
                    const ItemKind kind = DetectItemKind(fullPath);
                    if (kind != ItemKind::Unknown) {
                        ev.isDirectoryKnown = true;
                        ev.isDirectory = (kind == ItemKind::Directory);
                    }
                    pendingOldName.clear();
                    if (callback_)
                        callback_(ev);
                    break;
                }
                default:
                    break;
                }

                if (info->NextEntryOffset == 0)
                    break;
                offset += info->NextEntryOffset;
            }
        }
        if (running_.exchange(false))
            NotifyRescan();
    }

    WatchEventCallback callback_;
    std::string root_;
    bool recursive_ = false;
    std::atomic<bool> running_{false};
    HANDLE dirHandle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
};

std::unique_ptr<IWatcher> CreateWatcher(WatchEventCallback callback)
{
    return std::make_unique<WinWatcher>(std::move(callback));
}

} // namespace fw

#endif // _WIN32
