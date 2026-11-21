#pragma once
#include <map>
#include <functional>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")
#include "route_events.h"
struct EventTape {std::vector<Frame> frames;std::vector<JsonValue> events;double begin=0,end=0;std::string mapName;};
inline EventTape loadTape(const fs::path& path){
 EventTape tape;std::ifstream file(path);std::string line,error;bool began=false,ended=false,valid=false; double focusEnd=-1;
 std::vector<Frame> poses;std::vector<double> speeds;
 while(std::getline(file,line)){
  JsonValue j;if(!JsonValue::parse(line,j,error))throw std::runtime_error("Invalid recording JSON");
  auto type=j["type"].asString();
  if(type=="input"){
   if(ended)throw std::runtime_error("Input after recording end");double t=numeric(j["t"]);
   auto kind=j["kind"].asString();
   if(kind=="begin"){if(began)throw std::runtime_error("Multiple starts");began=true;tape.begin=t;continue;}
   if(!began)throw std::runtime_error("Missing input start"); if(kind=="focus_lost"||kind=="focus_end"){focusEnd=t;continue;} if(focusEnd>=0)throw std::runtime_error("Input after focus loss");
   if(t<tape.begin||(!tape.events.empty()&&t<tape.events.back()["t"].asNumber()))throw std::runtime_error("Input timestamps reversed");
   if(kind=="key"){
    double scan=numeric(j["scan"]);if(scan<1||scan>255||scan!=int(scan)||!j["up"].isBool()||!j["extended"].isBool())throw std::runtime_error("Invalid key event");
    UINT code=UINT(scan)|(j["extended"].asBool()?0xe000:0);UINT vk=MapVirtualKeyW(code,MAPVK_VSC_TO_VK_EX);if(vk==VK_LSHIFT||vk==VK_RSHIFT)vk=VK_SHIFT;if(vk==VK_LCONTROL||vk==VK_RCONTROL)vk=VK_CONTROL;
    if(!cs2n::routeKey(USHORT(vk)))throw std::runtime_error("Key outside gameplay allowlist");
   }else if(kind=="mouse"){
    for(const char* k:{"dx","dy","buttons","wheel"}){double n=numeric(j[k]);if(n!=int(n)||std::abs(n)>32767)throw std::runtime_error("Invalid mouse field");}
    if(j["buttons"].asInt()<0||(j["buttons"].asInt()&~0xfff))throw std::runtime_error("Unsupported mouse flags");
   }else throw std::runtime_error("Unknown input event");
   tape.events.push_back(j);if(tape.events.size()>2000000)throw std::runtime_error("Recording too large");
  }else if(type=="sample"){ if(focusEnd>=0&&j["t"].asNumber()>=focusEnd)continue;
   if(j["map"].isString()&&!j["map"].asString().empty()){auto map=j["map"].asString();if(!tape.mapName.empty()&&map!=tape.mapName)throw std::runtime_error("Map changed during recording");tape.mapName=map;}
   if(j["position"].size()!=3||!j["valid"].asBool()||!j["alive"].asBool()){if(began)throw std::runtime_error("Lost player/death during recording");continue;}
   Frame f{numeric(j["t"]),numeric(j["yaw"]),numeric(j["pitch"]),{numeric(j["position"].at(0)),numeric(j["position"].at(1)),numeric(j["position"].at(2))},j["action_states"]};
   if(f.t<0||std::abs(f.pitch)>1.57||std::abs(f.yaw)>6.29)throw std::runtime_error("Invalid pose values");
   if(!poses.empty()&&f.t<=poses.back().t)throw std::runtime_error("Pose timestamps reversed");poses.push_back(f);
   if(j["velocity"].size()!=3)throw std::runtime_error("Missing velocity");
   Vec3 velocity{numeric(j["velocity"].at(0)),numeric(j["velocity"].at(1)),numeric(j["velocity"].at(2))};speeds.push_back(velocity.length());
  }else if(type=="end"){ended=true;valid=j["input_valid"].asBool();}
 }
 if(focusEnd>=0&&!valid){std::cout<<"RECOVERED PREFIX: legacy validity reason unknown; only structural checks confirmed. Original unchanged.\n";throw std::runtime_error("Legacy invalid recording: inspect and recover explicitly, not automatic replay");}
 if(!began||!ended||!valid||tape.events.empty()||poses.size()<2)throw std::runtime_error("Need a new F7/F10 event recording; legacy samples are not exact input");
 std::size_t start=0;while(start+1<poses.size()&&poses[start+1].t<=tape.begin)++start;
 if(poses[start].t>tape.begin||tape.begin-poses[start].t>.05)throw std::runtime_error("No fresh pose at event start");
 if(speeds[start]>3)throw std::runtime_error("Recording started while moving; stand still before F7");
 tape.frames.assign(poses.begin()+start,poses.end());tape.frames.front().t=tape.begin;tape.end=tape.frames.back().t;
 if(tape.end+.05<tape.events.back()["t"].asNumber()||tape.end-tape.begin>600)throw std::runtime_error("Missing final pose or route too long");
 tape.end=std::max(tape.end,tape.events.back()["t"].asNumber());
 for(std::size_t i=1;i<tape.frames.size();++i)if(tape.frames[i].t-tape.frames[i-1].t>.15||(tape.frames[i].position-tape.frames[i-1].position).length()>100)throw std::runtime_error("Pose gap: cannot verify replay trajectory");
 return tape;
}
class PhysicalReplay {
 std::map<unsigned,INPUT> held_;DWORD mouseHeld_=0;
 void send(INPUT i){if(SendInput(1,&i,sizeof(i))!=1)throw std::runtime_error("SendInput failed");}
 void mouse(DWORD flags,LONG dx=0,LONG dy=0,DWORD data=0){INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=flags;i.mi.dx=dx;i.mi.dy=dy;i.mi.mouseData=data;send(i);}
 public:
 ~PhysicalReplay(){release();}
 void release() noexcept {
  for(auto& [key,i]:held_){i.ki.dwFlags|=KEYEVENTF_KEYUP;SendInput(1,&i,sizeof(i));}held_.clear();
  for(auto pair:{std::pair<DWORD,DWORD>{1,MOUSEEVENTF_LEFTUP},{2,MOUSEEVENTF_RIGHTUP},{4,MOUSEEVENTF_MIDDLEUP},{8,MOUSEEVENTF_XUP},{16,MOUSEEVENTF_XUP}})if(mouseHeld_&pair.first){INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=pair.second;i.mi.mouseData=pair.first==8?XBUTTON1:pair.first==16?XBUTTON2:0;SendInput(1,&i,sizeof(i));}mouseHeld_=0;
 }
 void apply(const JsonValue& e){
  if(e["kind"].asString()=="key"){
   INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wScan=WORD(e["scan"].asInt());i.ki.dwFlags=KEYEVENTF_SCANCODE|(e["extended"].asBool()?KEYEVENTF_EXTENDEDKEY:0)|(e["up"].asBool()?KEYEVENTF_KEYUP:0);
   unsigned key=i.ki.wScan|(e["extended"].asBool()?0x100:0);if(!e["up"].asBool())held_[key]=i;send(i);if(e["up"].asBool())held_.erase(key);
  }else{
   auto flags=USHORT(e["buttons"].asInt());LONG dx=LONG(e["dx"].asInt()),dy=LONG(e["dy"].asInt());
   if(dx||dy)mouse(MOUSEEVENTF_MOVE|MOUSEEVENTF_MOVE_NOCOALESCE,dx,dy);
   const DWORD down[]={MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_RIGHTDOWN,MOUSEEVENTF_MIDDLEDOWN,MOUSEEVENTF_XDOWN,MOUSEEVENTF_XDOWN};
   const DWORD up[]={MOUSEEVENTF_LEFTUP,MOUSEEVENTF_RIGHTUP,MOUSEEVENTF_MIDDLEUP,MOUSEEVENTF_XUP,MOUSEEVENTF_XUP};
   for(int b=0;b<5;++b){DWORD data=b==3?XBUTTON1:b==4?XBUTTON2:0;if(flags&(1<<(2*b))){mouseHeld_|=1<<b;mouse(down[b],0,0,data);}if(flags&(2<<(2*b))){mouse(up[b],0,0,data);mouseHeld_&=~(1<<b);}}
   if(flags&RI_MOUSE_WHEEL)mouse(MOUSEEVENTF_WHEEL,0,0,DWORD(LONG(e["wheel"].asInt())));
   if(flags&RI_MOUSE_HWHEEL)mouse(MOUSEEVENTF_HWHEEL,0,0,DWORD(LONG(e["wheel"].asInt())));
  }
 }
};
inline void replayEvents(const EventTape& tape,DWORD pid,const std::function<PlayerState()>& read){
 using Clock=std::chrono::steady_clock;
 struct TimerPeriod{bool active=timeBeginPeriod(1)==TIMERR_NOERROR;~TimerPeriod(){if(active)timeEndPeriod(1);}} timerPeriod;
 DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);if(foreground!=pid)throw std::runtime_error("Activate CS2 before replay");
 for(int k=1;k<256;++k)if((cs2n::routeKey(USHORT(k))||k==VK_LBUTTON||k==VK_RBUTTON||k==VK_MBUTTON||k==VK_XBUTTON1||k==VK_XBUTTON2)&&(GetAsyncKeyState(k)&0x8000))throw std::runtime_error("Release game keys before replay");
 PhysicalReplay output;auto start=Clock::now(),nextCheck=start;std::size_t index=0,pose=0;double maxDrift=0,maxLate=0;
 std::mutex stateMutex;PlayerState latest=read();auto updated=Clock::now();std::exception_ptr readError;
 std::jthread observer([&](std::stop_token stop){while(!stop.stop_requested()){
  try{auto state=read();std::lock_guard<std::mutex> lock(stateMutex);latest=state;updated=Clock::now();}
  catch(...){std::lock_guard<std::mutex> lock(stateMutex);readError=std::current_exception();return;}
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }});
 start=Clock::now();nextCheck=start;
 while(true){auto now=Clock::now();double t=std::chrono::duration<double>(now-start).count()+tape.begin;if(t>tape.end)break;
  GetWindowThreadProcessId(GetForegroundWindow(),&foreground);if(foreground!=pid||cancelled||(GetAsyncKeyState(VK_F10)&0x8000)||(GetAsyncKeyState(VK_ESCAPE)&0x8000))throw std::runtime_error("Replay stopped: focus/user");
  if(now>=nextCheck){PlayerState s;{std::lock_guard<std::mutex> lock(stateMutex);if(readError)std::rethrow_exception(readError);if(now-updated>std::chrono::milliseconds(150))throw std::runtime_error("Telemetry stale during replay");s=latest;}while(pose+1<tape.frames.size()&&tape.frames[pose+1].t<=t)++pose;
   const auto& a=tape.frames[pose];Vec3 expected=a.position;
   if(pose+1<tape.frames.size()){const auto& b=tape.frames[pose+1];double u=clamp((t-a.t)/(b.t-a.t),0.,1.);expected={a.position.x+(b.position.x-a.position.x)*u,a.position.y+(b.position.y-a.position.y)*u,a.position.z+(b.position.z-a.position.z)*u};}
   double drift=(s.position-expected).length();maxDrift=std::max(maxDrift,drift);if(drift>48)throw std::runtime_error("Replay diverged >48 units; no automatic correction");nextCheck=Clock::now()+std::chrono::milliseconds(10);
  }
  now=Clock::now();t=std::chrono::duration<double>(now-start).count()+tape.begin;
  while(index<tape.events.size()&&tape.events[index]["t"].asNumber()<=t){double late=t-tape.events[index]["t"].asNumber();maxLate=std::max(maxLate,late);if(late>.025)throw std::runtime_error("Input scheduling delay >25ms; aborted instead of bursting events");output.apply(tape.events[index++]);}
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 if(index!=tape.events.size())throw std::runtime_error("Replay ended before all events");output.release();
 std::ofstream("route-replay.log",std::ios::app)<<"EVENTS finished count="<<index<<" max_drift="<<maxDrift<<" max_lateness_ms="<<maxLate*1000<<'\n';
}

