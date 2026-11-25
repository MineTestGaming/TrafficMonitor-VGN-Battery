#include <windows.h>
#include "PluginInterface.h"
#include <iostream>
#include <stdexcept>
#include <string>

void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
std::wstring wait_for_change(ITMPlugin* plugin,const std::wstring& before) {
    auto deadline=GetTickCount64()+5000;
    while(GetTickCount64()<deadline) {
        plugin->DataRequired();auto value=std::wstring(plugin->GetItem(0)->GetItemValueText());
        if(value!=L"--" && value!=before)return value;
        Sleep(20);
    }
    throw std::runtime_error("manual refresh must complete before the 30 second interval");
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    wchar_t temp_dir[MAX_PATH]{},counter[MAX_PATH]{};
    try {
        check(GetTempPathW(MAX_PATH,temp_dir)!=0 && GetTempFileNameW(temp_dir,L"VRF",0,counter)!=0,"test counter path");
        check(SetEnvironmentVariableW(L"VGN_VXE_TEST_COUNTER_PATH",counter)!=FALSE,"fixture environment");
        auto module=LoadLibraryW(argv[1]);check(module!=nullptr,"plugin must load");
        auto factory=reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module,"TMPluginGetInstance"));
        check(factory!=nullptr,"factory must exist");auto plugin=factory();
        check(plugin->GetCommandCount()==1,"one refresh command must be exposed");
        check(std::wstring(plugin->GetCommandName(0))==L"立即刷新","refresh menu label");
        check(!plugin->GetCommandName(-1) && !plugin->GetCommandName(1),"menu index bounds");
        auto first=wait_for_change(plugin,L"--");
        auto begin=GetTickCount64();
        plugin->OnPluginCommand(-1,nullptr,nullptr);plugin->DataRequired();Sleep(50);plugin->DataRequired();
        check(std::wstring(plugin->GetItem(0)->GetItemValueText())==first,"invalid command must not refresh");
        plugin->OnPluginCommand(0,nullptr,nullptr);
        check(GetTickCount64()-begin<500,"menu command must not wait for device I/O");
        auto second=wait_for_change(plugin,first);
        // Queue repeated requests while the next acquisition is still in flight.
        plugin->OnPluginCommand(0,nullptr,nullptr);plugin->DataRequired();
        for(int i=0;i<20;++i)plugin->OnPluginCommand(0,nullptr,nullptr);
        auto third=wait_for_change(plugin,second);
        auto fourth=wait_for_change(plugin,third);
        auto after=GetTickCount64()+700;
        while(GetTickCount64()<after){plugin->DataRequired();Sleep(20);}
        check(std::wstring(plugin->GetItem(0)->GetItemValueText())==fourth,"in-flight requests must coalesce to one additional acquisition");
        FreeLibrary(module);
        DeleteFileW(counter);
        std::cout << "manual command, early refresh, nonblocking dispatch and coalescing passed\n";
    }catch(const std::exception& e){DeleteFileW(counter);std::cerr << e.what() << '\n';return 1;}
}
