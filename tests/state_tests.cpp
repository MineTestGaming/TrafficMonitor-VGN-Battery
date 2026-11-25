#include "state.h"
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
int main() {
    try {
        BatteryState s;
        check(value_text(s, 1000) == L"--", "unknown must not be zero");
        s.connected = true; s.percent = 0; s.observed_ms = 1000;
        check(value_text(s, 1000) == L"0%", "empty battery is valid");
        s.percent = 100;
        check(value_text(s, 1000) == L"100%", "full battery is valid");
        s.percent = 101;
        check(value_text(s, 1000) == L"--", "reject invalid percentage");
        s.percent = 81; s.connected = false;
        check(value_text(s, 1000) == L"--", "disconnected battery is not current");
        s.connected = true;
        check(value_text(s, 121001) == L"--", "expire stale observations");
        check(value_text(s, 900) == L"--", "reject future observation");
        check(value_text(s, 1001) == L"81%", "valid current observation");
        s.charging = true; s.percent = 59;
        check(value_text(s, 1001) == L"59+", "charging replaces percent sign");
        s.percent = 30;
        check(value_text(s, 1001) == L"30+", "mouse charging uses same marker");
        s.percent = 0;
        check(value_text(s, 1001) == L"0+", "empty charging battery is valid");
        s.percent = 100;
        check(value_text(s, 1001) == L"100+", "full battery preserves reported charging state");
        check(value_text(s, 121001) == L"--", "charging does not make stale data valid");
        s.connected = false;
        check(value_text(s, 1001) == L"--", "charging does not make disconnected data valid");
        s.connected = true; s.charging = false; s.percent = 59;
        check(value_text(s, 1001) == L"59%", "restore percentage when charging stops");
        std::cout << "state tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
