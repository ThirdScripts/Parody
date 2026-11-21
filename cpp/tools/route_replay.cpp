// Bounded local route experiment. Replays only explicitly recorded gameplay input.
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include "movement/memory_input.h"
#include "movement/move_controller.h"
#include "navigation/navmesh_io.h"
#include "navigation/navigator.h"
using namespace cs2n;
namespace fs=std::filesystem;
std::atomic<bool> cancelled=false;
HANDLE routeStopEvent=nullptr;
BOOL WINAPI cancelRoute(DWORD){cancelled=true;return TRUE;}
Vec3 operator-(const Vec3& a,const Vec3& b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
struct Frame {double t,yaw,pitch;Vec3 position;JsonValue actions;};
double numeric(const JsonValue& j){if(!j.isNumber()||!std::isfinite(j.asNumber()))throw std::runtime_error("Invalid numeric field");return j.asNumber();}
#include "route_playback.h"
int wmain(int argc,wchar_t** argv){
 try{
  if(argc!=4||(std::wstring(argv[1])!=L"--check"&&std::wstring(argv[1])!=L"--approach"&&std::wstring(argv[1])!=L"--replay"&&std::wstring(argv[1])!=L"--validate")||std::wstring(argv[3])!=L"de_mirage"){
   std::cout<<"route_replay --check|--approach|--replay|--validate recording.jsonl de_mirage\n";return 2;
  }
  auto record=fs::absolute(argv[2]);auto mode=std::wstring(argv[1]);
  auto tape=loadTape(record);auto frames=tape.frames;
  if(mode==L"--validate"){std::cout<<"VALID events="<<tape.events.size()<<" poses="<<frames.size()<<std::endl;return 0;}
  bool run=mode!=L"--check";
  if(!tape.mapName.empty()&&tape.mapName!="de_mirage")throw std::runtime_error("This executor supports Mirage only; recording belongs to "+tape.mapName);
  wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);fs::current_path(fs::path(exe).parent_path());
  std::ofstream log("route-replay.log",std::ios::app);auto report=[&](const std::string& s){std::cout<<s<<std::endl;log<<s<<std::endl;};
  NavMesh mesh;std::string error;if(!NavMeshIo::loadFile("assets/mirage.nav.json",mesh,error,false))throw std::runtime_error(error);
  Cs2MemoryProvider memory("assets/memory.json",false);auto p=memory.poll();if(!p||!p->alive)throw std::runtime_error("No compatible live player: "+memory.error());
  if(!p->mapName.empty()&&p->mapName!="de_mirage")throw std::runtime_error("Current map is not Mirage");
  Navigator nav(&mesh);Path approach;
  if(!nav.planTo(p->position.xy(),p->position.z,frames[0].position.xy(),frames[0].position.z,approach,error))throw std::runtime_error("Cannot reach route start: "+error);
  for(auto& point:approach.points)if(point.traversalFlags&(ConnJump|ConnLadder))throw std::runtime_error("Approach needs jump/ladder: not supported in this trial");
  report("CHECK samples="+std::to_string(frames.size())+" seconds="+std::to_string(frames.back().t)+" approach_points="+std::to_string(approach.size())+" approach_distance="+std::to_string(approach.length()));
  if(!run)return 0;
  HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\CS2RouteReplay");if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS){if(mutex)CloseHandle(mutex);throw std::runtime_error("Another replay is running");}
  struct Guard{HANDLE h;~Guard(){ReleaseMutex(h);CloseHandle(h);}} guard{mutex};
  SetConsoleCtrlHandler(cancelRoute,TRUE);
  routeStopEvent=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\CS2RouteStop-"+std::to_wstring(GetCurrentProcessId())).c_str());
  struct StopGuard{~StopGuard(){if(routeStopEvent)CloseHandle(routeStopEvent);routeStopEvent=nullptr;}} stopGuard;
  MemoryInput input(memory.target(),true);
  auto read=[&](){if(cancelled||(routeStopEvent&&WaitForSingleObject(routeStopEvent,0)==WAIT_OBJECT_0)||input.stopRequested()||(GetAsyncKeyState(VK_F10)&0x8000))throw std::runtime_error("Stopped by user");auto s=memory.poll();if(!s||!s->alive||!input.ready())throw std::runtime_error("Player unavailable/dead or input invalid: "+input.error());if(!s->mapName.empty()&&s->mapName!="de_mirage")throw std::runtime_error("Map changed");return *s;};
  auto aim=[&](const PlayerState& s,double yaw,double pitch){input.setTurnAxis(clamp(wrapAngle(yaw-s.yaw)*180/kPi*4/400,-.45,.45));input.setTurnAxis(clamp((pitch-s.pitch)*180/kPi*4/60,-.6,.6),true);};
  using Clock=std::chrono::steady_clock;auto seconds=[](auto since){return std::chrono::duration<double>(Clock::now()-since).count();};
  MoveConfig cfg;cfg.arriveRadius=4;MoveController move(cfg);move.setMesh(&mesh);
  auto begin=Clock::now(),progressAt=begin;Vec3 progress=p->position;
  report(mode==L"--approach"?"APPROACH started; F10 stops":"REPLAY start pose check");
  if(mode==L"--replay"&&(p->position-frames[0].position).length()>8)throw std::runtime_error("Not at start: use approach first");
  while(mode==L"--approach"){auto s=read();auto delta=s.position-frames[0].position;
   if(delta.xy().length()<=4&&std::abs(delta.z)<=6){input.releaseAll();if(s.velocity.length()<3)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
   if(seconds(begin)>120)throw std::runtime_error("Approach timeout");
   if((s.position-progress).length()>12){progress=s.position;progressAt=Clock::now();}
   if(seconds(progressAt)>6)throw std::runtime_error("Approach stuck; released input");
   auto intent=move.evaluate(approach,s.position.xy(),s.yaw,s.position.z);
   bool step=delta.xy().length()>40||std::fmod(seconds(begin),.15)<.03;
   aim(s,intent.aimYaw,frames[0].pitch);input.setWalk(intent.forward&&step);input.setBackward(intent.backward&&step);input.setStrafe(step?intent.strafe:0);input.setCrouch(intent.crouch);input.setRecordedSprint(delta.xy().length()<100);
   std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  input.releaseAll();begin=Clock::now();
  while(true){auto s=read();if((s.position-frames[0].position).length()>8)throw std::runtime_error("Drift away from start while aligning");
   aim(s,frames[0].yaw,frames[0].pitch);
   if(std::abs(wrapAngle(s.yaw-frames[0].yaw))<.01&&std::abs(s.pitch-frames[0].pitch)<.01&&s.velocity.length()<3)break;
   if(seconds(begin)>8)throw std::runtime_error("Start alignment timeout");std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  input.releaseAll();if(mode==L"--approach"){report("AT START: position and view aligned; input released");return 0;}
  report("Activate CS2 and release keys within 5 seconds; F10 cancels");
  for(int i=0;i<50;++i){read();std::this_thread::sleep_for(std::chrono::milliseconds(100));}
  auto s=read();if((s.position-frames[0].position).length()>8||std::abs(wrapAngle(s.yaw-frames[0].yaw))>.01||std::abs(s.pitch-frames[0].pitch)>.01)throw std::runtime_error("Start pose changed during countdown");
  replayEvents(tape,memory.processId(),read);report("FINISHED event replay");return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;std::ofstream("route-replay.log",std::ios::app)<<"STOP "<<e.what()<<std::endl;return 1;}
}
