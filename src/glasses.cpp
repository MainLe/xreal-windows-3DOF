#include "glasses.hpp"
#include "../third_party/json.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
using nlohmann::json;
namespace xreal {
std::vector<HidInfo> enumerateHid(){
    std::vector<HidInfo> out;GUID guid;HidD_GetHidGuid(&guid);
    auto set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(set==INVALID_HANDLE_VALUE)return out;
    SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
    for(DWORD i=0;SetupDiEnumDeviceInterfaces(set,nullptr,&guid,i,&iface);++i){
        DWORD n=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&n,nullptr);
        std::vector<uint8_t> raw(n);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(raw.data());detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,n,nullptr,nullptr))continue;
        HANDLE h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(h==INVALID_HANDLE_VALUE)continue;
        HIDD_ATTRIBUTES a{};a.Size=sizeof(a);HidInfo info;info.path=detail->DevicePath;
        if(HidD_GetAttributes(h,&a)&&a.VendorID==0x3318&&a.ProductID==0x0426){
            std::wstring lower=info.path;for(auto& c:lower)c=wchar_t(towlower(c));auto pos=lower.find(L"&mi_");if(pos!=std::wstring::npos)info.iface=std::wcstoul(lower.c_str()+pos+4,nullptr,16);
            wchar_t serial[256]{};if(HidD_GetSerialNumberString(h,serial,sizeof(serial)))info.serial=serial;
            PHIDP_PREPARSED_DATA pp=nullptr;HIDP_CAPS caps{};
            if(HidD_GetPreparsedData(h,&pp)){if(HidP_GetCaps(pp,&caps)==HIDP_STATUS_SUCCESS){info.input=caps.InputReportByteLength;info.output=caps.OutputReportByteLength;}HidD_FreePreparsedData(pp);}
            out.push_back(info);
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);return out;
}
class Hid {
    HANDLE h_=INVALID_HANDLE_VALUE;unsigned in_=0,out_=0;
    DWORD io(bool write,uint8_t* p,DWORD n,DWORD timeout){
        OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);DWORD done=0;
        BOOL ok=write?WriteFile(h_,p,n,&done,&ov):ReadFile(h_,p,n,&done,&ov);
        if(!ok){DWORD error=GetLastError();if(error!=ERROR_IO_PENDING){CloseHandle(ov.hEvent);throw std::runtime_error("HID I/O failed: "+std::to_string(error));}
            DWORD wait=WaitForSingleObject(ov.hEvent,timeout);
            if(wait!=WAIT_OBJECT_0){CancelIoEx(h_,&ov);GetOverlappedResult(h_,&ov,&done,TRUE);CloseHandle(ov.hEvent);if(wait==WAIT_TIMEOUT&&!write)return 0;throw std::runtime_error("HID write timeout");}
            if(!GetOverlappedResult(h_,&ov,&done,FALSE)){error=GetLastError();CloseHandle(ov.hEvent);throw std::runtime_error("HID completion failed: "+std::to_string(error));}
        }
        CloseHandle(ov.hEvent);return done;
    }
