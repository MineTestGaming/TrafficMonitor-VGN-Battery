#include "hid_protocol.h"
#include <iostream>
int main() {
    auto m = compx_request(4);
    if(m.size()!=17 || m[0]!=8 || m[1]!=4 || m[16]!=0x49) return 1;
    auto k = evision_request(0x1234);
    if(k.size()!=32 || k[0]!=4 || k[1]!=0x34 || k[2]!=0x12 || k[3]!=0x1a) return 2;
    std::vector<unsigned char> bad{8,4,0};
    if(compx_battery(bad).first!=-1) return 3;
    m[5]=2; m[6]=0; m[7]=1;
    if(compx_battery(m)!=std::pair<int,int>{0,1}) return 4;
    m[6]=101;
    if(compx_battery(m).first!=-1) return 5;
    k[8]=81; k[9]=0;
    if(evision_battery(k,0x1234)!=std::pair<int,int>{81,0}) return 6;
    if(evision_battery(k,1).first!=-1) return 7;
    Bytes observed{0x08,0x04,0,0,0,0x02,0x14,0,0x0D,0xDC,0,0,0,0,0,0,0x4A};
    if(!compx_reply_valid(observed,4) || compx_battery(observed).first!=20) return 8;
    observed[16]^=1;
    if(compx_reply_valid(observed,4))return 9;
    if(report_rate_hz(1)!=1000 || report_rate_hz(3)!=0)return 10;
    m[6]=20;m[2]=1;
    if(compx_battery(m).first!=-1)return 11;
    m[2]=0;m[5]=1;
    if(compx_battery(m).first!=-1)return 12;
    Bytes charging{0x08,0x04,0,0,0,0x02,0x1e,0x01,0x0e,0x7d,0,0,0,0,0,0,0x9d};
    if(!compx_reply_valid(charging,4) || compx_battery(charging)!=std::pair<int,int>{30,1})return 13;
    k[8]=59;k[9]=1;
    if(evision_battery(k,0x1234)!=std::pair<int,int>{59,1})return 14;
    std::cout << "HID request encoding, report offsets, short/invalid replies passed\n";
}
