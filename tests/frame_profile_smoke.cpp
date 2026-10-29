#include "FrameProfile.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <sstream>
#include <vector>
#include <cmath>
int main(){
 const std::wstring path=L"frame_profile_smoke.csv";std::filesystem::remove(path);std::filesystem::remove(path+L".status.txt");
 if(!FrameProfile::Request(path,1,1)||FrameProfile::Request(path,1,1))return 1;
 FrameProfile::Frame();FrameProfile::Memory(nullptr);
 {DVR_PROFILE_DETAIL("excluded.detail");}
 FrameProfile::CompleteStereo(false);Sleep(2);FrameProfile::CompleteStereo(false);
 FrameProfile::CompleteStereo(true);Sleep(2);FrameProfile::CompleteStereo(true);
 {DVR_PROFILE("parent");Sleep(5);{DVR_PROFILE("child");Sleep(10);}Sleep(5);}
 std::thread worker([]{DVR_PROFILE("worker");Sleep(3);});worker.join();Sleep(1000);FrameProfile::Poll();
 for(int i=0;i<100&&!std::filesystem::exists(path+L".status.txt");++i)Sleep(10);
 std::ifstream f(path);std::string line;bool parent=false,child=false,other=false,native=false,afw=false;
 double parentIn=0,parentEx=0,childIn=0;std::string parentThread,workerThread;
 while(std::getline(f,line)){parent|=line.find(",parent,")!=std::string::npos;child|=line.find(",child,")!=std::string::npos;other|=line.find(",worker,")!=std::string::npos;std::cout<<line<<'\n';
 if(line.find("excluded.detail")!=std::string::npos)return 3;
 native|=line.find("interval.nativeStereoAccepted")!=std::string::npos;afw|=line.find("interval.afwStereoAccepted")!=std::string::npos;
 std::stringstream stream(line);std::vector<std::string> cells;std::string cell;while(std::getline(stream,cell,','))cells.push_back(cell);
 if(cells.size()==6&&cells[2]=="parent"){parentIn=std::stod(cells[4]);parentEx=std::stod(cells[5]);parentThread=cells[0];}
 if(cells.size()==6&&cells[2]=="child")childIn=std::stod(cells[4]);
 if(cells.size()==6&&cells[2]=="worker")workerThread=cells[0];
 }
 if(!parent||!child||!other||FrameProfile::active)return 2;
 if(!native||!afw||parentThread==workerThread||childIn<=0||parentEx<=0||std::abs(parentIn-parentEx-childIn)>0.01)return 4;
 std::cout<<"PASS numeric nesting identity, thread separation, stereo intervals, detail filtering, nested and worker scopes, bounded capture, asynchronous export and busy refusal\n";
}
