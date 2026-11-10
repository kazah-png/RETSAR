#pragma once
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace seats::runtime {
class JournalError:public std::runtime_error {public:using std::runtime_error::runtime_error;};
class Journal {
public:
    virtual ~Journal()=default;
    virtual const std::vector<std::string>& records()const noexcept=0;
    // Success means every byte was written and flushed. A thrown error poisons
    // the caller's transaction; it cannot assume that no bytes reached disk.
    virtual void append(const std::string& record)=0;
};
enum class JournalOpenMode {OpenExisting,CreateNew};
class DurableJournal final:public Journal {
    void* file_=nullptr;
    bool poisoned_=false;
    std::vector<std::string> records_;
public:
    explicit DurableJournal(const std::filesystem::path& absolutePathOnC,JournalOpenMode mode=JournalOpenMode::OpenExisting);
    ~DurableJournal();
    DurableJournal(const DurableJournal&)=delete;
    DurableJournal& operator=(const DurableJournal&)=delete;
    const std::vector<std::string>& records()const noexcept override{return records_;}
    void append(const std::string& record)override;
};
}
