#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include "tracking.hpp"
#include "gyro_bias.hpp"

namespace xreal {
using Clock=std::chrono::steady_clock;
inline double seconds(Clock::time_point t){return std::chrono::duration<double>(Clock::now()-t).count();}
struct HidInfo {std::wstring path,serial;unsigned iface=999,input=0,output=0;};
std::vector<HidInfo> enumerateHid();
struct PoseStatus {
    Quat q;Vec gyro,rawGyro,runtimeBias;bool connected=false,valid=false,calibrated=false,biasReady=false;
    uint64_t samples=0,rejected=0,generation=0;int displayMode=-1;Clock::time_point last{};
    GyroBias::State biasState=GyroBias::State::None;double biasProgress=0;unsigned biasRevision=0,biasRestarts=0;
    bool autoBiasEnabled=true,autoBiasActive=false;double stationarySeconds=0;
    std::wstring biasMessage=L"尚未静置校准：放在桌面上后点击校准";
    std::wstring message=L"等待 Air 2 Ultra USB HID";
};
class Glasses {
    std::atomic<bool> stop_{false},calibrationRequest_{false},autoBias_{true};std::atomic<int> modeRequest_{-1};
    std::wstring calibrationPath_;
    std::thread worker_;std::mutex mutex_;PoseStatus status_;
    void run();
public:
    explicit Glasses(std::wstring calibrationPath=L""):calibrationPath_(std::move(calibrationPath)){worker_=std::thread([this]{run();});}
    ~Glasses(){stop_=true;if(worker_.joinable())worker_.join();}
    PoseStatus status(){std::lock_guard<std::mutex> lock(mutex_);auto s=status_;s.valid=s.valid&&seconds(s.last)<.12;return s;}
    void requestStereo(){modeRequest_=3;}
    void restoreDisplay(){modeRequest_=-2;}
    void calibrateGyro(){calibrationRequest_=true;}
    void autoBias(bool enabled){autoBias_=enabled;}
};
}
