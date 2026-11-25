#include <windows.h>
#include <string>

int wmain(int argc,wchar_t** argv) {
    if(argc!=3 || std::wstring(argv[1])!=L"--output")return 2;
    // A deterministic collector fixture makes each completed acquisition visible.
    wchar_t counter_path[MAX_PATH]{};
    if(!GetEnvironmentVariableW(L"VGN_VXE_TEST_COUNTER_PATH",counter_path,MAX_PATH))return 3;
    std::wstring output=argv[2],counter=counter_path;
    auto n=GetPrivateProfileIntW(L"test",L"count",0,counter.c_str())+1;
    auto count=std::to_wstring(n);
    WritePrivateProfileStringW(L"test",L"count",count.c_str(),counter.c_str());
    Sleep(250);
    WritePrivateProfileStringW(L"snapshot",L"version",L"1",output.c_str());
    auto tick=std::to_wstring(GetTickCount64());
    for(auto section:{L"keyboard",L"mouse"}) {
        WritePrivateProfileStringW(section,L"connected",L"1",output.c_str());
        WritePrivateProfileStringW(section,L"percent",count.c_str(),output.c_str());
        WritePrivateProfileStringW(section,L"observed_ms",tick.c_str(),output.c_str());
    }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,output.c_str());
    return 0;
}
