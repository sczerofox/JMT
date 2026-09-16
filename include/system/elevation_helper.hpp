#pragma once
#include <string>

class ElevationHelper {
public:
    static bool IsElevated();
    static bool RelaunchElevated(const std::wstring& commandLine);
};