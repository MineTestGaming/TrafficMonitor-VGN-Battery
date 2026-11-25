#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <iostream>
#include <vector>
int main() {
    GUID guid{};HidD_GetHidGuid(&guid);
    auto set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    for(DWORD i=0;;++i) {
        SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
        if(!SetupDiEnumDeviceInterfaces(set,nullptr,&guid,i,&iface))break;
        DWORD size{};SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&size,nullptr);
        std::vector<unsigned char> bytes(size);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(bytes.data());detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,size,nullptr,nullptr))continue;
        auto h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(h==INVALID_HANDLE_VALUE)continue;
        HIDD_ATTRIBUTES a{};a.Size=sizeof(a);HidD_GetAttributes(h,&a);
        if(a.VendorID==0x320f || a.VendorID==0x3554) {
            PHIDP_PREPARSED_DATA pre{};HIDP_CAPS caps{};
            if(HidD_GetPreparsedData(h,&pre)) {
                HidP_GetCaps(pre,&caps);HidD_FreePreparsedData(pre);
                std::cout << std::hex << a.VendorID << ':' << a.ProductID << " usage=" << caps.UsagePage << ':' << caps.Usage << std::dec
                    << " input=" << caps.InputReportByteLength << " output=" << caps.OutputReportByteLength << " feature=" << caps.FeatureReportByteLength << '\n';
                std::wcout << detail->DevicePath << L'\n';
            }
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
}
