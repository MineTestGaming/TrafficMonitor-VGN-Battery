#include <windows.h>
#include <commctrl.h>
#include "PluginInterface.h"
#include "state.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace fs = std::filesystem;
namespace {
HMODULE self_module{};
struct Device : BatteryState {
    std::wstring name, status = L"等待首次采集", source, details;
};
class Plugin;
class Item final : public IPluginItem {
    Plugin& owner; int index;
public:
    Item(Plugin& plugin, int i):owner(plugin),index(i){}
    const wchar_t* GetItemName() const override { return index?L"VXE R1SE 鼠标电量":L"VGN N75 PRO 键盘电量"; }
    const wchar_t* GetItemId() const override { return index?L"VgnVxeR1Battery":L"VgnVxeN75Battery"; }
    const wchar_t* GetItemLableText() const override { return index?L"鼠标: ":L"键盘: "; }
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override { return L"100%"; }
};
class Plugin final : public ITMPlugin {
    std::array<Item,2> items{Item(*this,0),Item(*this,1)};
    std::array<Device,2> devices;
    std::mutex data_mutex, acquisition_mutex;
    std::atomic_bool refresh_requested{false};
    HANDLE process{}, job{};
    ULONGLONG started{}, last_attempt{};
    fs::path snapshot;
    std::wstring collector_status = L"正在准备采集";
    static constexpr int refresh_button = 1001;
    void request_refresh() noexcept { refresh_requested.store(true); }
    std::wstring options_summary() {
        std::lock_guard lock(data_mutex);
        auto now=GetTickCount64();
        return L"键盘: "+value_text(devices[0],now)+L"    鼠标: "+value_text(devices[1],now)+
            L"\n"+(refresh_requested.load()?L"已请求刷新，等待采集":collector_status)+
            L"\n自动刷新间隔 30 秒，也可点击“立即刷新”。";
    }
    static HRESULT CALLBACK options_callback(HWND hwnd,UINT notification,WPARAM button,LPARAM,LONG_PTR context) noexcept {
        auto plugin=reinterpret_cast<Plugin*>(context);
        try {
            if(notification==TDN_BUTTON_CLICKED && button==refresh_button) {
                plugin->request_refresh();
                auto text=plugin->options_summary();
                SendMessageW(hwnd,TDM_SET_ELEMENT_TEXT,TDE_CONTENT,reinterpret_cast<LPARAM>(text.c_str()));
                return S_FALSE; // Refresh keeps the options dialog open.
            }
            if(notification==TDN_TIMER) {
                // Only pump the same nonblocking poll used by the host. No device
                // I/O runs here; this also permits refresh in a standalone tester.
                plugin->DataRequired();
                auto summary=plugin->options_summary();
                auto details=std::wstring(plugin->GetTooltipInfo());
                SendMessageW(hwnd,TDM_UPDATE_ELEMENT_TEXT,TDE_CONTENT,reinterpret_cast<LPARAM>(summary.c_str()));
                SendMessageW(hwnd,TDM_UPDATE_ELEMENT_TEXT,TDE_EXPANDED_INFORMATION,reinterpret_cast<LPARAM>(details.c_str()));
            }
        } catch (...) { /* Never let a C++ exception cross the Win32 callback. */ }
        return S_OK;
    }
    std::wstring get(const wchar_t* section, const wchar_t* key) {
        wchar_t value[2048]{};
        GetPrivateProfileStringW(section,key,L"",value,2048,snapshot.c_str()); return value;
    }
    void error(const std::wstring& text) { std::lock_guard lock(data_mutex); collector_status=text; }
    void stop_child() {
        if (job) { CloseHandle(job); job=nullptr; }
        if (process) { CloseHandle(process); process=nullptr; }
    }
    void consume() {
        if (get(L"snapshot",L"version")!=L"1") { error(L"采集器未生成有效快照"); return; }
        std::array<Device,2> next;
        for (int i=0;i<2;++i) {
            auto section=i?L"mouse":L"keyboard";
            auto& d=next[i];
            d.name=get(section,L"name"); d.status=get(section,L"status"); d.source=get(section,L"source");
            d.connected=get(section,L"connected")==L"1";
            d.charging=get(section,L"charging")==L"正在充电";
            try {
                d.percent=std::stoi(get(section,L"percent"));
                d.observed_ms=std::stoull(get(section,L"observed_ms"));
            } catch (...) { d.percent=-1; d.observed_ms=0; }
            for (const auto& field : std::array<std::pair<const wchar_t*,const wchar_t*>,14>{{
                {L"address",L"蓝牙地址"},{L"manufacturer",L"制造商"},{L"model",L"型号"},
                {L"serial",L"序列号"},{L"firmware",L"固件"},{L"hardware",L"硬件"},
                {L"software",L"软件"},{L"pnp",L"PnP"},{L"error",L"读取说明"},
                {L"charging",L"充电状态"},{L"voltage_raw",L"电压原始值（单位未确认）"},
                {L"report_rate_raw",L"回报率编码"},{L"dpi_stage",L"DPI 档位编码"},{L"report_rate",L"回报率"}}}) {
                auto value=get(section,field.first);
                if(!value.empty()) d.details+=L"\n  "+std::wstring(field.second)+L": "+value;
            }
        }
        auto global_error=get(L"snapshot",L"error");
        std::lock_guard lock(data_mutex);
        devices=std::move(next); collector_status=global_error.empty()?L"设备采集完成":L"采集错误: "+global_error;
    }
    void launch(ULONGLONG now) {
        last_attempt=now;
        wchar_t module_path[32768]{};
        if (!GetModuleFileNameW(self_module,module_path,32768)) {error(L"无法定位插件目录");return;}
        auto helper=fs::path(module_path).parent_path()/L"BatteryCollector.exe";
        if (snapshot.empty()) {
            wchar_t temp_dir[MAX_PATH]{}, temp_path[MAX_PATH]{};
            if (!GetTempPathW(MAX_PATH,temp_dir) || !GetTempFileNameW(temp_dir,L"VGB",0,temp_path)) {
                error(L"无法创建临时快照"); return;
            }
            snapshot=temp_path;
        }
        DeleteFileW(snapshot.c_str());
        job=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
            stop_child(); error(L"无法创建采集隔离 Job"); return;
        }
        std::wstring command=L"\""+helper.wstring()+L"\" --output \""+snapshot.wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup); PROCESS_INFORMATION info{};
        if (!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,
            CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,helper.parent_path().c_str(),&startup,&info)) {
            auto code=GetLastError();stop_child();error(L"无法启动 BatteryCollector.exe，错误 "+std::to_wstring(code));return;
        }
        process=info.hProcess;
        if (!AssignProcessToJobObject(job,process) || ResumeThread(info.hThread)==static_cast<DWORD>(-1)) {
            TerminateProcess(process,1);CloseHandle(info.hThread);stop_child();error(L"无法隔离采集器");return;
        }
        CloseHandle(info.hThread);started=now;error(L"设备读取中（保留有效缓存）");
    }
