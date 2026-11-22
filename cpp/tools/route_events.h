#pragma once
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include "core/json.h"
namespace cs2n {
inline bool routeKey(USHORT v){
 return v=='W'||v=='A'||v=='S'||v=='D'||v==VK_SPACE||v==VK_CONTROL||v==VK_SHIFT||
 v=='R'||v=='E'||v=='Q'||v=='G'||v=='F'||v=='B'||(v>='1'&&v<='5');
}
// Raw Input is collected on its own message thread, independently of memory polling.
// Only the explicitly selected game's foreground input is retained. No text capture.
class RouteEvents {
 using Clock=std::chrono::steady_clock;
 DWORD pid_;Clock::time_point epoch_;std::thread thread_;std::atomic<HWND> window_{nullptr};
 std::mutex mutex_;std::vector<JsonValue> queue_;bool active_=false,finished_=false;
 std::atomic<bool> failed_{false},finishedSignal_{false};
 bool timerPeriod_=false; Clock::time_point idleSince_{}; std::atomic<bool> started_{false};
 void emit(const char* kind,JsonValue j=JsonValue::makeObject()){
  j.set("type",JsonValue::makeString("input"));j.set("kind",JsonValue::makeString(kind));
  j.set("t",JsonValue::makeNumber(std::chrono::duration<double>(Clock::now()-epoch_).count()));
  std::lock_guard<std::mutex> lock(mutex_);if(queue_.size()>=65536){failed_=true;return;}queue_.push_back(std::move(j));
 }
 bool focused(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==pid_;}
 bool released(){for(int v=1;v<256;++v)if((routeKey(USHORT(v))||v==VK_LBUTTON||v==VK_RBUTTON||v==VK_MBUTTON||v==VK_XBUTTON1||v==VK_XBUTTON2)&&(GetAsyncKeyState(v)&0x8000))return false;return true;}
 void checkFocus(){if(finished_)return;if(active_&&!focused()){emit("focus_end");finished_=true;finishedSignal_=true;}}
 static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){
  auto self=reinterpret_cast<RouteEvents*>(GetWindowLongPtrW(w,GWLP_USERDATA));
  if(m==WM_NCCREATE){self=static_cast<RouteEvents*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self&&m==WM_TIMER){self->checkFocus();if(!self->active_&&!self->finished_&&self->focused()){
   self->active_=true;self->started_=true;self->emit("begin");
   for(int v=1;v<256;++v)if(routeKey(USHORT(v))&&(GetAsyncKeyState(v)&0x8000)){
    UINT scan=MapVirtualKeyW(v,MAPVK_VK_TO_VSC_EX);auto j=JsonValue::makeObject();j.set("scan",JsonValue::makeNumber(scan&255));j.set("extended",JsonValue::makeBool((scan&0xff00)==0xe000));j.set("up",JsonValue::makeBool(false));j.set("initial",JsonValue::makeBool(true));self->emit("key",j);
   }
   int flags=0;const int buttons[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};for(int i=0;i<5;i++)if(GetAsyncKeyState(buttons[i])&0x8000)flags|=1<<(2*i);
   if(flags){auto j=JsonValue::makeObject();j.set("dx",JsonValue::makeNumber(0));j.set("dy",JsonValue::makeNumber(0));j.set("buttons",JsonValue::makeNumber(flags));j.set("wheel",JsonValue::makeNumber(0));j.set("initial",JsonValue::makeBool(true));self->emit("mouse",j);}
   MessageBeep(MB_OK);
  }return 0;}
  if(self&&m==WM_APP+1){if(!self->active_&&!self->finished_&&self->focused()&&self->released()){self->active_=true;self->started_=true;self->emit("begin");}return 0;}
  if(self&&m==WM_INPUT){self->checkFocus();if(self->active_&&!self->finished_&&self->focused()){
   RAWINPUT raw{};UINT size=sizeof(raw);
   if(GetRawInputData(reinterpret_cast<HRAWINPUT>(b),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))==UINT(-1)){self->failed_=true;}
   else if(raw.header.dwType==RIM_TYPEKEYBOARD){auto k=raw.data.keyboard;
    if(k.VKey==VK_F10&&!(k.Flags&RI_KEY_BREAK))self->finished_=true;
    if(routeKey(k.VKey)){if(!k.MakeCode||(k.Flags&RI_KEY_E1)){self->failed_=true;}else{auto j=JsonValue::makeObject();j.set("scan",JsonValue::makeNumber(k.MakeCode));j.set("extended",JsonValue::makeBool((k.Flags&RI_KEY_E0)!=0));j.set("up",JsonValue::makeBool((k.Flags&RI_KEY_BREAK)!=0));self->emit("key",j);}}
    else if(k.VKey!=VK_F7&&k.VKey!=VK_F8&&k.VKey!=VK_F9&&k.VKey!=VK_F10)self->failed_=true;
   }else if(raw.header.dwType==RIM_TYPEMOUSE){auto r=raw.data.mouse;if(r.usFlags&MOUSE_MOVE_ABSOLUTE){self->failed_=true;}else if(r.lLastX||r.lLastY||r.usButtonFlags){auto j=JsonValue::makeObject();j.set("dx",JsonValue::makeNumber(r.lLastX));j.set("dy",JsonValue::makeNumber(r.lLastY));j.set("buttons",JsonValue::makeNumber(r.usButtonFlags));j.set("wheel",JsonValue::makeNumber(static_cast<SHORT>(r.usButtonData)));self->emit("mouse",j);}}
  }}
  if(m==WM_CLOSE){DestroyWindow(w);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
  return DefWindowProcW(w,m,a,b);
 }
 public:
 RouteEvents(DWORD pid,Clock::time_point epoch):pid_(pid),epoch_(epoch){
  std::promise<bool> promise;auto future=promise.get_future();
  thread_=std::thread([this,p=std::move(promise)]()mutable{
   WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"CS2RouteRawInput";
   RegisterClassW(&cls);HWND w=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,this);
   RAWINPUTDEVICE devices[]={{1,2,RIDEV_INPUTSINK,w},{1,6,RIDEV_INPUTSINK,w}};
   if(!w||!RegisterRawInputDevices(devices,2,sizeof(RAWINPUTDEVICE))){if(w)DestroyWindow(w);p.set_value(false);return;}
   window_=w;SetTimer(w,1,10,nullptr);p.set_value(true);MSG msg{};
   while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
   window_=nullptr;
  });
  if(!future.get()){thread_.join();throw std::runtime_error("Raw Input registration failed");}
  timerPeriod_=timeBeginPeriod(1)==TIMERR_NOERROR;
 }
 ~RouteEvents(){stop();if(timerPeriod_)timeEndPeriod(1);}
 void arm(){if(auto w=window_.load())PostMessageW(w,WM_APP+1,0,0);}
 void stop(){if(auto w=window_.load())PostMessageW(w,WM_CLOSE,0,0);if(thread_.joinable())thread_.join();}
 bool finished()const{return finishedSignal_.load();} bool failed()const{return failed_.load()||!started_.load();}
 std::vector<JsonValue> drain(){std::lock_guard<std::mutex> lock(mutex_);std::vector<JsonValue> result;result.swap(queue_);return result;}
};
}

