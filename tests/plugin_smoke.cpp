#include <windows.h>
#include "PluginInterface.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    try {
        for (int run=0;run<5;++run) {
            auto module = LoadLibraryW(argv[1]);
            check(module != nullptr, "plugin DLL must load");
            auto factory = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module,"TMPluginGetInstance"));
            check(factory != nullptr,"export name must match official ABI");
            auto plugin = factory();
            check(plugin && plugin->GetAPIVersion()==7,"API must be 7");
            check(plugin==factory(),"factory must return singleton");
            check(!plugin->GetItem(-1) && !plugin->GetItem(2),"item bounds");
            check(plugin->GetItem(0) && plugin->GetItem(1),"two items");
            auto start = std::chrono::steady_clock::now();
            plugin->DataRequired();
            check(std::chrono::steady_clock::now()-start < std::chrono::milliseconds(500),"DataRequired blocks host");
            std::thread reader([&] {
                for(int j=0;j<1000;++j) for(int i=0;i<2;++i)
                    check(plugin->GetItem(i)->GetItemValueText()!=nullptr,"valid cached string");
            });
            for (int j=0;j<50;++j) { plugin->DataRequired(); check(plugin->GetTooltipInfo()!=nullptr,"valid tooltip"); }
            reader.join();
            if (argc>2 && run==0) {
                for (int j=0;j<300;++j) {
                    Sleep(100); plugin->DataRequired();
                    if(std::wstring(plugin->GetItem(0)->GetItemValueText())!=L"--" &&
                       std::wstring(plugin->GetItem(1)->GetItemValueText())!=L"--") break;
                }
                for (int i=0;i<2;++i) {
                    auto value=std::wstring(plugin->GetItem(i)->GetItemValueText());
                    check(value!=L"--","live integration requires real battery values");
                    if(std::wstring(argv[2])==L"--charging")
                        check(!value.empty() && value.back()==L'+',"live charging state must display plus");
                    std::cout << (i?"mouse=":"keyboard=");
                    for(auto c:value)std::cout << static_cast<char>(c);
                    std::cout << '\n';
                }
                if(std::wstring(argv[2])==L"--options")
                    check(plugin->ShowOptionsDialog(nullptr)==ITMPlugin::OR_OPTION_UNCHANGED,"options must close without changing settings");
            }
            FreeLibrary(module);
        }
        std::cout << "plugin ABI, concurrent getters and unload tests passed\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
