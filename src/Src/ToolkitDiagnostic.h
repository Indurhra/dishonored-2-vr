// Process-local diagnostic entry point: the application's own HKCU view is authoritative.
// No registry changes happen unless explicitly requested through a DVR command.
#pragma once
#include <Windows.h>
#include <fstream>
#include <string>
#include "GameInfo.h"
inline void ToolkitDiagnostic(const std::string& mode) {
 const auto root=GameInfo::GameDir();
 std::ofstream report(root+L"\\DVR_toolkit_config.txt");
 if(mode!="status"&&mode!="0"&&mode!="1"&&mode!="restore"){report<<"Use vr toolkitvrs status|0|1|restore\n";return;}
 constexpr wchar_t key[]=L"SOFTWARE\\OpenXR_Toolkit\\DishonoredVR";
 const wchar_t* names[]={L"vrs",L"vrs_quality",L"eye_tracking"};
 DWORD before[3]{},exists[3]{};
 for(int i=0;i<3;++i){DWORD size=sizeof(DWORD);auto rc=RegGetValueW(HKEY_CURRENT_USER,key,names[i],RRF_RT_REG_DWORD,nullptr,&before[i],&size);report<<"before "<<i<<" rc "<<rc<<" value "<<before[i]<<'\n';if(rc!=ERROR_SUCCESS&&rc!=ERROR_FILE_NOT_FOUND){report<<"Unsupported registry value; no writes\n";return;}exists[i]=rc==ERROR_SUCCESS;}
 if(mode=="status")return;
 const auto backup=root+L"\\DVR_toolkit_config_backup.txt";
 DWORD values[3]{},present[3]{1,1,1};
 if(mode=="restore"){
  std::ifstream in(backup);for(int i=0;i<3;++i)if(!(in>>present[i]>>values[i])||present[i]>1){report<<"Invalid/missing backup; no writes\n";return;}
 }else{
  if(GetFileAttributesW(backup.c_str())==INVALID_FILE_ATTRIBUTES){std::ofstream out(backup);for(int i=0;i<3;++i)out<<exists[i]<<' '<<before[i]<<'\n';out.close();if(!out){report<<"Backup failed; no writes\n";return;}}
  values[0]=mode=="1"?1:0;values[1]=1;values[2]=0; // Quality preset, fixed foveation.
 }
 HKEY handle=nullptr;auto rc=RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&handle,nullptr);
 if(rc!=ERROR_SUCCESS){report<<"Open failed "<<rc<<'\n';return;}
 bool ok=true;for(int i=0;i<3;++i){rc=present[i]?RegSetValueExW(handle,names[i],0,REG_DWORD,reinterpret_cast<const BYTE*>(&values[i]),sizeof(DWORD)):RegDeleteValueW(handle,names[i]);if(rc!=ERROR_SUCCESS&&!(rc==ERROR_FILE_NOT_FOUND&&!present[i]))ok=false;report<<"write "<<i<<" rc "<<rc<<" value "<<values[i]<<'\n';}
 RegCloseKey(handle);
 for(int i=0;i<3;++i){DWORD size=sizeof(DWORD),v=0;rc=RegGetValueW(HKEY_CURRENT_USER,key,names[i],RRF_RT_REG_DWORD,nullptr,&v,&size);report<<"after "<<i<<" rc "<<rc<<" value "<<v<<'\n';}
 if(mode=="restore"&&ok){DeleteFileW(backup.c_str());report<<"Restored original values\n";}
}
