#include "journal.hpp"
#include "safe_c_path.hpp"
#include <windows.h>
#include <algorithm>
#include <memory>

namespace seats::runtime {
namespace {
constexpr size_t MaxFile=32*1024*1024,MaxRecord=1024*1024;
void u32(std::string& out,uint32_t v){for(unsigned i=0;i<4;++i)out.push_back(static_cast<char>((v>>(8*i))&255));}
void u64(std::string& out,uint64_t v){for(unsigned i=0;i<8;++i)out.push_back(static_cast<char>((v>>(8*i))&255));}
uint32_t r32(const std::string& in,size_t at){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(static_cast<unsigned char>(in.at(at+i)))<<(8*i);return v;}
uint64_t r64(const std::string& in,size_t at){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(static_cast<unsigned char>(in.at(at+i)))<<(8*i);return v;}
uint32_t crc(const std::string& bytes){uint32_t c=0xffffffff;for(unsigned char b:bytes){c^=b;for(unsigned n=0;n<8;++n)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
void writeAll(HANDLE file,const std::string& bytes){
    size_t at=0;while(at<bytes.size()){DWORD written=0;if(!WriteFile(file,bytes.data()+at,static_cast<DWORD>(bytes.size()-at),&written,nullptr)||!written)throw JournalError("journal write failed");at+=written;}
    if(!FlushFileBuffers(file))throw JournalError("journal durable flush failed");
}
}
DurableJournal::DurableJournal(const std::filesystem::path& input,JournalOpenMode mode){
    std::unique_ptr<seats::LockedCParents> parents;
    try{parents=std::make_unique<seats::LockedCParents>(input);}catch(const std::exception& error){throw JournalError(error.what());}
    bool existed=false;HANDLE file=INVALID_HANDLE_VALUE;
    try{file=parents->openFile(GENERIC_READ|GENERIC_WRITE,mode==JournalOpenMode::CreateNew?CREATE_NEW:OPEN_EXISTING,existed);}catch(const std::exception& error){throw JournalError(error.what());}
    file_=file;
    try{
        BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw JournalError("journal target cannot be verified");
        LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>static_cast<LONGLONG>(MaxFile))throw JournalError("journal exceeds size limit");
        if(size.QuadPart==0){
            if(existed)throw JournalError("existing empty journal is corrupt; never reset automatically");
            std::string header="ZZENSEAT";u32(header,1);u32(header,crc(header));writeAll(file,header);return;
        }
        std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD read=0;
        if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=bytes.size())throw JournalError("journal read failed");
        if(bytes.size()<16||bytes.substr(0,8)!="ZZENSEAT"||r32(bytes,8)!=1||r32(bytes,12)!=crc(bytes.substr(0,12)))throw JournalError("journal header corrupt or unsupported");
        size_t at=16;uint64_t sequence=1;
        while(at<bytes.size()){
            if(bytes.size()-at<20)throw JournalError("journal has a truncated tail");
            const uint32_t length=r32(bytes,at);const uint64_t observed=r64(bytes,at+4);const uint32_t checksum=r32(bytes,at+12);
            if(r32(bytes,at+16)!=0x3154535a||!length||length>MaxRecord||observed!=sequence||bytes.size()-at-20<length)throw JournalError("journal frame/sequence/truncation invalid");
            const auto payload=bytes.substr(at+20,length);std::string checked=bytes.substr(at,12)+payload;
            if(checksum!=crc(checked))throw JournalError("journal checksum mismatch");
            records_.push_back(payload);at+=20+length;++sequence;
        }
    }catch(...){CloseHandle(file);file_=nullptr;throw;}
}
DurableJournal::~DurableJournal(){if(file_)CloseHandle(static_cast<HANDLE>(file_));}
void DurableJournal::append(const std::string& payload){
    if(poisoned_||!file_)throw JournalError("journal unavailable after prior error");
    try{
        if(payload.empty()||payload.size()>MaxRecord)throw JournalError("journal record size invalid");
        const HANDLE file=static_cast<HANDLE>(file_);LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart+static_cast<LONGLONG>(payload.size()+20)>static_cast<LONGLONG>(MaxFile))throw JournalError("journal size limit reached");
        LARGE_INTEGER end{};if(!SetFilePointerEx(file,end,nullptr,FILE_END))throw JournalError("journal seek failed");
        std::string frame;u32(frame,static_cast<uint32_t>(payload.size()));u64(frame,records_.size()+1);const auto checksum=crc(frame+payload);u32(frame,checksum);u32(frame,0x3154535a);frame+=payload;
        writeAll(file,frame);records_.push_back(payload);
    }catch(...){poisoned_=true;throw;}
}
}
