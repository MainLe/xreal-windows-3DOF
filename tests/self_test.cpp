#include "../src/graphics.hpp"
#include "../src/glasses.hpp"
#include "../third_party/json.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
using namespace xreal;
using nlohmann::json;
static std::string hex(const Packet& p){std::ostringstream s;for(auto b:p)s<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(b);return s.str();}
int runTests(HINSTANCE instance){
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);auto directory=std::filesystem::path(exe).parent_path();
    json result;result["tests"]=json::array();bool passed=true;
    auto expect=[&](const char* name,bool ok){result["tests"].push_back({{"name",name},{"passed",ok}});passed&=ok;};
    try{
        auto command=imuCommand(0x19,{1});expect("IMU enable golden packet",hex(command).substr(0,18)=="aac5d1214204001901");
        expect("CRC32 standard vector",crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926);
        std::vector<uint8_t> packet(command.begin(),command.end()),payload;uint16_t cmd;
        expect("valid response CRC",response(packet,true,cmd,payload)&&cmd==0x19&&payload==std::vector<uint8_t>{1});packet[8]^=1;expect("corrupt CRC rejected",!response(packet,true,cmd,payload));packet.resize(4);expect("truncated report rejected",!response(packet,true,cmd,payload));
        result["packets"]={{"imu_on",hex(imuCommand(0x19,{1}))},{"imu_off",hex(imuCommand(0x19,{0}))},{"config_length",hex(imuCommand(0x14))},{"config_next",hex(imuCommand(0x15))},{"mode_query",hex(mcuCommand(7))},{"stereo60",hex(mcuCommand(8,{3}))}};
        std::vector<uint8_t> raw(128);raw[0]=1;raw[1]=2;put32(raw.data()+4,1000000000);put16(raw.data()+12,1000);put32(raw.data()+14,1000);put16(raw.data()+27,1);put32(raw.data()+29,1000);raw[39]=0xe8;raw[40]=3;raw[24]=90;
        Sample s;expect("raw IMU axes and physical units",parseImu(raw,{},s)&&std::abs(s.gyro.y-pi/2)<1e-9&&std::abs(s.accel.y-9.81)<1e-9&&s.ns==1000000000);
        raw[18]=0xff;raw[19]=0xff;raw[20]=0xff;expect("signed 24-bit IMU field",parseImu(raw,{},s)&&std::abs(s.gyro.x-pi/180)<1e-9);put32(raw.data()+14,0);expect("zero scale rejected",!parseImu(raw,{},s));
        Fusion f;Sample still{1000000,{}, {0,9.81,0}};f.update(still);for(int i=0;i<10000;++i){still.ns+=1000000;f.update(still);}expect("stationary gravity does not rotate",std::abs(f.orientation().w-1)<1e-10);
        Fusion yaw;Sample turn{1000000,{0,pi/2,0},{0,9.81,0}};yaw.update(turn);for(int i=0;i<1000;++i){turn.ns+=1000000;yaw.update(turn);}auto direction=yaw.orientation().rotate({0,0,-1});expect("one second 90-degree yaw",std::abs(direction.x+1)<1e-6&&std::abs(direction.z)<1e-6);
        auto before=yaw.orientation();turn.ns+=1000000000;expect("timestamp gap not integrated",!yaw.update(turn)&&std::abs(yaw.orientation().w-before.w)<1e-12);turn.ns-=100;expect("out of order timestamp rejected",!yaw.update(turn));
        auto q=Quat::axis({.2,1,.4},.7);auto r=relativePose(q,q);expect("recenter invariant",std::abs(r.w-1)<1e-10);
        auto predicted=predictPose({}, {0,1,0},.01);auto pv=predicted.rotate({0,0,-1});expect("bounded angular prediction",std::abs(pv.x+std::sin(.01))<1e-10&&std::abs(predictPose({}, {0,1,0},1).y-std::sin(.01))<1e-10);expect("zero prediction preserves pose",std::abs(predictPose(q,{1,1,1},0).w-q.w)<1e-10);
        Vec gravity=Quat::axis({1,0,0},.3).inverse().rotate({0,9.81,0});Fusion tilt;tilt.update({1000000,{},gravity});auto world=tilt.orientation().rotate(gravity);expect("gravity aligned at first sample",(world-Vec{0,9.81,0}).length()<1e-8);
        expect("desktop rotation 90 degrees",desktopUv(.2f,.3f,2)==std::array<float,2>{.7f,.2f});expect("desktop rotation 180 degrees",desktopUv(.2f,.3f,3)==std::array<float,2>{.8f,.7f});expect("desktop rotation 270 degrees",desktopUv(.2f,.3f,4)==std::array<float,2>{.3f,.8f});
        // Bias taken from the measured v0.1.0 desk-rest case, with deterministic
        // sensor noise. Learn it only during an explicitly requested calibration.
        Vec bias{.0206*pi/180,.3882*pi/180,.1270*pi/180};
        GyroBias rest;rest.start();Sample b{1000000,bias,{0,9.81,0}};bool completed=false;
        for(int i=0;i<9000;++i){b.ns+=1000000;double noise=.015*pi/180*std::sin(i*.13);b.gyro=bias+Vec{noise,noise*.7,-noise*.5};completed|=rest.update(b);}
        expect("five-second explicit rest calibration completes",completed&&rest.ready()&&rest.state()==GyroBias::State::Complete);
        expect("three-axis runtime zero bias estimated",(rest.offset()-bias).length()<.00001);
        auto heldBias=rest.offset();Fusion correctedFusion;Sample corrected{1000000,rest.correct(bias),{0,9.81,0}};correctedFusion.update(corrected);
        for(int i=0;i<600000;++i){corrected.ns+=1000000;correctedFusion.update(corrected);}
        Vec fwd=correctedFusion.orientation().rotate({0,0,-1});double drift=std::atan2(-fwd.x,-fwd.z)*180/pi;
        result["biasSimulation"]={{"uncorrectedDegreesPerMinute",.3882*60},{"correctedTenMinuteHeadingDegrees",drift}};
        expect("ten-minute static bias drift is suppressed",std::abs(drift)<.05);
        // A deliberately slow real head turn must survive: no dead zone and no
        // stationary detector is allowed to keep learning after calibration.
        Fusion slowYaw;Sample slow{1000000,rest.correct(bias+Vec{0,.2*pi/180,0}),{0,9.81,0}};slowYaw.update(slow);
        for(int i=0;i<10000;++i){slow.ns+=1000000;rest.update({slow.ns,bias+Vec{0,.2*pi/180,0},{0,9.81,0}});slowYaw.update(slow);}
        auto slowForward=slowYaw.orientation().rotate({0,0,-1});double slowDegrees=std::atan2(-slowForward.x,-slowForward.z)*180/pi;
        expect("slow intentional yaw remains tracked",std::abs(slowDegrees-2)<.01&&(rest.offset()-heldBias).length()<1e-12);
        GyroBias motion;motion.load(bias);motion.start();Sample moving{1000000,{0,pi/4,0},{0,9.81,0}};
        for(int i=0;i<47000;++i){moving.ns+=1000000;motion.update(moving);}
        expect("moving calibration times out and preserves previous bias",motion.state()==GyroBias::State::TimedOut&&(motion.offset()-bias).length()<1e-12);
        GyroBias interrupted;interrupted.start();Sample paused{1000000,bias,{0,9.81,0}};
        for(int i=0;i<5000;++i){paused.ns+=1000000;interrupted.update(paused);}double progress=interrupted.progress();paused.ns+=1000000000;interrupted.update(paused);
        expect("sample gap resets rest window",progress>.2&&interrupted.progress()==0&&interrupted.active());
        GyroBias vibrating;vibrating.start();Sample shaken{1000000,bias,{0,9.81,0}};
        for(int i=0;i<12000;++i){shaken.ns+=1000000;shaken.gyro=bias+Vec{.3*pi/180*std::sin(i*.3),0,0};vibrating.update(shaken);}
        expect("vibrating gyro is not accepted as a rest calibration",!vibrating.ready());
        GyroBias invalid;expect("unbounded stored zero bias rejected",!invalid.load({1,0,0})&&!invalid.ready());
        GyroBias thermal;thermal.load(bias);thermal.maintenance(true);Fusion thermalFusion;Sample warm{1000000,{}, {0,9.81,0}};thermalFusion.update(warm);
        for(int i=0;i<120000;++i){warm.ns+=1000000;double driftBias=std::min(double(i)/60000,1.0)*.05*pi/180;Sample input{warm.ns,bias+Vec{0,driftBias,0},{0,9.81,0}};thermal.update(input);warm.gyro=thermal.correct(input.gyro);thermalFusion.update(warm);}
        auto thermalForward=thermalFusion.orientation().rotate({0,0,-1});double thermalDegrees=std::atan2(-thermalForward.x,-thermalForward.z)*180/pi;
        expect("static thermal bias is maintained conservatively",thermal.maintaining()&&std::abs(thermal.correct(bias+Vec{0,.05*pi/180,0}).y)<.001*pi/180&&std::abs(thermalDegrees)<.4);
        result["biasSimulation"]["twoMinuteThermalHeadingDegrees"]=thermalDegrees;
        GyroBias maintainedTurn;maintainedTurn.load(bias);maintainedTurn.maintenance(true);Sample intentional{1000000,bias+Vec{0,.2*pi/180,0},{0,9.81,0}};
        for(int i=0;i<15000;++i){intentional.ns+=1000000;maintainedTurn.update(intentional);}
        expect("maintenance preserves 0.2-degree-per-second yaw",(maintainedTurn.offset()-bias).length()<1e-12&&!maintainedTurn.maintaining());
        maintainedTurn.maintenance(false);intentional.gyro=bias+Vec{0,.02*pi/180,0};for(int i=0;i<10000;++i){intentional.ns+=1000000;maintainedTurn.update(intentional);}
        expect("disabled maintenance preserves extremely slow yaw",(maintainedTurn.offset()-bias).length()<1e-12);
        WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"XrealStableSelfTest";RegisterClassW(&wc);
        HWND win=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,1920,540,nullptr,nullptr,instance,nullptr);
        {Graphics g(win,{},true);g.grid(true);g.screenshot((directory/L"test-stereo-center.png").wstring());g.render({}, {},true,1,2,26,true);g.screenshot((directory/L"test-stereo-yaw.png").wstring());g.render({},Quat::axis({0,1,0},10*pi/180),true,1,2,26,true);g.screenshot((directory/L"test-tracking-lost.png").wstring());g.render({}, {},false,1,2,26,true);expect("D3D WARP stereo rendering and PNG readback",g.stats.rendered==3);}
        DestroyWindow(win);
        auto all=monitors();if(!all.empty()){
            auto selected=all.front();for(auto& m:all)if(m.primary)selected=m;
            win=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,960,540,nullptr,nullptr,instance,nullptr);
            {Graphics capture(win,selected);auto started=GetTickCount64();while(GetTickCount64()-started<2500){capture.render({}, {},true,1,2,26,false);Sleep(5);}result["captureSmoke"]={{"rendered",capture.stats.rendered},{"captured",capture.stats.captured},{"restarts",capture.stats.restarts}};expect("real DXGI desktop duplication smoke",capture.stats.captured>0&&capture.stats.rendered>10);}
            DestroyWindow(win);
        }else expect("local display available for DXGI smoke",false);
    }catch(const std::exception& e){result["exception"]=e.what();passed=false;}
    result["passed"]=passed;result["hardwareTrackingVerified"]=false;std::ofstream out(directory/L"self-test.json");out<<result.dump(2);return passed?0:1;
}