public:
    ~Plugin() {
        // No DLL workers/callbacks exist. Closing the Job terminates only our collector.
        stop_child();
        if (!snapshot.empty()) {DeleteFileW(snapshot.c_str());DeleteFileW((snapshot.wstring()+L".tmp").c_str());}
    }
    IPluginItem* GetItem(int i) override { return i>=0&&i<2?&items[i]:nullptr; }
    void DataRequired() override {
        try {
            std::lock_guard acquisition_lock(acquisition_mutex);
            auto now=GetTickCount64();
            if (process) {
                if (WaitForSingleObject(process,0)==WAIT_OBJECT_0) { consume();stop_child(); }
                else if (now-started>=30000) { stop_child();consume();error(L"采集超时，保留已完成的结果并等待重试"); }
                else return;
                if(!refresh_requested.load()) return;
            }
            if (refresh_requested.exchange(false) || !last_attempt || now-last_attempt>=30000) launch(now);
        } catch (...) { error(L"插件采集错误，将自动重试"); }
    }
    std::wstring value(int i) { std::lock_guard lock(data_mutex);return value_text(devices[i],GetTickCount64()); }
    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch(index) {
        case TMI_NAME:return L"VGN / VXE Battery";
        case TMI_DESCRIPTION:return L"直接读取 2.4G HID / BLE 键鼠电量，独立采集，区分未知、断连和过期数据。";
        case TMI_AUTHOR:return L"TrafficMonitor-Battery";
        case TMI_COPYRIGHT:return L"2026 TrafficMonitor-Battery contributors";
        case TMI_VERSION:return L"0.1.2";
        case TMI_URL:return L"";
        default:return L"";
        }
    }
    const wchar_t* GetTooltipInfo() override {
        thread_local std::wstring text;
        std::lock_guard lock(data_mutex);auto now=GetTickCount64();
        text=L"VGN / VXE Battery\n"+collector_status;
        if(refresh_requested.load())text+=L"\n已请求手动刷新";
        for(int i=0;i<2;++i) {
            const auto& d=devices[i];
            text+=L"\n"+(d.name.empty()?std::wstring(i?L"VXE R1SE":L"VGN N75 PRO"):d.name)+L": "+value_text(d,now);
            text+=L"\n  "+d.status+L" | 来源: "+d.source;
            if(d.observed_ms && now>=d.observed_ms) text+=L" | "+std::to_wstring((now-d.observed_ms)/1000)+L" 秒前";
            text+=d.details;
        }
        text+=L"\n刷新 30 秒；有效期 120 秒。仅显示设备实际返回的字段。";
        return text.c_str();
    }
    OptionReturn ShowOptionsDialog(void* parent) override {
        auto summary=options_summary();
        auto details=std::wstring(GetTooltipInfo());
        TASKDIALOG_BUTTON buttons[]{{refresh_button,L"立即刷新"}};
        TASKDIALOGCONFIG config{};config.cbSize=sizeof(config);
        config.hwndParent=static_cast<HWND>(parent);
        config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_CALLBACK_TIMER;
        config.dwCommonButtons=TDCBF_CLOSE_BUTTON;
        config.pszWindowTitle=L"VGN / VXE Battery 设置";
        config.pszMainInstruction=L"设备电量";
        config.pszContent=summary.c_str();
        config.pszExpandedInformation=details.c_str();
        config.pszExpandedControlText=L"显示设备详情";
        config.pszCollapsedControlText=L"收起设备详情";
        config.cButtons=1;config.pButtons=buttons;config.nDefaultButton=IDCLOSE;
        config.pfCallback=options_callback;config.lpCallbackData=reinterpret_cast<LONG_PTR>(this);
        config.cxWidth=360;
        // Resolve at runtime: hosts without the v6 common-controls activation
        // context must still be able to load the plugin and use its menu command.
        using ShowTaskDialog=HRESULT (WINAPI*)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
        auto controls=LoadLibraryW(L"comctl32.dll");
        auto show=controls?reinterpret_cast<ShowTaskDialog>(GetProcAddress(controls,"TaskDialogIndirect")):nullptr;
        auto result=show?show(&config,nullptr,nullptr,nullptr):E_NOTIMPL;
        if(controls)FreeLibrary(controls);
        if(FAILED(result)) {
            auto text=details+L"\n\n点击“重试”请求立即刷新。";
            if(MessageBoxW(config.hwndParent,text.c_str(),config.pszWindowTitle,MB_RETRYCANCEL|MB_ICONINFORMATION)==IDRETRY)
                request_refresh();
        }
        return OR_OPTION_UNCHANGED;
    }
    int GetCommandCount() override {return 1;}
    const wchar_t* GetCommandName(int index) override {return index==0?L"立即刷新":nullptr;}
    void OnPluginCommand(int index,void*,void*) override {if(index==0)request_refresh();}
};
const wchar_t* Item::GetItemValueText() const {
    thread_local std::array<std::wstring,2> values;
    values[index]=owner.value(index);return values[index].c_str();
}
}
extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() {static Plugin plugin;return &plugin;}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) self_module=module;
    return TRUE;
}
