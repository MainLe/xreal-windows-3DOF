#include "graphics.hpp"
#include "glasses.hpp"
#include "../third_party/json.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iomanip>
using namespace xreal;
using nlohmann::json;
int runTests(HINSTANCE instance);
namespace {
constexpr wchar_t version[]=L"0.1.1";
enum {Source=100,Output,Refresh,Connect,Stereo,Start,Stop,Recenter,Grid,Scale,Distance,Fov,Preview,Report,Prediction,Calibrate,AutoBias};
HWND control=nullptr,eye=nullptr,sourceBox=nullptr,outputBox=nullptr,statusText=nullptr,scaleBar=nullptr,distanceBar=nullptr,fovBar=nullptr,gridBox=nullptr,predictionBar=nullptr;
HWND autoBiasBox=nullptr;
HFONT uiFont=nullptr;UINT uiDpi=96;uint64_t poseGeneration=0;
std::vector<Monitor> displays;std::unique_ptr<Glasses> glasses;std::unique_ptr<Graphics> graphics;
Quat anchor;bool anchored=false,preview=false,stereo=true;std::wstring error,notice;
unsigned selectedSource=0;ULONGLONG statsAt=0;uint64_t previousRender=0,previousCapture=0;double renderFps=0,captureFps=0;
std::filesystem::path directory;std::ofstream metrics;
int px(int n){return MulDiv(n,uiDpi,96);}
HWND widget(const wchar_t* cls,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h){auto win=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,control,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SetWindowPos(win,nullptr,px(x),px(y),px(w),px(h),SWP_NOZORDER);SendMessageW(win,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE);return win;}
void text(const wchar_t* label,int x,int y,int w=640,int h=22){widget(L"STATIC",label,0,0,x,y,w,h);}
float slider(HWND hwnd,float divisor){return float(SendMessageW(hwnd,TBM_GETPOS,0,0))/divisor;}
void saveSettings(){json j={{"scale",slider(scaleBar,100)},{"distanceMeters",slider(distanceBar,100)},{"verticalFovDegrees",slider(fovBar,10)},{"predictionMs",slider(predictionBar,1)},{"autoGyroBias",SendMessageW(autoBiasBox,BM_GETCHECK,0,0)==BST_CHECKED}};std::ofstream out(directory/L"settings.json");out<<j.dump(2);}
void loadSettings(){try{std::ifstream in(directory/L"settings.json");if(!in)return;json j;in>>j;
    auto set=[&](HWND h,const char* key,double factor,int min,int max){if(j.contains(key)&&j[key].is_number()){double v=j[key].get<double>()*factor;if(std::isfinite(v))SendMessageW(h,TBM_SETPOS,TRUE,std::clamp(int(std::lround(std::clamp(v,double(min),double(max)))),min,max));}};
    set(scaleBar,"scale",100,25,150);set(distanceBar,"distanceMeters",100,50,500);set(fovBar,"verticalFovDegrees",10,180,400);set(predictionBar,"predictionMs",1,0,20);
    if(j.contains("autoGyroBias")&&j["autoGyroBias"].is_boolean())SendMessageW(autoBiasBox,BM_SETCHECK,j["autoGyroBias"].get<bool>()?BST_CHECKED:BST_UNCHECKED,0);
    }catch(...) {}}
std::string utf8(std::wstring s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
void stop(){graphics.reset();if(eye){DestroyWindow(eye);eye=nullptr;}anchored=false;}
void refresh(){
    displays=monitors();SendMessageW(sourceBox,CB_RESETCONTENT,0,0);SendMessageW(outputBox,CB_RESETCONTENT,0,0);
    int source=0,output=-1;
    for(size_t i=0;i<displays.size();++i){auto& m=displays[i];SendMessageW(sourceBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(m.name.c_str()));SendMessageW(outputBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(m.name.c_str()));if(m.primary&&!m.glasses)source=int(i);if(m.glasses)output=int(i);}
    if(output<0&&displays.size()>1)output=source==0?1:0;
    SendMessageW(sourceBox,CB_SETCURSEL,source,0);SendMessageW(outputBox,CB_SETCURSEL,output,0);
}
void connect(){if(!glasses){glasses=std::make_unique<Glasses>((directory/L"gyro-calibration.json").wstring());glasses->autoBias(SendMessageW(autoBiasBox,BM_GETCHECK,0,0)==BST_CHECKED);}}
void begin(bool isPreview){
    stop();error.clear();notice.clear();preview=isPreview;
    int src=int(SendMessageW(sourceBox,CB_GETCURSEL,0,0)),dst=int(SendMessageW(outputBox,CB_GETCURSEL,0,0));
    if(src<0||src>=int(displays.size()))throw std::runtime_error("Select a desktop source");
    selectedSource=src;
    int x=80,y=80,w=1100,h=640;DWORD style=WS_OVERLAPPEDWINDOW;
    if(!preview){
        if(dst<0||dst>=int(displays.size())||dst==src)throw std::runtime_error("Use Windows Extend mode; source and glasses output must be different displays");
        auto r=displays[dst].rect;x=r.left;y=r.top;w=r.right-r.left;h=r.bottom-r.top;
        if(w!=3840||h!=1080)throw std::runtime_error("Glasses display must be 3840x1080 Full SBS. Click SBS 60Hz, wait, then refresh display list");
        style=WS_POPUP;connect();
    }
    stereo=!preview;
    eye=CreateWindowExW(WS_EX_TOOLWINDOW,L"XrealStableEye",preview?L"桌面预览 · 固定视角，不验证 3DoF":L"XREAL 稳屏 · 3DoF",style,x,y,w,h,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!eye)throw std::runtime_error("Cannot create display window");
    SetWindowDisplayAffinity(eye,WDA_EXCLUDEFROMCAPTURE);ShowWindow(eye,SW_SHOWNOACTIVATE);
    try{graphics=std::make_unique<Graphics>(eye,displays[src]);}catch(...){stop();throw;}
    statsAt=GetTickCount64();previousRender=previousCapture=0;
}
json report(){
    json j={{"version","0.1.1"},{"monotonicMs",GetTickCount64()},{"platform","Windows x64"},{"transport","Microsoft HID / Air 2 Ultra 3318:0426 / transplanted USB protocol"},{"fusion","local gyro + gravity + explicit rest bias calibration; not XREAL proprietary tracking engine"},{"preview",preview},{"rendering",bool(graphics)},{"renderFps",renderFps},{"captureUpdateFps",captureFps},{"scale",slider(scaleBar,100)},{"distanceMeters",slider(distanceBar,100)},{"verticalFovDegrees",slider(fovBar,10)},{"predictionMs",slider(predictionBar,1)},{"error",utf8(error)},{"notice",utf8(notice)}};
    if(glasses){auto s=glasses->status();j["glasses"]={{"connected",s.connected},{"validPose",s.valid},{"calibrated",s.calibrated},{"samples",s.samples},{"rejected",s.rejected},{"sampleAgeMs",s.samples?seconds(s.last)*1000:-1},{"displayMode",s.displayMode},{"generation",s.generation},{"rotationXYZW",{s.q.x,s.q.y,s.q.z,s.q.w}},{"gyroRadPerSecond",{s.gyro.x,s.gyro.y,s.gyro.z}},{"rawGyroRadPerSecond",{s.rawGyro.x,s.rawGyro.y,s.rawGyro.z}},{"runtimeGyroBiasRadPerSecond",{s.runtimeBias.x,s.runtimeBias.y,s.runtimeBias.z}},{"restCalibrationReady",s.biasReady},{"restCalibrationState",int(s.biasState)},{"restCalibrationProgress",s.biasProgress},{"restCalibrationRevision",s.biasRevision},{"restCalibrationRestarts",s.biasRestarts},{"restCalibrationMessage",utf8(s.biasMessage)},{"message",utf8(s.message)}};}
    if(glasses){auto s=glasses->status();j["glasses"]["autoBiasEnabled"]=s.autoBiasEnabled;j["glasses"]["autoBiasActive"]=s.autoBiasActive;j["glasses"]["stationarySeconds"]=s.stationarySeconds;}
    j["displays"]=json::array();for(auto& d:displays)j["displays"].push_back({{"name",utf8(d.name)},{"primary",d.primary},{"glassesCandidate",d.glasses},{"left",d.rect.left},{"top",d.rect.top},{"width",d.rect.right-d.rect.left},{"height",d.rect.bottom-d.rect.top}});
    if(graphics){auto s=graphics->stats;j["capture"]={{"frames",s.captured},{"rendered",s.rendered},{"noUpdatePolls",s.timeouts},{"restarts",s.restarts},{"lastUpdateAgeSeconds",s.lastCaptureAge},{"status",utf8(s.capture)}};}
    return j;
}
void updateStatus(){
    if(graphics){auto now=GetTickCount64();double dt=double(now-statsAt)/1000;if(dt>=.5){auto& s=graphics->stats;renderFps=(s.rendered-previousRender)/dt;captureFps=(s.captured-previousCapture)/dt;previousRender=s.rendered;previousCapture=s.captured;statsAt=now;}}
    std::wstring t=error.empty()?L"":L"错误："+error+L"\r\n";
    if(!notice.empty())t+=notice+L"\r\n";
    if(glasses){auto s=glasses->status();t+=s.message+L"\r\n";if(s.connected&&!s.valid)t+=L"姿态未就绪 / 数据过期：眼镜输出保持黑色\r\n";}else t+=L"请连接 Air 2 Ultra 到电脑支持 DP 视频输出的 USB-C 接口\r\n";
    if(glasses)t+=glasses->status().biasMessage+L"\r\n";
    if(glasses&&glasses->status().autoBiasActive)t+=L"静止微调中 · ";
    if(graphics){t+=L"渲染 "+std::to_wstring(int(renderFps))+L" fps · 桌面更新 "+std::to_wstring(int(captureFps))+L" fps · "+graphics->stats.capture+L"\r\n";if(preview)t+=L"当前为固定视角桌面预览；不验证眼镜悬停\r\n";}
    t+=L"屏幕 "+std::to_wstring(int(slider(scaleBar,1)))+L"% · 距离 "+std::to_wstring(slider(distanceBar,100)).substr(0,4)+L" m · 视场 "+std::to_wstring(slider(fovBar,10)).substr(0,4)+L"° · 预测 "+std::to_wstring(int(slider(predictionBar,1)))+L"ms";
    SetWindowTextW(statusText,t.c_str());
    if(metrics.is_open()){metrics<<report().dump()<<'\n';metrics.flush();}
}
void command(unsigned id){try{
    switch(id){case Refresh:if(graphics)stop();refresh();break;case Connect:connect();break;case Stereo:connect();glasses->requestStereo();notice=L"正在切换 SBS 60Hz；随后自动恢复扩展显示。";error.clear();SetTimer(control,3,3000,nullptr);break;case Start:begin(false);break;case Preview:begin(true);break;case Stop:stop();break;case Recenter:anchored=false;break;case Calibrate:connect();glasses->calibrateGyro();notice.clear();break;case AutoBias:if(glasses)glasses->autoBias(SendMessageW(autoBiasBox,BM_GETCHECK,0,0)==BST_CHECKED);break;case Report:{std::ofstream out(directory/L"diagnostic.json");out<<report().dump(2);notice=L"已保存程序目录下 diagnostic.json";break;}}
    }catch(const std::exception& e){auto s=std::string(e.what());error=std::wstring(s.begin(),s.end());}updateStatus();}
LRESULT CALLBACK eyeProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){if(msg==WM_CLOSE){PostMessageW(control,WM_COMMAND,Stop,0);return 0;}if(msg==WM_KEYDOWN&&w==VK_ESCAPE){PostMessageW(control,WM_COMMAND,Stop,0);return 0;}if(msg==WM_ERASEBKGND)return 1;return DefWindowProcW(hwnd,msg,w,l);}
LRESULT CALLBACK controlProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_COMMAND){command(LOWORD(w));return 0;}
    if(msg==WM_TIMER){
        if(w==2){KillTimer(hwnd,2);try{refresh();}catch(...) {}}
        if(w==3){KillTimer(hwnd,3);if(glasses&&glasses->status().displayMode==3){LONG rc=SetDisplayConfig(0,nullptr,0,nullptr,SDC_APPLY|SDC_TOPOLOGY_EXTEND);if(rc==ERROR_SUCCESS){notice=L"SBS 已启用，扩展显示已恢复；请启动眼镜画面。";SetTimer(hwnd,2,1500,nullptr);}else error=L"恢复扩展显示失败，Windows 错误 "+std::to_wstring(rc);}else error=L"眼镜尚未确认 SBS 模式，请检查连接状态后重试。";}
        updateStatus();return 0;
    }
    if(msg==WM_HOTKEY){command(w==1?Recenter:Stop);return 0;}
    if(msg==WM_DISPLAYCHANGE){stop();notice=L"显示配置已变化，正在刷新显示器列表。";SetTimer(hwnd,2,1500,nullptr);return 0;}
    if(msg==WM_CLOSE){saveSettings();DestroyWindow(hwnd);return 0;}
    if(msg==WM_DESTROY){stop();PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,msg,w,l);
}
void layout(){
    text(L"XREAL Windows 稳屏 · 3DoF 移植验证版",20,16);
    text(L"先在 Windows 显示设置中选择「扩展这些显示器」。仅连接一副 Air 2 Ultra。",20,46);
    text(L"桌面来源",20,83,100);sourceBox=widget(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL,Source,120,78,540,200);
    text(L"眼镜输出",20,123,100);outputBox=widget(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL,Output,120,118,540,200);
    widget(L"BUTTON",L"刷新显示器",BS_PUSHBUTTON,Refresh,20,160,135,32);widget(L"BUTTON",L"连接 / 读取姿态",BS_PUSHBUTTON,Connect,165,160,160,32);widget(L"BUTTON",L"切换 SBS 60Hz",BS_PUSHBUTTON,Stereo,335,160,155,32);
    widget(L"BUTTON",L"启动 3DoF 眼镜画面",BS_PUSHBUTTON,Start,20,205,195,38);widget(L"BUTTON",L"停止画面",BS_PUSHBUTTON,Stop,225,205,120,38);widget(L"BUTTON",L"重新居中",BS_PUSHBUTTON,Recenter,355,205,135,38);
    gridBox=widget(L"BUTTON",L"静态标定网格（关闭后显示桌面）",BS_AUTOCHECKBOX,Grid,20,255,420,30);
    autoBiasBox=widget(L"BUTTON",L"静止微调（极慢转头可关闭）",BS_AUTOCHECKBOX,AutoBias,20,722,620,25);SendMessageW(autoBiasBox,BM_SETCHECK,BST_CHECKED,0);
    text(L"屏幕大小 25%～150%",20,300,200);scaleBar=widget(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS,Scale,220,290,440,40);SendMessageW(scaleBar,TBM_SETRANGE,TRUE,MAKELONG(25,150));SendMessageW(scaleBar,TBM_SETPOS,TRUE,100);
    text(L"屏幕距离 0.5～5.0m",20,345,200);distanceBar=widget(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS,Distance,220,335,440,40);SendMessageW(distanceBar,TBM_SETRANGE,TRUE,MAKELONG(50,500));SendMessageW(distanceBar,TBM_SETPOS,TRUE,200);
    text(L"垂直视场角 18°～40°",20,390,200);fovBar=widget(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS,Fov,220,380,440,40);SendMessageW(fovBar,TBM_SETRANGE,TRUE,MAKELONG(180,400));SendMessageW(fovBar,TBM_SETPOS,TRUE,260);
    text(L"转头预测 0～20ms",20,435,200);predictionBar=widget(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS,Prediction,220,425,440,40);SendMessageW(predictionBar,TBM_SETRANGE,TRUE,MAKELONG(0,20));SendMessageW(predictionBar,TBM_SETPOS,TRUE,8);
    text(L"快捷键：Ctrl + Alt + R 居中；Ctrl + Alt + Esc 停止。鼠标键盘照常操作电脑。",20,480);
    widget(L"BUTTON",L"桌面预览（固定视角）",BS_PUSHBUTTON,Preview,20,515,220,32);widget(L"BUTTON",L"保存诊断",BS_PUSHBUTTON,Report,250,515,125,32);
    widget(L"BUTTON",L"静置校准（5秒）",BS_PUSHBUTTON,Calibrate,385,515,180,32);
    statusText=widget(L"STATIC",L"",SS_LEFT,0,20,565,645,125);
    text(L"转头预测可调为 0 作对照；若出现回弹请减小。视场角暂用近似值，需实物校准。",20,695);
}
int diagnose(){json j;j["hid"]=json::array();for(auto& h:enumerateHid())j["hid"].push_back({{"path",utf8(h.path)},{"interface",h.iface},{"inputReportBytes",h.input},{"outputReportBytes",h.output}});j["monitors"]=json::array();for(auto& m:monitors())j["monitors"].push_back({{"name",utf8(m.name)},{"primary",m.primary},{"glassesCandidate",m.glasses}});std::ofstream out(directory/L"hardware-inventory.json");out<<j.dump(2);return 0;}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR commandLine,int show){
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);directory=std::filesystem::path(path).parent_path();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;try{
        std::wstring args=commandLine;
        if(args.find(L"--diagnose")!=std::wstring::npos)return diagnose();
        if(args.find(L"--self-test")!=std::wstring::npos)return runTests(instance);
        INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_BAR_CLASSES};InitCommonControlsEx(&ic);
        WNDCLASSW wc{};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=HBRUSH(COLOR_WINDOW+1);wc.lpfnWndProc=eyeProc;wc.lpszClassName=L"XrealStableEye";RegisterClassW(&wc);wc.lpfnWndProc=controlProc;wc.lpszClassName=L"XrealStableControl";RegisterClassW(&wc);
        control=CreateWindowExW(0,wc.lpszClassName,L"XREAL Windows 稳屏 0.1.1",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,710,730,nullptr,nullptr,instance,nullptr);
        uiDpi=GetDpiForWindow(control);uiFont=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");SetWindowPos(control,nullptr,0,0,px(710),px(790),SWP_NOMOVE|SWP_NOZORDER);
        layout();loadSettings();refresh();ShowWindow(control,show);RegisterHotKey(control,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'R');RegisterHotKey(control,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_ESCAPE);SetTimer(control,1,1000,nullptr);metrics.open(directory/L"runtime-metrics.jsonl",std::ios::trunc);updateStatus();
        bool quit=false;while(!quit){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){quit=true;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}if(quit)break;
            if(graphics){try{PoseStatus pose;if(glasses)pose=glasses->status();bool ready=preview||pose.valid;
                if(pose.generation!=poseGeneration){poseGeneration=pose.generation;anchored=false;}
                if(ready&&!anchored){anchor=preview?Quat{}:pose.q;anchored=true;}
                graphics->grid(SendMessageW(gridBox,BM_GETCHECK,0,0)==BST_CHECKED);
                auto predicted=predictPose(pose.q,pose.gyro,slider(predictionBar,1000));
                graphics->render(anchor,preview?Quat{}:predicted,ready,slider(scaleBar,100),slider(distanceBar,100),slider(fovBar,10),stereo);
            }catch(const std::exception& e){std::string s=e.what();error=std::wstring(s.begin(),s.end());stop();updateStatus();}}
            else MsgWaitForMultipleObjects(0,nullptr,FALSE,100,QS_ALLINPUT);
        }
        glasses.reset();UnregisterHotKey(control,1);UnregisterHotKey(control,2);if(uiFont)DeleteObject(uiFont);
    }catch(const std::exception& e){std::ofstream out(directory/L"startup-error.txt");out<<e.what();result=1;}
    CoUninitialize();return result;
}
