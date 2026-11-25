#pragma once
#include <cstdint>
#include <string>

struct BatteryState {
    bool connected = false;
    bool charging = false;
    int percent = -1;
    std::uint64_t observed_ms = 0;
};
inline std::wstring value_text(const BatteryState& s, std::uint64_t now_ms) {
    if (!s.connected || s.percent < 0 || s.percent > 100 || !s.observed_ms ||
        now_ms < s.observed_ms || now_ms - s.observed_ms > 120000)
        return L"--";
    return std::to_wstring(s.percent) + (s.charging ? L"+" : L"%");
}
