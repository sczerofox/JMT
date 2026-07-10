#pragma once
#include <string>
#include <windows.h>

class FileLock {
public:
        explicit FileLock(const std::wstring& filePath);
        ~FileLock();
        bool tryLock();
        void unlock();
        bool writeAllText(const std::wstring& content);   // 务必放在 public

    private:
        HANDLE hFile_;
        bool locked_;
};