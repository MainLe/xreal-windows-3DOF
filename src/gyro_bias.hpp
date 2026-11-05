#pragma once
#include "tracking.hpp"

namespace xreal {
// Explicit rest calibration, with optional conservative bias maintenance after
// it succeeds. No yaw snapping, angular dead zone, or automatic recentering.
// Very slow constant yaw remains unobservable and requires disabling maintenance.
class GyroBias {
public:
    enum class State {None,Preparing,Collecting,Complete,TimedOut};
private:
    Vec bias_,meanGyro_,meanAccel_,m2Gyro_,m2Accel_,firstAccel_;
    uint64_t started_=0,first_=0,last_=0;unsigned count_=0;bool requested_=false,ready_=false;
    State state_=State::None;double progress_=0;unsigned revision_=0,restarts_=0;
    bool maintenance_=false,maintaining_=false,filtered_=false;uint64_t maintenanceLast_=0;
    double restSeconds_=0;Vec filteredGyro_,filteredAccel_,restAccel_;
    static constexpr uint64_t delayNs=3000000000ull,windowNs=5000000000ull,timeoutNs=45000000000ull;
    void resetWindow(){first_=0;count_=0;meanGyro_={};meanAccel_={};m2Gyro_={};m2Accel_={};progress_=0;}
    static void add(Vec value,unsigned count,Vec& mean,Vec& m2){
        Vec d=value-mean;mean=mean+d*(1.0/count);Vec d2=value-mean;
        m2=m2+Vec{d.x*d2.x,d.y*d2.y,d.z*d2.z};
    }
    static double maxStd(Vec m2,unsigned count){return std::sqrt(std::max({0.0,m2.x,m2.y,m2.z})/(count-1));}
    void maintain(const Sample& s){
        maintaining_=false;
        if(!maintenance_||!ready_||!s.ns||!std::isfinite(s.gyro.length())||!std::isfinite(s.accel.length())){maintenanceLast_=0;filtered_=false;restSeconds_=0;return;}
        if(!maintenanceLast_){maintenanceLast_=s.ns;return;}
        if(s.ns<=maintenanceLast_)return;
        double dt=double(s.ns-maintenanceLast_)*1e-9;maintenanceLast_=s.ns;
        if(dt>.05){filtered_=false;restSeconds_=0;return;}
        if(!filtered_){filteredGyro_=s.gyro;filteredAccel_=s.accel;filtered_=true;restSeconds_=0;restAccel_=s.accel;}
        double alpha=1-std::exp(-dt/.4);filteredGyro_=filteredGyro_+(s.gyro-filteredGyro_)*alpha;filteredAccel_=filteredAccel_+(s.accel-filteredAccel_)*alpha;
        // Filter first, so individual sensor-noise spikes are not mistaken for
        // a head turn. A larger raw motion still cancels immediately.
        if((s.gyro-bias_).length()>1.5*pi/180||(filteredGyro_-bias_).length()>.12*pi/180||std::abs(filteredAccel_.length()-9.81)>.3||(s.accel-filteredAccel_).length()>.3||(filteredAccel_-restAccel_).length()>.03){
            restSeconds_=0;restAccel_=filteredAccel_;return;
        }
        restSeconds_+=dt;if(restSeconds_<3)return;
        bias_=bias_+(filteredGyro_-bias_)*(1-std::exp(-dt/5));maintaining_=true;
    }
public:
    void start(){requested_=true;started_=last_=0;restarts_=0;state_=State::Preparing;resetWindow();maintenanceLast_=0;restSeconds_=0;maintaining_=false;filtered_=false;}
    bool load(Vec bias){if(!std::isfinite(bias.length())||bias.length()>3*pi/180)return false;bias_=bias;ready_=true;++revision_;return true;}
    bool update(const Sample& s){
        if(!active()){maintain(s);return false;}
        if(!s.ns||!std::isfinite(s.gyro.length())||!std::isfinite(s.accel.length()))return false;
        if(requested_){started_=last_=s.ns;requested_=false;}
        if(s.ns<last_)return false;
        if(s.ns-started_>timeoutNs){state_=State::TimedOut;progress_=0;return false;}
        if(s.ns-started_<delayNs){last_=s.ns;return false;}
        state_=State::Collecting;
        if(s.ns==last_)return false;
        if(s.ns-last_>50000000ull){resetWindow();++restarts_;}last_=s.ns;
        if(s.gyro.length()>3*pi/180||std::abs(s.accel.length()-9.81)>.4){resetWindow();++restarts_;return false;}
        if(first_&&(s.accel-firstAccel_).length()>.1){resetWindow();++restarts_;}
        if(!first_){first_=s.ns;firstAccel_=s.accel;}
        ++count_;add(s.gyro,count_,meanGyro_,m2Gyro_);add(s.accel,count_,meanAccel_,m2Accel_);
        progress_=std::min(1.0,double(s.ns-first_)/windowNs);
        if(s.ns-first_<windowNs||count_<250)return false;
        if(maxStd(m2Gyro_,count_)>.12*pi/180||maxStd(m2Accel_,count_)>.08){resetWindow();++restarts_;return false;}
        bias_=meanGyro_;ready_=true;++revision_;state_=State::Complete;return true;
    }
    Vec correct(Vec gyro) const{return gyro-bias_;}
    Vec offset() const{return bias_;}
    bool active() const{return state_==State::Preparing||state_==State::Collecting;}
    bool ready() const{return ready_;}
    State state() const{return state_;}
    double progress() const{return progress_;}
    unsigned revision() const{return revision_;}
    unsigned restarts() const{return restarts_;}
    void maintenance(bool enabled){maintenance_=enabled;}
    bool maintaining() const{return maintaining_;}
    double stationarySeconds() const{return restSeconds_;}
};
}
