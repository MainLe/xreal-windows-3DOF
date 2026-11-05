#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc>1&&std::wstring(argv[1])==L"--extend"){auto rc=SetDisplayConfig(0,nullptr,0,nullptr,SDC_APPLY|SDC_TOPOLOGY_EXTEND);std::wcout<<L"EXTEND result="<<rc<<L"\n";if(rc)return int(rc);}
    for(DWORD i=0;;++i){DISPLAY_DEVICEW d{};d.cb=sizeof(d);if(!EnumDisplayDevicesW(nullptr,i,&d,0))break;
        std::wcout<<L"ADAPTER "<<d.DeviceName<<L" | "<<d.DeviceString<<L" flags="<<d.StateFlags<<L"\n";
        for(DWORD j=0;;++j){DISPLAY_DEVICEW m{};m.cb=sizeof(m);if(!EnumDisplayDevicesW(d.DeviceName,j,&m,0))break;std::wcout<<L" MONITOR "<<m.DeviceString<<L" | "<<m.DeviceID<<L" flags="<<m.StateFlags<<L"\n";}
        DEVMODEW dm{};dm.dmSize=sizeof(dm);for(DWORD n=0;EnumDisplaySettingsExW(d.DeviceName,n,&dm,0);++n)if((dm.dmPelsWidth==3840&&dm.dmPelsHeight==1080)||n==0)std::wcout<<L" MODE "<<dm.dmPelsWidth<<L"x"<<dm.dmPelsHeight<<L"@"<<dm.dmDisplayFrequency<<L" depth="<<dm.dmBitsPerPel<<L"\n";
    }
    UINT pc=0,mc=0;LONG status=GetDisplayConfigBufferSizes(QDC_ALL_PATHS,&pc,&mc);if(status)return int(status);
    auto paths=new DISPLAYCONFIG_PATH_INFO[pc];auto modes=new DISPLAYCONFIG_MODE_INFO[mc];status=QueryDisplayConfig(QDC_ALL_PATHS,&pc,paths,&mc,modes,nullptr);if(status)return int(status);
    for(UINT i=0;i<pc;++i){auto& p=paths[i];DISPLAYCONFIG_TARGET_DEVICE_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;name.header.size=sizeof(name);name.header.adapterId=p.targetInfo.adapterId;name.header.id=p.targetInfo.id;DisplayConfigGetDeviceInfo(&name.header);DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};source.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;source.header.size=sizeof(source);source.header.adapterId=p.sourceInfo.adapterId;source.header.id=p.sourceInfo.id;DisplayConfigGetDeviceInfo(&source.header);
        if(p.targetInfo.targetAvailable)std::wcout<<L"PATH "<<source.viewGdiDeviceName<<L" -> "<<name.monitorFriendlyDeviceName<<L" available="<<p.targetInfo.targetAvailable<<L" active="<<((p.flags&DISPLAYCONFIG_PATH_ACTIVE)!=0)<<L" target="<<p.targetInfo.id<<L" connector="<<name.connectorInstance<<L"\n";
    }delete[] paths;delete[] modes;return 0;
}
