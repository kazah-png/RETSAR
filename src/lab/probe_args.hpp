#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace seats::lab {
struct ProbeOptions {
    bool interactive=false;
    uint32_t expectedSession=0;
    uint32_t referenceSession=0;
    std::optional<std::wstring> output;
};

inline uint32_t sessionNumber(const std::wstring& value){
    if(value.empty())throw std::invalid_argument("A session number is required.");
    uint64_t result=0;
    for(const auto c:value){
        if(c<L'0'||c>L'9')throw std::invalid_argument("Session IDs must be decimal integers.");
        const auto digit=static_cast<uint64_t>(c-L'0');
        if(result>(std::numeric_limits<uint32_t>::max()-digit)/10)
            throw std::invalid_argument("Session ID exceeds the supported range.");
        result=result*10+digit;
    }
    if(result==0||result==std::numeric_limits<uint32_t>::max())
        throw std::invalid_argument("Session zero and special session IDs are not accepted.");
    return static_cast<uint32_t>(result);
}

// Pure parsing: no Windows API, filesystem query, device or session enumeration.
inline ProbeOptions parseProbeArgs(const std::vector<std::wstring>& args){
    ProbeOptions result;
    if(args.empty()||(args.size()==1&&args.front()==L"--help"))return result;
    bool sawExpected=false,sawReference=false;
    for(size_t i=0;i<args.size();++i){
        const auto& option=args[i];
        if(option==L"--interactive"){
            if(result.interactive)throw std::invalid_argument("Duplicate --interactive.");
            result.interactive=true;
        }else if(option==L"--expected-session"||option==L"--reference-session"||option==L"--output"){
            if(i+1==args.size())throw std::invalid_argument("Missing option value.");
            const auto& value=args[++i];
            if(option==L"--expected-session"){
                if(sawExpected)throw std::invalid_argument("Duplicate --expected-session.");
                result.expectedSession=sessionNumber(value);sawExpected=true;
            }else if(option==L"--reference-session"){
                if(sawReference)throw std::invalid_argument("Duplicate --reference-session.");
                result.referenceSession=sessionNumber(value);sawReference=true;
            }else{
                if(result.output)throw std::invalid_argument("Duplicate --output.");
                if(value.size()<4||(value[0]!=L'C'&&value[0]!=L'c')||value[1]!=L':'||
                    (value[2]!=L'\\'&&value[2]!=L'/')||value.back()==L'\\'||value.back()==L'/')
                    throw std::invalid_argument("Output requires an absolute ordinary C: filename.");
                result.output=value;
            }
        }else throw std::invalid_argument("Unknown option; use --help by itself.");
    }
    if(!result.interactive||!sawExpected||!sawReference)
        throw std::invalid_argument("Interactive mode requires both explicit session IDs.");
    if(result.expectedSession==result.referenceSession)
        throw std::invalid_argument("Expected and reference sessions must be different.");
    return result;
}

inline bool currentSessionAllowed(const ProbeOptions& options,uint32_t current)noexcept{
    return options.interactive&&options.expectedSession!=0&&
        options.expectedSession!=options.referenceSession&&
        current==options.expectedSession&&current!=options.referenceSession;
}
}
