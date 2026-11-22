// Read-only demonstration recorder. Never injects input or modifies CS2.
#include <Windows.h>
#include <TlHelp32.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <regex>
#include <sstream>
#include <map>
#include "state/cs2_memory.h"
#include "route_events.h"
#include "state/gsi_provider.h"
using namespace cs2n;
namespace fs=std::filesystem;
std::atomic<bool> recording{true};
BOOL WINAPI stopHandler(DWORD){recording=false;return TRUE;}
JsonValue number(double n){return std::isfinite(n)?JsonValue::makeNumber(n):JsonValue::makeNull();}
JsonValue vec(Vec3 v){auto a=JsonValue::makeArray();a.append(number(v.x));a.append(number(v.y));a.append(number(v.z));return a;}
int run(int argc,wchar_t** argv){
 SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);SetConsoleTitleW(L"Пародия — запись игры");
 if(argc==2&&std::wstring(argv[1])==L"--help"){
  std::cout<<"Parody: read-only CS2 recorder. Run without arguments, select PID. Optional: <GSI cfg> <expected SteamID>. F8 manual round, F9 bookmark, F10 stop.\n";return 0;
 }
 if(argc!=1&&argc!=3)return 2;
 wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);const auto home=fs::path(exe).parent_path();
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return 2;
 PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);std::vector<DWORD> choices;
 if(Process32FirstW(snapshot,&entry))do{if(_wcsicmp(entry.szExeFile,L"cs2.exe")==0){choices.push_back(entry.th32ProcessID);}}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);
 if(choices.empty()){std::cerr<<"Сначала запусти CS2, затем открой Пародию снова.\n";return 2;}
 if(choices.size()!=1){std::cerr<<"Оставь запущенной только одну CS2.\n";return 2;}
 DWORD pid=choices.front();
 HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid);if(!process)return 3;
 ProcessTarget target{pid,processCreation(process)};
 wchar_t name[32768]{};DWORD size=32768;
 if(!target.created||!QueryFullProcessImageNameW(process,0,name,&size)||_wcsicmp(fs::path(name).filename().c_str(),L"cs2.exe")!=0){CloseHandle(process);return 3;}
 const auto profileUtf8=(home/"assets/memory.json").u8string();
 Cs2MemoryProvider memory(std::string(profileUtf8.begin(),profileUtf8.end()),false,target);
 if(!memory.poll()){std::cerr<<"Profile unavailable: "<<memory.error()<<'\n';CloseHandle(process);return 4;}
 auto loadProfile=[&](const char* filename){std::ifstream input(home/"assets"/filename);std::ostringstream text;text<<input.rdbuf();JsonValue value;std::string error;if(!JsonValue::parse(text.str(),value,error))throw std::runtime_error("Invalid recorder profile");return value;};
 const auto localProfile=loadProfile("memory.json"),buttonProfile=loadProfile("input-buttons.json");
 if(buttonProfile["clientSha256"].asString().empty()||buttonProfile["clientSha256"].asString()!=localProfile["clientSha256"].asString()){CloseHandle(process);return 4;}
 std::map<std::string,std::uintptr_t> actions;
 for(const char* action:{"forward","back","left","right","duck","jump","sprint"}){
   const auto rva=buttonProfile["buttons"][action]["rva"].asInt();
   if(rva<0x30||rva+4>localProfile["moduleSize"].asInt()){CloseHandle(process);return 4;}
   const auto address=memory.clientBase()+rva;std::uintptr_t pointer=0;char name[32]{};SIZE_T bytes=0;
   if(!ReadProcessMemory(process,reinterpret_cast<void*>(address-0x28),&pointer,8,&bytes)||bytes!=8||
      !ReadProcessMemory(process,reinterpret_cast<void*>(pointer),name,sizeof(name),&bytes)||bytes!=sizeof(name)||
      std::string(name,strnlen_s(name,sizeof(name)))!=action){CloseHandle(process);return 4;}
   actions[action]=address;
 }
 std::unique_ptr<GsiProvider> gsi;
 if(argc==1){
  auto cfgDir=fs::path(name).parent_path().parent_path().parent_path()/"csgo"/"cfg";
  std::vector<fs::path> configs;std::error_code ec;
  for(const auto& item:fs::directory_iterator(cfgDir,ec))if(item.path().filename().wstring().starts_with(L"gamestate_integration")&&item.path().extension()==L".cfg")configs.push_back(item.path());
  if(configs.size()==1){std::ifstream f(configs[0]);std::ostringstream content;content<<f.rdbuf();auto text=content.str();std::smatch uri,token;
   if(std::regex_search(text,uri,std::regex("http://127\\.0\\.0\\.1:([0-9]+)"))&&std::regex_search(text,token,std::regex("\"token\"\\s*\"([^\"]+)\""))){
    auto port=std::stoul(uri[1]);if(port>0&&port<=65535){GsiConfig c;c.port=static_cast<uint16_t>(port);c.token=token[1];c.requiredMap.clear();gsi=std::make_unique<GsiProvider>(c);if(!gsi->listening()){gsi.reset();std::cout<<"Карта неизвестна: GSI-порт занят.\n";}}
   }
  }
  if(!gsi)std::cout<<"Автоопределение карты недоступно: нет единственного свободного GSI endpoint.\n";
 }
 if(argc==3){
  std::ifstream cfg{fs::path(argv[1])};std::ostringstream content;content<<cfg.rdbuf();std::smatch uri,token;auto text=content.str();
  if(!cfg||!std::regex_search(text,uri,std::regex("\"uri\"\\s*\"http://127\\.0\\.0\\.1:([0-9]+)/?\""))||!std::regex_search(text,token,std::regex("\"token\"\\s*\"([^\"]+)\""))){CloseHandle(process);return 5;}
  unsigned long port=0;try{port=std::stoul(uri[1]);}catch(...){CloseHandle(process);return 5;}
  if(!port||port>65535){CloseHandle(process);return 5;}
  GsiConfig config;config.port=static_cast<uint16_t>(port);config.token=token[1];config.requiredMap.clear();
  auto id=std::wstring(argv[2]);if(id.empty()||id.find_first_not_of(L"0123456789")!=std::wstring::npos){CloseHandle(process);return 5;}
  config.expectedSteamId=std::string(id.begin(),id.end());gsi=std::make_unique<GsiProvider>(config);
  if(!gsi->listening()){std::cerr<<"GSI port busy. Close navigator first.\n";CloseHandle(process);return 5;}
 }
 std::string map="unknown";
 std::cout<<"CS2 найдена. Вернись в игру и играй как обычно.\n"
          <<"F10 — закончить запись. F9 — отметить интересный момент.\n"
          <<"Окно можно свернуть, но не закрывай его во время записи.\n";
 // Hotkeys are scoped to this recorder; no keyboard hook or arbitrary text logging.
 if(!RegisterHotKey(nullptr,1,MOD_NOREPEAT,VK_F10)){CloseHandle(process);return 6;}
 if(!RegisterHotKey(nullptr,2,MOD_NOREPEAT,VK_F9)||!RegisterHotKey(nullptr,3,MOD_NOREPEAT,VK_F8)){UnregisterHotKey(nullptr,1);UnregisterHotKey(nullptr,2);UnregisterHotKey(nullptr,3);CloseHandle(process);return 6;}
 if(!RegisterHotKey(nullptr,4,MOD_NOREPEAT,VK_F7)){for(int id=1;id<=3;++id)UnregisterHotKey(nullptr,id);CloseHandle(process);return 6;}
 SetConsoleCtrlHandler(stopHandler,TRUE);
 const auto utc=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 fs::create_directories(home/"recordings");const auto path=home/"recordings"/("parody-"+std::to_string(utc)+"-"+std::to_string(GetCurrentProcessId())+".jsonl");
 std::ofstream file(path);int result=0;
 if(!file){for(int id=1;id<=3;++id)UnregisterHotKey(nullptr,id);CloseHandle(process);std::cerr<<"Не удалось создать файл записи.\n";return 7;}
 std::cout<<"ЗАПИСЬ ИДЁТ. Файлы сохраняются в папку recordings рядом с программой.\n";
 auto header=JsonValue::makeObject();header.set("type",JsonValue::makeString("header"));header.set("schema",number(2));header.set("utc_ms",number(double(utc)));header.set("pid",number(pid));header.set("process_created",JsonValue::makeString(std::to_string(target.created)));header.set("map_label",JsonValue::makeString(map));header.set("gsi_enabled",JsonValue::makeBool(bool(gsi)));header.set("sample_hz",number(50));file<<header.dump()<<'\n';
 const int keys[]={'W','A','S','D',VK_SPACE,VK_CONTROL,VK_SHIFT,'R','E','Q','1','2','3','4','5',VK_LBUTTON,VK_RBUTTON};
 const char* labels[]={"W","A","S","D","SPACE","CTRL","SHIFT","R","E","Q","1","2","3","4","5","MOUSE_LEFT","MOUSE_RIGHT"};
 auto start=std::chrono::steady_clock::now(),deadline=start;unsigned long samples=0;int manualRound=0;
 RouteEvents events(pid,start);
 std::string lastMap;
 HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\CS2RouteStop-"+std::to_wstring(GetCurrentProcessId())).c_str());
 struct StopEventGuard{HANDLE h;~StopEventGuard(){if(h)CloseHandle(h);}} stopGuard{stopEvent};
 std::cout<<"Переключись в CS2 — запись начнётся сразу. F10 — стоп.\n";
 while(recording&&sameProcess(process,target)&&!events.finished()){
  if(stopEvent&&WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0)break;
  auto now=std::chrono::steady_clock::now();double elapsed=std::chrono::duration<double>(now-start).count();if(elapsed>=7200)break;
  DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);bool focused=foreground==pid;bool bookmark=false;
  MSG msg{};while(PeekMessageW(&msg,nullptr,WM_HOTKEY,WM_HOTKEY,PM_REMOVE)){if(msg.wParam==1)recording=false;if(focused&&msg.wParam==4)events.arm();if(focused&&msg.wParam==2)bookmark=true;if(focused&&msg.wParam==3)++manualRound;}
  for(auto& event:events.drain())if(event["kind"].asString()=="begin") {std::cout<<"ЗАПИСЬ СОБЫТИЙ НАЧАЛАСЬ"<<std::endl;file<<event.dump()<<std::endl;} else file<<event.dump()<<'\n';
  if(!recording)break;
  auto row=JsonValue::makeObject();row.set("type",JsonValue::makeString("sample"));row.set("t",number(elapsed));row.set("focused",JsonValue::makeBool(focused));row.set("bookmark",JsonValue::makeBool(bookmark));row.set("manual_round",number(manualRound));
  auto pressed=JsonValue::makeArray();if(focused)for(size_t i=0;i<std::size(keys);++i)if(GetAsyncKeyState(keys[i])&0x8000)pressed.append(JsonValue::makeString(labels[i]));
  row.set("keys",focused?pressed:JsonValue::makeNull());auto p=memory.poll();row.set("valid",JsonValue::makeBool(bool(p)));
  if(p){if(!p->mapName.empty())row.set("map",JsonValue::makeString(p->mapName));row.set("position",vec(p->position));row.set("velocity",vec(p->velocity));row.set("eye",vec(p->eye));row.set("yaw",number(p->yaw));row.set("pitch",number(p->pitch));row.set("alive",JsonValue::makeBool(p->alive));row.set("health",number(p->health));row.set("team",number(p->team));row.set("ground",JsonValue::makeBool(p->onGround));row.set("weapon_valid",JsonValue::makeBool(p->weapon.valid));row.set("weapon",number(p->weapon.item));row.set("ammo",number(p->weapon.ammo));row.set("reloading",JsonValue::makeBool(p->weapon.reloading));row.set("shots",number(p->shotsFired));row.set("recoil",p->recoilValid?vec({p->recoil.x,p->recoil.y,0}):JsonValue::makeNull());}
  else row.set("error",JsonValue::makeString(memory.error()));
  if(gsi){auto setup=gsi->matchSetup();row.set("gsi_valid",JsonValue::makeBool(setup.valid));if(setup.valid){row.set("map",JsonValue::makeString(setup.mapName));}}
  auto actionStates=JsonValue::makeObject();bool actionsValid=bool(p);
  if(gsi){auto setup=gsi->matchSetup();if(setup.valid&&setup.mapName!=lastMap){lastMap=setup.mapName;std::cout<<"Карта: "<<lastMap<<std::endl;}}
  if(p)for(const auto& action:actions){std::uint32_t state=0;SIZE_T bytes=0;
    if(!ReadProcessMemory(process,reinterpret_cast<void*>(action.second),&state,sizeof(state),&bytes)||bytes!=sizeof(state)){actionsValid=false;break;}
    actionStates.set(action.first,JsonValue::makeNumber(state));
  }
  row.set("action_state_format",JsonValue::makeString("source2-buttons-raw-v1"));
  row.set("actions_valid",JsonValue::makeBool(actionsValid));
  row.set("action_states",actionsValid?actionStates:JsonValue::makeNull());
  file<<row.dump()<<'\n';if(++samples%50==0)file.flush();if(!file){result=7;break;}
  deadline+=std::chrono::milliseconds(20);if(deadline<std::chrono::steady_clock::now())deadline=std::chrono::steady_clock::now();std::this_thread::sleep_until(deadline);
 }
 events.stop();for(auto& event:events.drain())if(event["kind"].asString()=="begin") {std::cout<<"ЗАПИСЬ СОБЫТИЙ НАЧАЛАСЬ"<<std::endl;file<<event.dump()<<std::endl;} else file<<event.dump()<<'\n';
 file<<"{\"type\":\"end\",\"samples\":"<<samples<<",\"input_valid\":"<<(events.failed()?"false":"true")<<"}\n";file.flush();if(!file)result=7;
 for(int id=1;id<=4;++id)UnregisterHotKey(nullptr,id);CloseHandle(process);
 std::cout<<(result?"Ошибка сохранения. Проверь свободное место.\n":"ЗАПИСЬ ЗАВЕРШЕНА. Открываю папку с записью.\n");
 if(!result){std::wstring command=L"explorer.exe /select,\""+path.wstring()+L"\"";STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}}
 if(events.failed()){std::cerr<<"Recording input incomplete: no start or capture error. File saved for diagnostics.\n";return 9;}
 return result;
}

int wmain(int argc,wchar_t** argv){
 try{return run(argc,argv);}catch(const std::exception& e){std::cerr<<"Recorder error: "<<e.what()<<'\n';return 8;}
}


