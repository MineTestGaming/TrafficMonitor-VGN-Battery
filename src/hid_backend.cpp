#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "hid_backend.h"
#include "hid_protocol.h"
#include <algorithm>
#include <stdexcept>

namespace {
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle):h(handle){}
    ~Handle(){if(h!=INVALID_HANDLE_VALUE && h)CloseHandle(h);}
    Handle(const Handle&)=delete;
};
std::wstring hex(const Bytes& b) {
    std::wstring out;for(auto v:b){wchar_t buf[4];swprintf_s(buf,L"%02X ",v);out+=buf;}return out;
}
bool io(HANDLE h, bool write, Bytes& bytes, DWORD timeout) {
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED ov{};ov.hEvent=event.h;
    DWORD count{};
    BOOL ok=write?WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&count,&ov):
        ReadFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&count,&ov);
    if(!ok && GetLastError()!=ERROR_IO_PENDING)return false;
    if(!ok && WaitForSingleObject(event.h,timeout)!=WAIT_OBJECT_0) {
        CancelIoEx(h,&ov);GetOverlappedResult(h,&ov,&count,TRUE);return false;
    }
    if(!GetOverlappedResult(h,&ov,&count,FALSE))return false;
    if(write)return count==bytes.size();
    bytes.resize(count);return true;
}
Bytes exchange(HANDLE h, const HIDP_CAPS& caps, Bytes request, int kind, unsigned short seq=0) {
    if(caps.OutputReportByteLength < request.size())throw std::runtime_error("unexpected HID output report length");
    request.resize(caps.OutputReportByteLength,0);
    HidD_FlushQueue(h);
    // Reads are on a vendor collection; never consume keyboard/mouse input collections.
    if(!io(h,true,request,500))throw std::runtime_error("HID query write failed");
    auto until=GetTickCount64()+1500;
    while(GetTickCount64()<until) {
        Bytes response(caps.InputReportByteLength);
        auto remaining=static_cast<DWORD>(until-GetTickCount64());
        if(!io(h,false,response,std::min<DWORD>(remaining,1500)))break;
        if(kind==0 && response.size()>=10 && response[0]==4 && response[1]==(seq&255) && response[2]==(seq>>8) && response[3]==request[3])return response;
        if(kind==1 && compx_reply_valid(response,request[1]))return response;
    }
    throw std::runtime_error("HID query response timed out");
}
std::wstring product_string(HANDLE h, bool maker=false) {
    wchar_t buf[256]{};
    if(maker)HidD_GetManufacturerString(h,buf,sizeof(buf));else HidD_GetProductString(h,buf,sizeof(buf));
    return buf;
}
}
std::array<HidResult,2> collect_hid() {
    std::array<HidResult,2> results;
    GUID guid{};HidD_GetHidGuid(&guid);
    auto set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE)return results;
    for(DWORD index=0;;++index) {
        SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
        if(!SetupDiEnumDeviceInterfaces(set,nullptr,&guid,index,&iface))break;
        DWORD size{};SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&size,nullptr);
        if(!size)continue;
        std::vector<unsigned char> buffer(size);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,size,nullptr,nullptr))continue;
        Handle probe(CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        if(probe.h==INVALID_HANDLE_VALUE)continue;
        HIDD_ATTRIBUTES attributes{};attributes.Size=sizeof(attributes);
        if(!HidD_GetAttributes(probe.h,&attributes))continue;
        int kind=attributes.VendorID==0x320f && attributes.ProductID==0x5088?0:
            (attributes.VendorID==0x3554 && attributes.ProductID==0xf58e?1:-1);
        if(kind<0 || results[kind].connected)continue;
        PHIDP_PREPARSED_DATA preparsed{};HIDP_CAPS caps{};
        if(!HidD_GetPreparsedData(probe.h,&preparsed))continue;
        auto status=HidP_GetCaps(preparsed,&caps);HidD_FreePreparsedData(preparsed);
        // WebHID groups a physical HID interface's collections. Win32 opens a
        // single collection: this receiver's report 8 is on FF02, not the
        // feature-only FF04 collection used in the WebHID device filter.
        if(status!=HIDP_STATUS_SUCCESS || caps.UsagePage!=(kind?0xff02:0xff1c) || caps.Usage!=(kind?2:0x92))continue;
        auto name=product_string(probe.h);
        if(!kind && name.find(L"N75 PRO")==std::wstring::npos)continue;
        auto& r=results[kind];r.found=true;r.name=name;r.status=L"接收器存在，设备尚未回应";
        r.metadata.emplace_back(L"manufacturer",product_string(probe.h,true));
        wchar_t identity[128]{};swprintf_s(identity,L"VID=%04X PID=%04X revision=%04X, input=%u output=%u",attributes.VendorID,attributes.ProductID,attributes.VersionNumber,caps.InputReportByteLength,caps.OutputReportByteLength);
        r.metadata.emplace_back(L"pnp",identity);
        Handle device(CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
        if(device.h==INVALID_HANDLE_VALUE) {r.error=L"HID 打开失败 "+std::to_wstring(GetLastError());continue;}
        try {
            if(kind) {
                auto model=exchange(device.h,caps,compx_request(0x10),1);
                if(model[5]<2)throw std::runtime_error("short model reply");
                r.metadata.emplace_back(L"model",L"CID="+std::to_wstring(model[6])+L" MID="+std::to_wstring(model[7]));
                r.metadata.emplace_back(L"raw_model",hex(model));
                if(model[6]!=2 || (model[7]!=31 && model[7]!=32)) {r.status=L"接收器型号不匹配 R1 SE+";continue;}
                auto online=exchange(device.h,caps,compx_request(3),1);
                if(online[5]<1)throw std::runtime_error("short online reply");
                if(!online[6]){r.status=L"接收器在线，鼠标离线或休眠";continue;}
                auto battery=exchange(device.h,caps,compx_request(4),1);
                auto [percent,charging]=compx_battery(battery);
                if(percent<0)throw std::runtime_error("invalid battery reply");
                r.percent=percent;r.connected=true;r.observed=GetTickCount64();
                r.source=L"2.4G HID Compx 0x04";r.status=L"实时读取成功";
                r.metadata.emplace_back(L"charging",charging?L"正在充电":L"未充电");
                if(battery[5]>=4)r.metadata.emplace_back(L"voltage_raw",std::to_wstring((battery[8]<<8)|battery[9]));
                r.metadata.emplace_back(L"raw_battery",hex(battery));
                // Optional reads never invalidate the completed battery observation.
                try {
                    auto fw=exchange(device.h,caps,compx_request(0x12),1);
                    if(fw[5]<2)throw std::runtime_error("short firmware reply");
                    r.metadata.emplace_back(L"firmware",hex(Bytes{fw[6],fw[7]}));
                    auto dpi=exchange(device.h,caps,compx_request(8,0,6),1);
                    if(dpi[5]<6)throw std::runtime_error("short DPI reply");
                    r.metadata.emplace_back(L"report_rate_raw",std::to_wstring(dpi[6]));
                    auto rate=report_rate_hz(dpi[6]);
                    if(rate)r.metadata.emplace_back(L"report_rate",std::to_wstring(rate)+L" Hz");
                    r.metadata.emplace_back(L"dpi_stage",std::to_wstring(dpi[10]));
                    r.metadata.emplace_back(L"raw_rate_dpi",hex(dpi));
                }catch(const std::exception&){}
            } else {
                auto seq=static_cast<unsigned short>((GetTickCount64()%65534)+1);
                auto reply=exchange(device.h,caps,evision_request(seq),0,seq);
                auto [percent,charging]=evision_battery(reply,seq);
                if(percent<0)throw std::runtime_error("invalid battery reply");
                r.percent=percent;r.connected=true;r.observed=GetTickCount64();
                r.source=L"2.4G HID Evision 0x1A";r.status=L"实时读取成功";
                r.metadata.emplace_back(L"charging",charging?L"正在充电":L"未充电");
                r.metadata.emplace_back(L"raw_battery",hex(reply));
            }
        }catch(const std::exception& e) {
            auto text=std::string(e.what());r.error=std::wstring(text.begin(),text.end());
        }
    }
    SetupDiDestroyDeviceInfoList(set);return results;
}
