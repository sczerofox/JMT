#include "platform/elevator.hpp"

#include "system/elevation_helper.hpp"

bool WinElevator::isElevated() {
    return ElevationHelper::IsElevated();
}

bool WinElevator::relaunchElevated(const std::wstring& commandLine) {
    return ElevationHelper::RelaunchElevated(commandLine);
}
