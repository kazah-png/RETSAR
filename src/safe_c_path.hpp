#pragma once
#include <windows.h>
#include <winternl.h>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace seats {
// Resolve one component at a time relative to the retained parent handle.
// OPEN_REPARSE_POINT alone on an absolute path protects only its last component.
// Sharing restrictions alone do not prohibit every change to reparse attributes.
class LockedCParents {
    std::filesystem::path path_;
    std::vector<HANDLE> parents_;
    void close()noexcept{for(auto h:parents_)CloseHandle(h);parents_.clear();}
    static void validateComponent(const std::wstring& component){
        if(component.empty()||component==L"."||component==L".."||component.back()==L' '||component.back()==L'.')
            throw std::runtime_error("Ambiguous path component is not accepted.");
        for(wchar_t c:component)if(c<32||std::wstring(L"<>:\"|?*").find(c)!=std::wstring::npos)
            throw std::runtime_error("Invalid or alternate-stream path component.");
        std::wstring base=component.substr(0,component.find(L'.'));
        while(!base.empty()&&(base.back()==L' '||base.back()==L'.'))base.pop_back();
        for(auto& c:base)if(c>=L'a'&&c<=L'z')c=static_cast<wchar_t>(c-L'a'+L'A');
        const bool port=base.size()==4&&(base.substr(0,3)==L"COM"||base.substr(0,3)==L"LPT")&&
            ((base[3]>=L'1'&&base[3]<=L'9')||base[3]==L'\u00b9'||base[3]==L'\u00b2'||base[3]==L'\u00b3');
        if(port||base==L"CON"||base==L"PRN"||base==L"AUX"||base==L"NUL"||base==L"CONIN$"||base==L"CONOUT$")
            throw std::runtime_error("DOS device paths are not accepted.");
    }
    static HANDLE relativeOpen(HANDLE parent,const std::wstring& component,ACCESS_MASK access,ULONG disposition,ULONG options,bool& existed){
        using NtCreateFileFn=NTSTATUS(NTAPI*)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,
            PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
        const auto module=GetModuleHandleW(L"ntdll.dll");
        const auto address=module?GetProcAddress(module,"NtCreateFile"):nullptr;
        NtCreateFileFn create=nullptr;static_assert(sizeof(create)==sizeof(address));
        std::memcpy(&create,&address,sizeof(create));
        if(!create)throw std::runtime_error("Relative file opening is unavailable.");
        if(component.size()>32767)throw std::runtime_error("Path component is too long.");
        UNICODE_STRING name{};name.Length=static_cast<USHORT>(component.size()*sizeof(wchar_t));
        name.MaximumLength=name.Length;name.Buffer=const_cast<PWSTR>(component.data());
        OBJECT_ATTRIBUTES attributes{};attributes.Length=sizeof(attributes);attributes.RootDirectory=parent;
        attributes.ObjectName=&name;attributes.Attributes=0x40u|0x1000u; // OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE
        IO_STATUS_BLOCK io{};HANDLE file=nullptr;
        const auto status=create(&file,access|SYNCHRONIZE,&attributes,&io,nullptr,FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ,disposition,options|0x20u|0x200000u,nullptr,0); // SYNCHRONOUS_IO_NONALERT | OPEN_REPARSE_POINT
        if(status<0)throw std::runtime_error("Relative file opening failed, NTSTATUS="+std::to_string(static_cast<ULONG>(status)));
        existed=io.Information==1; // FILE_OPENED; FILE_CREATED is 2.
        return file;
    }
    void lockDirectory(const std::filesystem::path& directory,bool root=false){
        bool existed=false;
        HANDLE h=root?CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr):
            relativeOpen(parents_.back(),directory.filename().wstring(),FILE_READ_ATTRIBUTES,1,1,existed); // FILE_OPEN | DIRECTORY_FILE
        if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock an output parent directory.");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(h,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||
           (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){
            CloseHandle(h);throw std::runtime_error("Output parents must be ordinary directories, without junctions or symlinks.");
        }
        try{parents_.push_back(h);}catch(...){CloseHandle(h);throw;}
    }
public:
    explicit LockedCParents(const std::filesystem::path& requested){
        const auto literal=requested.wstring();
        // Reject other volumes before performing any filesystem query.
        if(literal.size()<4||(literal[0]!=L'C'&&literal[0]!=L'c')||literal[1]!=L':'||(literal[2]!=L'\\'&&literal[2]!=L'/'))
            throw std::runtime_error("Only an absolute ordinary C: path is accepted.");
        if(!requested.is_absolute()||requested.filename().empty())throw std::runtime_error("Output filename is required.");
        for(const auto& component:requested.relative_path())validateComponent(component.wstring());
        path_=requested.lexically_normal();
        try{
            auto current=path_.root_path();lockDirectory(current,true);
            for(const auto& component:path_.parent_path().relative_path()){current/=component;lockDirectory(current);}
        }catch(...){close();throw;}
    }
    ~LockedCParents(){close();}
    LockedCParents(const LockedCParents&)=delete;
    LockedCParents& operator=(const LockedCParents&)=delete;
    const std::filesystem::path& path()const noexcept{return path_;}
    HANDLE openFile(ACCESS_MASK access,DWORD disposition,bool& existed)const{
        if(disposition!=CREATE_NEW&&disposition!=OPEN_ALWAYS&&disposition!=OPEN_EXISTING)throw std::runtime_error("Unsupported file creation mode.");
        HANDLE file=relativeOpen(parents_.back(),path_.filename().wstring(),access|FILE_READ_ATTRIBUTES,
            disposition==CREATE_NEW?2u:disposition==OPEN_EXISTING?1u:3u,0x40u,existed); // FILE_CREATE / FILE_OPEN / FILE_OPEN_IF, NON_DIRECTORY_FILE
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))){
            CloseHandle(file);throw std::runtime_error("Output must be an ordinary file, without reparses.");
        }
        return file;
    }
};
}
