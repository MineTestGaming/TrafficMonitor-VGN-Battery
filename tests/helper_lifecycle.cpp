#include <windows.h>
#include <tlhelp32.h>
#include "PluginInterface.h"
#include <iostream>
#include <stdexcept>

void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
HANDLE child_handle() {
    auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);HANDLE found{};
    if(Process32FirstW(snapshot,&entry))do {
        if(entry.th32ParentProcessID==GetCurrentProcessId() && !_wcsicmp(entry.szExeFile,L"BatteryCollector.exe")) {
            found=OpenProcess(SYNCHRONIZE,FALSE,entry.th32ProcessID);break;
        }
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    try {
        for(int run=0;run<2;++run) {
            auto module=LoadLibraryW(argv[1]);check(module!=nullptr,"fixture DLL failed to load");
            auto factory=reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module,"TMPluginGetInstance"));
            check(factory!=nullptr,"factory export missing");auto plugin=factory();
            plugin->DataRequired();auto child=child_handle();check(child!=nullptr,"collector must start before lifecycle test");
            if(run==0) {
                auto start=GetTickCount64();
                while(WaitForSingleObject(child,0)!=WAIT_OBJECT_0 && GetTickCount64()-start<35000) {
                    auto before=GetTickCount64();plugin->DataRequired();
                    check(GetTickCount64()-before<500,"poll must not wait for hung collector");Sleep(50);
                }
                check(WaitForSingleObject(child,2000)==WAIT_OBJECT_0,"hung child must be terminated");
                check(GetTickCount64()-start>=29500,"fixture must exercise real 30 second timeout");
                check(std::wstring(plugin->GetTooltipInfo()).find(L"超时")!=std::wstring::npos,"timeout must be visible");
            }
            FreeLibrary(module);
            check(WaitForSingleObject(child,2000)==WAIT_OBJECT_0,"DLL unload must terminate its child");CloseHandle(child);
        }
        std::cout << "30 second hung-helper timeout and unload child termination passed\n";
    }catch(const std::exception& e){std::cerr << e.what() << '\n';return 1;}
}
