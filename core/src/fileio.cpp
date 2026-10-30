#include "samn/fileio.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace samn {

namespace fs = std::filesystem;

void WriteFileAtomic(const fs::path& target, const std::string& data) {
    fs::path dir = target.parent_path();
    if (dir.empty()) dir = ".";
#ifdef _WIN32
    static std::atomic<unsigned> counter{0};
    const fs::path tmp =
        dir / (L"." + target.filename().wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) +
               L"-" + std::to_wstring(counter++));
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create a temp file next to " + target.u8string());
    bool ok = true;
    size_t off = 0;
    while (ok && off < data.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - off, 1u << 20));
        ok = WriteFile(h, data.data() + off, chunk, &written, nullptr) && written > 0;
        off += written;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        throw std::runtime_error("cannot write " + target.u8string());
    }
#else
    std::string tpl = (dir / ("." + target.filename().string() + ".tmp-XXXXXX")).string();
    std::vector<char> buf(tpl.begin(), tpl.end());
    buf.push_back('\0');
    const int fd = mkstemp(buf.data());
    if (fd < 0) throw std::runtime_error("cannot create a temp file next to " + target.u8string());
    const std::string tmp(buf.data());
    bool ok = true;
    size_t off = 0;
    while (ok && off < data.size()) {
        const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
        } else {
            off += static_cast<size_t>(n);
        }
    }
    ok = ok && ::fsync(fd) == 0;
    if (ok) {
        const mode_t um = ::umask(0);
        ::umask(um);
        ok = ::fchmod(fd, static_cast<mode_t>(0644 & ~um)) == 0;
    }
    ::close(fd);
    if (!ok || ::rename(tmp.c_str(), target.c_str()) != 0) {
        ::unlink(tmp.c_str());
        throw std::runtime_error("cannot write " + target.u8string());
    }
#endif
}

}  // namespace samn
