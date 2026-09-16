#include "system/file_lock.hpp"

FileLock::FileLock(const std::wstring& filePath) : hFile_(INVALID_HANDLE_VALUE), locked_(false) {
    hFile_ = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

FileLock::~FileLock() {
    if (locked_) unlock();
    if (hFile_ != INVALID_HANDLE_VALUE) CloseHandle(hFile_);
}

bool FileLock::tryLock() {
    if (hFile_ == INVALID_HANDLE_VALUE) return false;
    if (locked_) return true;
    OVERLAPPED ov = {0};
    if (LockFileEx(hFile_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        locked_ = true;
        return true;
    }
    return false;
}

void FileLock::unlock() {
    if (hFile_ != INVALID_HANDLE_VALUE && locked_) {
        OVERLAPPED ov = {0};
        UnlockFileEx(hFile_, 0, 1, 0, &ov);
        locked_ = false;
    }
}

bool FileLock::writeAllText(const std::wstring& content) {
    if (hFile_ == INVALID_HANDLE_VALUE) return false;
    if (!locked_) return false;  // 未加锁时不允许写入

    // 将文件指针移到开头并截断文件
    if (SetFilePointer(hFile_, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return false;
    if (!SetEndOfFile(hFile_)) return false;

    DWORD written = 0;
    auto toWrite = static_cast<DWORD>(content.size() * sizeof(wchar_t));
    BOOL ok = WriteFile(hFile_, content.c_str(), toWrite, &written, nullptr);
    return ok && (written == toWrite);
}