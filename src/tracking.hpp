#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace xreal {
constexpr double pi = 3.14159265358979323846;
struct Vec {
    double x=0,y=0,z=0;
    Vec operator+(Vec v) const {return {x+v.x,y+v.y,z+v.z};}
    Vec operator-(Vec v) const {return {x-v.x,y-v.y,z-v.z};}
    Vec operator*(double s) const {return {x*s,y*s,z*s};}
    double dot(Vec v) const {return x*v.x+y*v.y+z*v.z;}
    Vec cross(Vec v) const {return {y*v.z-z*v.y,z*v.x-x*v.z,x*v.y-y*v.x};}
    double length() const {return std::sqrt(dot(*this));}
    Vec unit() const {double n=length();return n>1e-10?*this*(1/n):Vec{};}
};
struct Quat {
    double x=0,y=0,z=0,w=1;
    Quat operator*(Quat b) const {return {w*b.x+x*b.w+y*b.z-z*b.y,w*b.y-x*b.z+y*b.w+z*b.x,w*b.z+x*b.y-y*b.x+z*b.w,w*b.w-x*b.x-y*b.y-z*b.z};}
    Quat inverse() const {return {-x,-y,-z,w};}
    Quat unit() const {double n=std::sqrt(x*x+y*y+z*z+w*w);return n>1e-12?Quat{x/n,y/n,z/n,w/n}:Quat{};}
    Vec rotate(Vec v) const {auto r=(*this)*Quat{v.x,v.y,v.z,0}*inverse();return {r.x,r.y,r.z};}
    static Quat axis(Vec axis,double angle) {axis=axis.unit();double s=std::sin(angle/2);return {axis.x*s,axis.y*s,axis.z*s,std::cos(angle/2)};}
    static Quat between(Vec a,Vec b) {
        a=a.unit();b=b.unit();double d=a.dot(b);
        if(d<-.999999){Vec t=std::abs(a.x)<.8?Vec{1,0,0}:Vec{0,0,1};return axis(a.cross(t),pi);}
        Vec c=a.cross(b);return Quat{c.x,c.y,c.z,1+d}.unit();
    }
};
inline uint16_t u16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint64_t u64(const uint8_t* p){return uint64_t(u32(p))|(uint64_t(u32(p+4))<<32);}
inline int32_t i24(const uint8_t* p){uint32_t v=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);return (v&0x800000)?int32_t(v|0xff000000):int32_t(v);}
inline void put16(uint8_t* p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void put32(uint8_t* p,uint32_t v){for(int i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline uint32_t crc32(const uint8_t* p,size_t n){uint32_t r=~0u;while(n--){r^=*p++;for(int i=0;i<8;++i)r=(r>>1)^(0xedb88320u&uint32_t(-int32_t(r&1)));}return ~r;}
using Packet=std::array<uint8_t,64>;
// Layout independently verified against supplied libnr_glasses_api.so cmd_build_*.
inline Packet imuCommand(uint8_t cmd,const std::vector<uint8_t>& payload={}){
    if(payload.size()>56)throw std::runtime_error("IMU command too large");
    Packet p{};p[0]=0xaa;put16(p.data()+5,uint16_t(3+payload.size()));p[7]=cmd;
    std::copy(payload.begin(),payload.end(),p.begin()+8);put32(p.data()+1,crc32(p.data()+5,3+payload.size()));return p;
}
inline Packet mcuCommand(uint16_t cmd,const std::vector<uint8_t>& payload={},uint32_t request=0x1337){
    if(payload.size()>42)throw std::runtime_error("MCU command too large");
    Packet p{};p[0]=0xfd;put16(p.data()+5,uint16_t(17+payload.size()));put32(p.data()+7,request);put16(p.data()+15,cmd);
    std::copy(payload.begin(),payload.end(),p.begin()+22);put32(p.data()+1,crc32(p.data()+5,17+payload.size()));return p;
}
inline bool response(const std::vector<uint8_t>& p,bool imu,uint16_t& cmd,std::vector<uint8_t>& data){
    size_t header=imu?8:22,base=imu?3:17;
    if(p.size()<header||p[0]!=(imu?0xaa:0xfd))return false;
    size_t n=u16(p.data()+5);if(n<base||n+5>p.size()||crc32(p.data()+5,n)!=u32(p.data()+1))return false;
    cmd=imu?p[7]:u16(p.data()+15);data.assign(p.begin()+header,p.begin()+5+n);return true;
}
struct Sample {uint64_t ns=0;Vec gyro,accel;};
struct Calibration {Vec gyroBias,accelBias;};
// Raw IMU axes -> right-handed display axes: +X right, +Y up, view -Z.
// ar-drivers-rs MIT protocol reference, Copyright (C) 2023 Alex Badics.
inline bool parseImu(const std::vector<uint8_t>& p,const Calibration& c,Sample& s){
    if(p.size()<42||p[0]!=1||p[1]!=2)return false;
    double gd=u32(p.data()+14),ad=u32(p.data()+29);if(gd==0||ad==0)return false;
    double gs=u16(p.data()+12)/gd*pi/180,as=u16(p.data()+27)/ad*9.81;
    s.ns=u64(p.data()+4);
    s.gyro={-i24(p.data()+18)*gs-c.gyroBias.x,i24(p.data()+24)*gs+c.gyroBias.y,i24(p.data()+21)*gs+c.gyroBias.z};
    s.accel={-i24(p.data()+33)*as-c.accelBias.x,i24(p.data()+39)*as+c.accelBias.y,i24(p.data()+36)*as+c.accelBias.z};
    return s.ns>0&&std::isfinite(s.gyro.length())&&s.gyro.length()<50&&std::isfinite(s.accel.length())&&s.accel.length()<200;
}
// Gyroscope integration with gravity correction. Input gyro is corrected by the
// separately requested rest calibration. No absolute yaw reference is available.
// This is not the proprietary XREAL fusion engine.
class Fusion {
    Quat q_;uint64_t last_=0;bool aligned_=false;
public:
    bool update(const Sample& s){
        if(!last_){last_=s.ns;if(s.accel.length()>5&&s.accel.length()<15){q_=Quat::between(s.accel,{0,9.81,0});aligned_=true;}return aligned_;}
        if(s.ns<=last_)return false;
        double dt=double(s.ns-last_)*1e-9;last_=s.ns;
        if(dt>.05)return false; // Suspend/disconnect never integrates a large jump.
        if(!aligned_&&s.accel.length()>5&&s.accel.length()<15){q_=Quat::between(s.accel,{0,9.81,0});aligned_=true;}
        if(!aligned_)return false;
        Vec omega=s.gyro;
        double a=s.accel.length();
        if(a>9.0&&a<10.6){Vec measured=s.accel.unit(),expected=q_.inverse().rotate({0,1,0});omega=omega+measured.cross(expected)*.6;}
        double speed=omega.length();if(speed>1e-9)q_=(q_*Quat::axis(omega,dt*speed)).unit();return true;
    }
    Quat orientation() const{return q_;}
};
inline Quat relativePose(Quat anchor,Quat current){return (anchor.inverse()*current).unit();}
inline Quat predictPose(Quat pose,Vec gyro,double horizon){
    double speed=gyro.length();horizon=std::clamp(horizon,0.0,.02);
    if(!std::isfinite(speed)||speed<1e-8||speed>15||horizon==0)return pose;
    return (pose*Quat::axis(gyro,std::min(speed*horizon,.15))).unit();
}
inline std::array<float,2> desktopUv(float u,float v,unsigned rotation){
    switch(rotation){case 2:return {1-v,u};case 3:return {1-u,1-v};case 4:return {v,1-u};default:return {u,v};}
}
}
