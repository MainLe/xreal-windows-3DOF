#include "graphics.hpp"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <chrono>
#include <sstream>
#include <cmath>
namespace xreal {
void check(HRESULT hr,const char* what){if(FAILED(hr)){std::ostringstream s;s<<what<<" HRESULT=0x"<<std::hex<<unsigned(hr);throw std::runtime_error(s.str());}}
std::vector<Monitor> monitors(){
    std::vector<Monitor> result;Com<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
    for(unsigned a=0;;++a){Com<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        for(unsigned o=0;;++o){Com<IDXGIOutput> output;if(adapter->EnumOutputs(o,&output)==DXGI_ERROR_NOT_FOUND)break;DXGI_OUTPUT_DESC d{};if(FAILED(output->GetDesc(&d))||!d.AttachedToDesktop)continue;
            MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(d.Monitor,&mi);DISPLAY_DEVICEW dd{};dd.cb=sizeof(dd);EnumDisplayDevicesW(d.DeviceName,0,&dd,0);
            std::wstring label=std::wstring(d.DeviceName)+L" · "+dd.DeviceString;
            unsigned w=d.DesktopCoordinates.right-d.DesktopCoordinates.left,h=d.DesktopCoordinates.bottom-d.DesktopCoordinates.top;
            std::wstring lower=label;for(auto& c:lower)c=wchar_t(towlower(c));bool glasses=lower.find(L"xreal")!=std::wstring::npos||lower.find(L"nreal")!=std::wstring::npos||lower.find(L"air 2")!=std::wstring::npos||(w==3840&&h==1080);
            result.push_back({d.Monitor,d.DesktopCoordinates,label+L" ("+std::to_wstring(w)+L"×"+std::to_wstring(h)+L")",adapter,output,bool(mi.dwFlags&MONITORINFOF_PRIMARY),glasses});
        }
    }return result;
}
static const char* shader=R"(
Texture2D screenTex:register(t0);Texture2D cursorTex:register(t1);SamplerState linearSampler:register(s0);
cbuffer Params:register(b0){float4 cursorRect;float cursorEnabled;float3 padding;};
struct V {float4 pos:POSITION;float2 uv:TEXCOORD0;float2 logical:TEXCOORD1;};
struct P {float4 pos:SV_POSITION;float2 uv:TEXCOORD0;float2 logical:TEXCOORD1;};
P vs(V v){P p;p.pos=v.pos;p.uv=v.uv;p.logical=v.logical;return p;}
float4 ps(P p):SV_TARGET {
    float4 base=screenTex.Sample(linearSampler,p.uv);
    if(cursorEnabled>0.5){float2 c=(p.logical-cursorRect.xy)/cursorRect.zw;if(all(c>=0)&&all(c<=1)){float4 cur=cursorTex.Sample(linearSampler,c);base.rgb=lerp(base.rgb,cur.rgb,cur.a);}}
    return float4(base.rgb,1);
})";
struct Vertex {float x,y,z,w,u,v,lu,lv;};
struct Params {float rect[4];float enabled=0,padding[3]{};};
void Graphics::shaders(){
    Com<ID3DBlob> v,p,error;HRESULT hr=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vs","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&v,&error);check(hr,"Compile vertex shader");
    check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"ps","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&p,&error),"Compile pixel shader");
    check(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs_),"Vertex shader");check(device_->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps_),"Pixel shader");
    D3D11_INPUT_ELEMENT_DESC elems[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(device_->CreateInputLayout(elems,3,v->GetBufferPointer(),v->GetBufferSize(),&layout_),"Input layout");
    D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Vertex)*8;b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;check(device_->CreateBuffer(&b,nullptr,&vertices_),"Vertex buffer");
    b.ByteWidth=sizeof(Params);b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device_->CreateBuffer(&b,nullptr,&constants_),"Constants");
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;check(device_->CreateSamplerState(&s,&sampler_),"Sampler");
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;check(device_->CreateRasterizerState(&r,&raster_),"Rasterizer");
    constexpr unsigned w=1024,h=576;std::vector<uint32_t> pixels(w*h);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){bool line=x%64<2||y%64<2;bool cross=std::abs(int(x)-int(w/2))<2||std::abs(int(y)-int(h/2))<2;pixels[y*w+x]=cross?0xff46e6ff:line?0xff8092a4:((x/64+y/64)&1)?0xff1c2936:0xff101923;}
    D3D11_TEXTURE2D_DESC t{};t.Width=w;t.Height=h;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_B8G8R8A8_UNORM;t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_IMMUTABLE;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA d{pixels.data(),w*4,0};check(device_->CreateTexture2D(&t,&d,&checker_),"Grid texture");check(device_->CreateShaderResourceView(checker_.Get(),nullptr,&checkerView_),"Grid view");
}
Graphics::Graphics(HWND window,const Monitor& source,bool warp):window_(window),source_(source){
    check(D3D11CreateDevice(warp?nullptr:source.adapter.Get(),warp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device_,nullptr,&context_),"D3D11 device");
    Com<IDXGIDevice> dx;check(device_.As(&dx),"DXGI device");Com<IDXGIAdapter> adapter;check(dx->GetAdapter(&adapter),"DXGI adapter");Com<IDXGIFactory2> factory;check(adapter->GetParent(IID_PPV_ARGS(&factory)),"DXGI factory2");
    DXGI_SWAP_CHAIN_DESC1 d{};d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=2;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;d.Scaling=DXGI_SCALING_STRETCH;
    check(factory->CreateSwapChainForHwnd(device_.Get(),window,&d,nullptr,nullptr,&swap_),"Swap chain");factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER);
    Com<IDXGIDevice1> dx1;if(SUCCEEDED(device_.As(&dx1)))dx1->SetMaximumFrameLatency(1);
    shaders();if(!warp)duplicate();
}
void Graphics::releaseFrame(){if(frameHeld_&&duplication_)duplication_->ReleaseFrame();frameHeld_=false;}
void Graphics::duplicate(){
    releaseFrame();duplication_.Reset();captureView_.Reset();capture_.Reset();
    Com<IDXGIOutput1> out;HRESULT hr=source_.output.As(&out);if(SUCCEEDED(hr))hr=out->DuplicateOutput(device_.Get(),&duplication_);
    if(FAILED(hr)){stats.capture=L"桌面采集暂不可用；自动重试 ("+std::to_wstring(unsigned(hr))+L")";nextRecovery_=GetTickCount64()+1000;return;}
    DXGI_OUTDUPL_DESC desc{};duplication_->GetDesc(&desc);rotation_=desc.Rotation;
    sourceW_=source_.rect.right-source_.rect.left;sourceH_=source_.rect.bottom-source_.rect.top;
    ++stats.restarts;stats.capture=L"等待桌面图像";
}
void Graphics::acquire(){
    if(!duplication_){if(GetTickCount64()>=nextRecovery_)duplicate();return;}
    releaseFrame();DXGI_OUTDUPL_FRAME_INFO info{};Com<IDXGIResource> resource;HRESULT hr=duplication_->AcquireNextFrame(0,&info,&resource);
    if(hr==DXGI_ERROR_WAIT_TIMEOUT){++stats.timeouts;return;}
    if(FAILED(hr)){duplication_.Reset();nextRecovery_=GetTickCount64()+1000;stats.capture=L"显示模式变化 / 桌面切换；重建采集";return;}
    frameHeld_=true;
    if(info.LastMouseUpdateTime.QuadPart)separateCursor_=bool(info.PointerPosition.Visible);
    try{
        Com<ID3D11Texture2D> tex;check(resource.As(&tex),"Desktop texture");D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);
        if(!capture_){d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=0;d.MiscFlags=0;check(device_->CreateTexture2D(&d,nullptr,&capture_),"Capture texture");check(device_->CreateShaderResourceView(capture_.Get(),nullptr,&captureView_),"Capture view");}
        context_->CopyResource(capture_.Get(),tex.Get());++stats.captured;lastCapture_=GetTickCount64();stats.capture=L"桌面采集中";
        releaseFrame();
    }catch(...){releaseFrame();throw;}
}
void Graphics::updateCursor(){
    CURSORINFO ci{sizeof(ci)};cursorVisible_=GetCursorInfo(&ci)&&ci.flags==CURSOR_SHOWING;
    if(!cursorVisible_)return;
    if(ci.hCursor!=lastCursor_){
        lastCursor_=ci.hCursor;ICONINFO ii{};if(!GetIconInfo(ci.hCursor,&ii)){cursorVisible_=false;return;}hotSpot_={LONG(ii.xHotspot),LONG(ii.yHotspot)};
        BITMAP bm{};GetObjectW(ii.hbmColor?ii.hbmColor:ii.hbmMask,sizeof(bm),&bm);cursorW_=unsigned(bm.bmWidth);cursorH_=unsigned(ii.hbmColor?bm.bmHeight:bm.bmHeight/2);
        if(cursorW_==0||cursorW_>256||cursorH_==0||cursorH_>256){if(ii.hbmColor)DeleteObject(ii.hbmColor);DeleteObject(ii.hbmMask);cursorVisible_=false;return;}
        BITMAPINFO b{};b.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);b.bmiHeader.biWidth=cursorW_;b.bmiHeader.biHeight=-int(cursorH_);b.bmiHeader.biPlanes=1;b.bmiHeader.biBitCount=32;b.bmiHeader.biCompression=BI_RGB;
        HDC dc=CreateCompatibleDC(nullptr);void* bits=nullptr;HBITMAP dib=CreateDIBSection(dc,&b,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,dib);memset(bits,0,cursorW_*cursorH_*4);DrawIconEx(dc,0,0,ci.hCursor,cursorW_,cursorH_,0,nullptr,DI_NORMAL);
        cursorPixels_.assign(static_cast<uint8_t*>(bits),static_cast<uint8_t*>(bits)+cursorW_*cursorH_*4);
        // Legacy monochrome pointers have no alpha channel. Preserve transparent
        // background; XOR-only pixels are approximated white in this first build.
        bool alpha=false;for(size_t i=3;i<cursorPixels_.size();i+=4)alpha|=cursorPixels_[i]!=0;
        if(!alpha)for(size_t i=0;i<cursorPixels_.size();i+=4)if(cursorPixels_[i]||cursorPixels_[i+1]||cursorPixels_[i+2])cursorPixels_[i+3]=255;
        SelectObject(dc,old);DeleteObject(dib);DeleteDC(dc);if(ii.hbmColor)DeleteObject(ii.hbmColor);DeleteObject(ii.hbmMask);
        cursorView_.Reset();cursorTexture_.Reset();D3D11_TEXTURE2D_DESC t{};t.Width=cursorW_;t.Height=cursorH_;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_B8G8R8A8_UNORM;t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_IMMUTABLE;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{cursorPixels_.data(),cursorW_*4,0};check(device_->CreateTexture2D(&t,&data,&cursorTexture_),"Cursor texture");check(device_->CreateShaderResourceView(cursorTexture_.Get(),nullptr,&cursorView_),"Cursor view");
    }
    cursorX_=float(ci.ptScreenPos.x-source_.rect.left-hotSpot_.x);cursorY_=float(ci.ptScreenPos.y-source_.rect.top-hotSpot_.y);
    if(ci.ptScreenPos.x<source_.rect.left||ci.ptScreenPos.x>=source_.rect.right||ci.ptScreenPos.y<source_.rect.top||ci.ptScreenPos.y>=source_.rect.bottom)cursorVisible_=false;
}
void Graphics::render(Quat anchor,Quat head,bool tracked,float scale,float distance,float fov,bool stereo){
    if(!grid_)acquire();updateCursor();
    RECT r{};GetClientRect(window_,&r);unsigned w=r.right,h=r.bottom;if(w==0||h==0)return;
    if(w!=width_||h!=height_){context_->OMSetRenderTargets(0,nullptr,nullptr);target_.Reset();check(swap_->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0),"Resize swap chain");Com<ID3D11Texture2D> back;check(swap_->GetBuffer(0,IID_PPV_ARGS(&back)),"Backbuffer");check(device_->CreateRenderTargetView(back.Get(),nullptr,&target_),"Render target");width_=w;height_=h;}
    float black[4]={0,0,0,1};context_->ClearRenderTargetView(target_.Get(),black);
    if(tracked){
        auto view=grid_?checkerView_:captureView_;
        if(view){
            float aspect=grid_?16.f/9:float(sourceW_)/float(sourceH_);float eyeAspect=float(stereo?w/2:w)/h;
            double tanY=std::tan(fov*pi/360),halfH=2*tanY*.8*scale,halfW=halfH*aspect;
            if(halfW>2*tanY*eyeAspect*.8*scale){halfW=2*tanY*eyeAspect*.8*scale;halfH=halfW/aspect;}
            Vertex verts[8];int eyes=stereo?2:1;
            for(int eye=0;eye<eyes;++eye)for(int i=0;i<4;++i){double sx=(i%2)?1:-1,sy=(i/2)?-1:1;Vec world=anchor.rotate({sx*halfW,sy*halfH,-distance});Vec v=head.inverse().rotate(world);v.x-=stereo?(eye==0?-.0315:.0315):0;double z=-v.z;
                auto uv=desktopUv(float(i%2),float(i/2),grid_?1:rotation_);verts[eye*4+i]={float(v.x/(tanY*eyeAspect)),float(v.y/tanY),float(z*100/(100-.05)-.05*100/(100-.05)),float(z),uv[0],uv[1],float(i%2),float(i/2)};
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};check(context_->Map(vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map vertices");memcpy(mapped.pData,verts,sizeof(verts));context_->Unmap(vertices_.Get(),0);
            Params p{};
            // Cursor uses logical upright screen coordinates independently of
            // the rotated DXGI desktop texture. Never duplicate a baked-in pointer.
            if(!grid_&&separateCursor_&&cursorVisible_&&cursorView_){p.rect[0]=cursorX_/sourceW_;p.rect[1]=cursorY_/sourceH_;p.rect[2]=float(cursorW_)/sourceW_;p.rect[3]=float(cursorH_)/sourceH_;p.enabled=1;}
            check(context_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map constants");memcpy(mapped.pData,&p,sizeof(p));context_->Unmap(constants_.Get(),0);
            UINT stride=sizeof(Vertex),offset=0;ID3D11Buffer* vb=vertices_.Get();context_->IASetVertexBuffers(0,1,&vb,&stride,&offset);context_->IASetInputLayout(layout_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);ID3D11ShaderResourceView* srvs[]={view.Get(),cursorView_.Get()};context_->PSSetShaderResources(0,2,srvs);auto sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);auto cb=constants_.Get();context_->PSSetConstantBuffers(0,1,&cb);auto rt=target_.Get();context_->OMSetRenderTargets(1,&rt,nullptr);context_->RSSetState(raster_.Get());
            for(int eye=0;eye<eyes;++eye){D3D11_VIEWPORT vp{float(eye*(w/eyes)),0,float(w/eyes),float(h),0,1};context_->RSSetViewports(1,&vp);context_->Draw(4,eye*4);}
            ID3D11ShaderResourceView* empty[2]{};context_->PSSetShaderResources(0,2,empty);
        }
    }
    if(!screenshot_.empty()){saveFrame(screenshot_);screenshot_.clear();}
    HRESULT hr=swap_->Present(1,0);check(hr,"Present");if(hr==DXGI_STATUS_OCCLUDED)Sleep(30);
    ++stats.rendered;stats.lastCaptureAge=lastCapture_?double(GetTickCount64()-lastCapture_)/1000:-1;
}
void Graphics::saveFrame(const std::wstring& path){
    Com<ID3D11Texture2D> back,staging;check(swap_->GetBuffer(0,IID_PPV_ARGS(&back)),"Screenshot buffer");D3D11_TEXTURE2D_DESC t{};back->GetDesc(&t);t.Usage=D3D11_USAGE_STAGING;t.BindFlags=0;t.MiscFlags=0;t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device_->CreateTexture2D(&t,nullptr,&staging),"Staging");context_->CopyResource(staging.Get(),back.Get());
    D3D11_MAPPED_SUBRESOURCE data{};check(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&data),"Readback");
    try{Com<IWICImagingFactory> wic;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC");Com<IWICStream> stream;check(wic->CreateStream(&stream),"WIC stream");check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"PNG path");Com<IWICBitmapEncoder> encoder;check(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"PNG encoder");check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"PNG init");Com<IWICBitmapFrameEncode> frame;check(encoder->CreateNewFrame(&frame,nullptr),"PNG frame");check(frame->Initialize(nullptr),"PNG frame init");check(frame->SetSize(t.Width,t.Height),"PNG size");auto format=GUID_WICPixelFormat32bppBGRA;check(frame->SetPixelFormat(&format),"PNG format");check(frame->WritePixels(t.Height,data.RowPitch,data.RowPitch*t.Height,static_cast<BYTE*>(data.pData)),"PNG pixels");check(frame->Commit(),"PNG frame commit");check(encoder->Commit(),"PNG commit");}catch(...){context_->Unmap(staging.Get(),0);throw;}
    context_->Unmap(staging.Get(),0);
}
}
