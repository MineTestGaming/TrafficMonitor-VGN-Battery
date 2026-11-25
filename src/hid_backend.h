#pragma once
#include <array>
#include <string>
#include <vector>
#include <utility>
struct HidResult {
    bool found=false,connected=false;
    int percent=-1;
    unsigned long long observed=0;
    std::wstring name,status,error,source;
    std::vector<std::pair<std::wstring,std::wstring>> metadata;
};
std::array<HidResult,2> collect_hid();
