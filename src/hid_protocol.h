#pragma once
#include <vector>
#include <utility>
#include <cstdint>
using Bytes = std::vector<unsigned char>;
inline Bytes compx_request(unsigned char command, unsigned short address=0, unsigned char length=0) {
    Bytes b(17); b[0]=8;b[1]=command;b[3]=static_cast<unsigned char>(address>>8);
    b[4]=static_cast<unsigned char>(address);b[5]=length;
    unsigned int sum=8;for(int i=1;i<16;++i)sum+=b[i];
    b[16]=static_cast<unsigned char>(85-(sum&255));return b;
}
inline Bytes evision_request(unsigned short sequence) {
    Bytes b(32);b[0]=4;b[1]=static_cast<unsigned char>(sequence);b[2]=static_cast<unsigned char>(sequence>>8);b[3]=0x1a;return b;
}
inline std::pair<int,int> compx_battery(const Bytes& b) {
    if(b.size()<17 || b[0]!=8 || b[1]!=4 || b[2]!=0 || b[5]<2 || b[6]>100)return {-1,-1};
    return {b[6],b[7]!=0};
}
inline bool compx_reply_valid(const Bytes& b, unsigned char command) {
    if(b.size()!=17 || b[0]!=8 || b[1]!=command || b[2]!=0)return false;
    unsigned int sum=0;for(auto byte:b)sum+=byte;
    return (sum&255)==85;
}
inline int report_rate_hz(unsigned char raw) {
    switch(raw) {
    case 1:return 1000;case 2:return 500;case 4:return 250;case 8:return 125;
    case 16:return 2000;case 32:return 4000;case 64:return 8000;default:return 0;
    }
}
inline std::pair<int,int> evision_battery(const Bytes& b,unsigned short seq) {
    if(b.size()<10 || b[0]!=4 || b[1]!=(seq&255) || b[2]!=(seq>>8) || b[3]!=0x1a || b[8]>100)return {-1,-1};
    return {b[8],b[9]!=0};
}
