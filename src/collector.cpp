#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include "hid_backend.h"

using namespace winrt;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;
using namespace std::chrono_literals;
ULONGLONG deadline{};

template<class T> auto bounded(T op) {
    if (GetTickCount64() >= deadline) throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    if (op.wait_for(1500ms) != Windows::Foundation::AsyncStatus::Completed) {
        op.Cancel(); throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    return op.GetResults();
}
guid uuid(unsigned short short_id) {
    return {short_id, 0, 0x1000, {0x80, 0, 0, 0x80, 0x5f, 0x9b, 0x34, 0xfb}};
}
struct Record {
    std::wstring name, address, id, status = L"未找到配对设备", error;
    bool connected = false;
    int percent = -1;
    ULONGLONG observed = 0;
    std::vector<std::pair<std::wstring, std::wstring>> metadata;
    std::wstring source;
};
std::vector<uint8_t> read(const GattDeviceService& service, unsigned short id) {
    auto chars = bounded(service.GetCharacteristicsForUuidAsync(uuid(id), BluetoothCacheMode::Cached));
    if (chars.Status() != GattCommunicationStatus::Success || !chars.Characteristics().Size()) return {};
    auto ch = chars.Characteristics().GetAt(0);
    if ((ch.CharacteristicProperties() & GattCharacteristicProperties::Read) == GattCharacteristicProperties::None)
        return {};
    auto result = bounded(ch.ReadValueAsync(BluetoothCacheMode::Uncached));
    if (result.Status() != GattCommunicationStatus::Success)
        throw hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_CONNECTED));
    std::vector<uint8_t> data(result.Value().Length());
    DataReader::FromBuffer(result.Value()).ReadBytes(data);
    return data;
}
GattDeviceService service(const BluetoothLEDevice& device, unsigned short id) {
    auto result = bounded(device.GetGattServicesForUuidAsync(uuid(id), BluetoothCacheMode::Cached));
    if (result.Status() == GattCommunicationStatus::Success && result.Services().Size())
        return result.Services().GetAt(0);
    return nullptr;
}
std::wstring utf8(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (!len) return L"(非 UTF-8 数据)";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()), result.data(), len);
    for (auto& c : result) if (c < L' ') c = L' ';
    return result;
}
void collect(const DeviceInformation& info, Record& r, bool metadata_only = false) {
    r.name = info.Name(); r.id = info.Id();
    try {
        auto device = bounded(BluetoothLEDevice::FromIdAsync(info.Id()));
        if (!device) { r.status = L"设备不可访问"; return; }
        wchar_t addr[32]{};
        swprintf_s(addr, L"%012llX", device.BluetoothAddress()); r.address = addr;
        if (!metadata_only) {
            r.connected = device.ConnectionStatus() == BluetoothConnectionStatus::Connected;
            r.status = r.connected ? L"已连接，未取得实时电量" : L"蓝牙未连接";
        }
        // FromIdAsync alone need not establish a connection. An uncached read may
        // reconnect a sleeping paired device, so do not skip on this initial flag.
        if (!metadata_only) if (auto battery = service(device, 0x180f)) {
            try {
                auto value = read(battery, 0x2a19);
                if (value.size() == 1 && value[0] <= 100) {
                    r.percent = value[0]; r.observed = GetTickCount64(); r.status = L"实时读取成功";
                    r.connected = true;
                } else r.status = L"电量特征缺失或数值无效";
            } catch (const hresult_error& e) { r.status = L"电量读取失败"; r.error = e.message(); }
            battery.Close();
        }
        // Each optional field has its own failure boundary; it must not invalidate battery data.
        if (metadata_only) if (auto dis = service(device, 0x180a)) {
            for (const auto& field : std::vector<std::pair<unsigned short, std::wstring>>{
                {static_cast<unsigned short>(0x2a29),L"manufacturer"},{static_cast<unsigned short>(0x2a24),L"model"},{static_cast<unsigned short>(0x2a25),L"serial"},
                {static_cast<unsigned short>(0x2a26),L"firmware"},{static_cast<unsigned short>(0x2a27),L"hardware"},{static_cast<unsigned short>(0x2a28),L"software"}}) {
                if (GetTickCount64() >= deadline) break;
                try { auto value = read(dis, field.first); if (!value.empty()) r.metadata.emplace_back(field.second, utf8(value)); }
                catch (const hresult_error&) { /* Optional and not supported by every device. */ }
            }
            try {
                auto p = read(dis, 0x2a50);
                if (p.size() == 7) {
                    wchar_t text[100]{};
                    swprintf_s(text, L"source=%u VID=%04X PID=%04X revision=%04X", p[0], p[1]|p[2]<<8, p[3]|p[4]<<8, p[5]|p[6]<<8);
                    r.metadata.emplace_back(L"pnp", text);
                }
            } catch (const hresult_error&) {}
            dis.Close();
        }
        if (!metadata_only && r.percent < 0)
            r.connected = device.ConnectionStatus() == BluetoothConnectionStatus::Connected;
        device.Close();
    } catch (const hresult_error& e) {
        // A failure in optional service discovery preserves a completed battery observation.
        r.error = e.message();
        if (r.percent < 0) r.status = L"设备读取失败或超时";
    }
}
void put(const std::wstring& path, const wchar_t* section, const wchar_t* key, std::wstring value) {
    for (auto& c : value) if (c < L' ') c = L' ';
    check_bool(WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()));
}
void save(const std::wstring& output, const Record records[2], const std::wstring& global_error) {
    auto temp = output + L".tmp";
    // UTF-16 BOM makes the Win32 INI API preserve Chinese names and errors.
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw_last_error();
    WORD bom = 0xfeff; DWORD count{};
    BOOL ok = WriteFile(file, &bom, sizeof(bom), &count, nullptr); CloseHandle(file); check_bool(ok);
    put(temp,L"snapshot",L"version",L"1");
    put(temp,L"snapshot",L"error",global_error);
    for (int i=0;i<2;++i) {
        auto section = i == 0 ? L"keyboard" : L"mouse";
        const auto& r = records[i];
        put(temp,section,L"name",r.name); put(temp,section,L"address",r.address);
        put(temp,section,L"id",r.id); put(temp,section,L"status",r.status); put(temp,section,L"error",r.error);
        put(temp,section,L"connected",r.connected?L"1":L"0");
        put(temp,section,L"percent",std::to_wstring(r.percent));
        put(temp,section,L"observed_ms",std::to_wstring(r.observed));
        put(temp,section,L"source",r.percent>=0?(r.source.empty()?L"BLE GATT 180F/2A19 Uncached":r.source):L"无实时数据");
        for (const auto& [key,value] : r.metadata) put(temp,section,key.c_str(),value);
    }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
    check_bool(MoveFileExW(temp.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || std::wstring(argv[1]) != L"--output") {
        std::wcerr << L"Usage: BatteryCollector.exe --output <absolute-path.ini>\n"; return 2;
    }
    init_apartment(apartment_type::multi_threaded);
    // Some Bluetooth RPC calls (including cancellation) can outlive an async
    // wait. The process-level watchdog is independent of those calls.
    std::thread([] { Sleep(25000); ExitProcess(3); }).detach();
    deadline = GetTickCount64() + 18000;
    Record records[2]; std::wstring error;
    DeviceInformation selected[2]{nullptr,nullptr};
    try {
        save(argv[2],records,error);
        auto hid = collect_hid();
        for(int i=0;i<2;++i) if(hid[i].found) {
            auto& r=records[i];auto& h=hid[i];
            r.name=h.name;r.status=h.status;r.error=h.error;r.connected=h.connected;
            r.percent=h.percent;r.observed=h.observed;r.source=h.source;r.metadata=h.metadata;
        }
        save(argv[2],records,error);
        if(records[0].connected && records[1].connected) {
            std::cout << "HID snapshot saved; keyboard=" << records[0].percent << ", mouse=" << records[1].percent << '\n';return 0;
        }
        auto devices = bounded(DeviceInformation::FindAllAsync(BluetoothLEDevice::GetDeviceSelectorFromPairingState(true)));
        for (auto info : devices) {
            std::wstring name = info.Name().c_str();
            std::transform(name.begin(),name.end(),name.begin(),towupper);
            int index = name.find(L"VGN N75 PRO") != std::wstring::npos ? 0 :
                (name.find(L"VXE R1SE") != std::wstring::npos ? 1 : -1);
            if (index < 0 || hid[index].found || records[index].connected) continue;
            selected[index] = info;
            collect(info,records[index]); save(argv[2],records,error);
        }
        for(int i=0;i<2;++i) if(selected[i] && records[i].connected && GetTickCount64()<deadline) {
            collect(selected[i],records[i],true); save(argv[2],records,error);
        }
    } catch (const hresult_error& e) { error = e.message(); }
    try { save(argv[2],records,error); }
    catch (const hresult_error& e) { std::wcerr << e.message().c_str() << L'\n'; return 1; }
    std::cout << "Snapshot saved; keyboard=" << records[0].percent << ", mouse=" << records[1].percent << '\n';
    return error.empty()?0:1;
}