public:
    explicit Hid(const HidInfo& i):in_(i.input),out_(i.output){
        if(in_<8||in_>4096||out_<65||out_>4096)throw std::runtime_error("Unsupported HID report sizes");
        h_=CreateFileW(i.path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(h_==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open HID; close other glasses applications");
        HidD_SetNumInputBuffers(h_,128);
    }
    ~Hid(){if(h_!=INVALID_HANDLE_VALUE)CloseHandle(h_);}
    void write(Packet p){std::vector<uint8_t> b(out_,0);std::copy(p.begin(),p.end(),b.begin()+1);if(io(true,b.data(),DWORD(b.size()),1000)!=b.size())throw std::runtime_error("Incomplete HID write");}
    std::vector<uint8_t> read(unsigned ms){std::vector<uint8_t> b(in_);auto n=io(false,b.data(),DWORD(b.size()),ms);b.resize(n);if(!b.empty()&&b[0]==0)b.erase(b.begin());return b;}
    std::vector<uint8_t> command(bool imu,uint16_t cmd,const std::vector<uint8_t>& payload={}){
        write(imu?imuCommand(uint8_t(cmd),payload):mcuCommand(cmd,payload));auto start=Clock::now();
        while(seconds(start)<1.5){auto p=read(100);uint16_t id=0;std::vector<uint8_t> data;
            if(response(p,imu,id,data)&&(id==cmd||(imu&&id==(cmd|0x80))))return data;
        }
        throw std::runtime_error("No valid HID acknowledgement, command "+std::to_string(cmd));
    }
};
static Calibration calibration(const json& j){
    auto v=[](const json& a){if(!a.is_array()||a.size()!=3)throw std::runtime_error("Invalid calibration vector");Vec v{a.at(0).get<double>(),a.at(1).get<double>(),a.at(2).get<double>()};if(!std::isfinite(v.length())||v.length()>50)throw std::runtime_error("Invalid calibration values");return v;};
    const auto& d=j.at("IMU").at("device_1");return {v(d.at("gyro_bias")),v(d.at("accel_bias"))};
}
void Glasses::run(){
    int originalMode=-1;bool changed=false;std::wstring lastError;
    auto message=[&](std::wstring text,bool connected=false){std::lock_guard<std::mutex> lock(mutex_);status_.message=text;status_.connected=connected;status_.valid=false;};
    while(!stop_){try{
        auto all=enumerateHid();std::vector<HidInfo> mcus,imus;for(auto& i:all){if(i.iface==0)mcus.push_back(i);if(i.iface==2)imus.push_back(i);}
        if(mcus.empty()||imus.empty())throw std::runtime_error("Waiting for Air 2 Ultra HID interfaces 0 and 2");
        // Refuse ambiguous multi-device pairing rather than combine unrelated sensors/displays.
        if(mcus.size()!=1||imus.size()!=1)throw std::runtime_error("Connect exactly one Air 2 Ultra for this first build");
        Hid mcu(mcus[0]),imu(imus[0]);message(L"读取眼镜标定…",true);
        auto mode=mcu.command(false,7);if(mode.size()<2||mode[0]!=0)throw std::runtime_error("Cannot query display mode");originalMode=mode[1];changed=false;
        {std::lock_guard<std::mutex> lock(mutex_);status_.displayMode=originalMode;++status_.generation;}
        imu.command(true,0x19,{0});
        auto len=imu.command(true,0x14);if(len.size()!=4)throw std::runtime_error("Invalid config size response");uint32_t n=u32(len.data());
        if(n<2||n>1024*1024)throw std::runtime_error("Config size out of range");std::vector<uint8_t> config;config.reserve(n);
        auto begin=Clock::now();while(config.size()<n&&!stop_){if(seconds(begin)>20)throw std::runtime_error("Calibration read timeout");auto part=imu.command(true,0x15);if(part.empty())throw std::runtime_error("Empty config chunk");config.insert(config.end(),part.begin(),part.end());}
        if(stop_)break;config.resize(n);while(!config.empty()&&config.back()==0)config.pop_back();
        auto j=json::parse(config);auto c=calibration(j);GyroBias runtimeBias;
        std::string serial;try{auto r=mcu.command(false,0x15);if(r.size()>1&&r[0]==0)serial.assign(r.begin()+1,r.end());while(!serial.empty()&&serial.back()==0)serial.pop_back();}catch(...){}
        auto configCrc=crc32(config.data(),config.size());
        if(!calibrationPath_.empty()&&!serial.empty()){try{
            std::ifstream saved{std::filesystem::path(calibrationPath_)};json b;if(saved){saved>>b;
                if(b.at("version")==1&&b.at("serial")==serial&&b.at("configCrc32")==configCrc){auto a=b.at("offsetRadPerSecond");if(a.is_array()&&a.size()==3)runtimeBias.load({a.at(0).get<double>(),a.at(1).get<double>(),a.at(2).get<double>()});}
            }
        }catch(...) {}}
        imu.command(true,0x19,{1});Fusion fusion;Clock::time_point lastSample=Clock::now();
        {std::lock_guard<std::mutex> lock(mutex_);status_.calibrated=true;status_.samples=0;status_.rejected=0;status_.biasMessage=L"尚未静置校准：放在桌面上后点击校准";}
        while(!stop_){
            int request=modeRequest_.exchange(-1);
            if(request!=-1){int target=request==-2?originalMode:request;
                auto reply=mcu.command(false,8,{uint8_t(target)});if(reply.empty()||reply[0]!=0)throw std::runtime_error("Glasses rejected display mode");changed=(target!=originalMode);
                {std::lock_guard<std::mutex> lock(mutex_);status_.displayMode=target;}
            }
            // Drain occasional MCU events so its Windows HID queue cannot accumulate indefinitely.
            mcu.read(0);
            auto data=imu.read(30);Sample s;
            if(parseImu(data,c,s)){
                if(calibrationRequest_.exchange(false))runtimeBias.start();
                runtimeBias.maintenance(autoBias_.load());
                bool calibratedNow=runtimeBias.update(s);bool persisted=false;
                if(calibratedNow&&!calibrationPath_.empty()&&!serial.empty()){try{
                    auto b=runtimeBias.offset();json saved={{"version",1},{"serial",serial},{"configCrc32",configCrc},{"offsetRadPerSecond",{b.x,b.y,b.z}}};
                    auto temp=calibrationPath_+L".tmp";{std::ofstream out{std::filesystem::path(temp)};out<<saved.dump(2);out.flush();if(!out)throw std::runtime_error("Cannot write calibration");}
                    persisted=MoveFileExW(temp.c_str(),calibrationPath_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
                }catch(...) {}}
                lastSample=Clock::now();Sample corrected=s;corrected.gyro=runtimeBias.correct(s.gyro);bool valid=fusion.update(corrected);
                std::lock_guard<std::mutex> lock(mutex_);status_.q=fusion.orientation();status_.gyro=corrected.gyro;status_.rawGyro=s.gyro;status_.runtimeBias=runtimeBias.offset();status_.biasReady=runtimeBias.ready();status_.biasState=runtimeBias.state();status_.biasProgress=runtimeBias.progress();status_.biasRevision=runtimeBias.revision();status_.biasRestarts=runtimeBias.restarts();status_.last=lastSample;status_.valid=valid;status_.connected=true;++status_.samples;
                status_.autoBiasEnabled=autoBias_.load();status_.autoBiasActive=runtimeBias.maintaining();status_.stationarySeconds=runtimeBias.stationarySeconds();
                if(calibratedNow)status_.biasMessage=persisted?L"静置校准完成并保存；戴上后请重新居中":L"静置校准完成（本次连接有效）；戴上后请重新居中";
                else if(runtimeBias.state()==GyroBias::State::Preparing)status_.biasMessage=L"3秒准备时间，请把眼镜放稳在桌面上";
                else if(runtimeBias.state()==GyroBias::State::Collecting)status_.biasMessage=L"静置校准 "+std::to_wstring(int(runtimeBias.progress()*100))+L"%；移动或振动会重新采集";
                else if(runtimeBias.state()==GyroBias::State::TimedOut)status_.biasMessage=L"静置校准超时，保留原校准值；请放稳后重试";
                else if(runtimeBias.ready()&&runtimeBias.state()==GyroBias::State::None)status_.biasMessage=L"已载入该眼镜上次零偏；温度变化后可重新静置校准";
                status_.message=L"Air 2 Ultra · 标定已读取 · 3DoF 姿态有效";
            }else if(!data.empty()){std::lock_guard<std::mutex> lock(mutex_);++status_.rejected;}
            if(seconds(lastSample)>2)throw std::runtime_error("No valid IMU sample for 2 seconds");
        }
        try{imu.command(true,0x19,{0});if(changed)mcu.command(false,8,{uint8_t(originalMode)});}catch(...){}
    }catch(const std::exception& e){std::string text=e.what();message(std::wstring(text.begin(),text.end()));}
    for(int i=0;i<10&&!stop_;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
}
