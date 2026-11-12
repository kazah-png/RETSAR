#include "license_transport.hpp"
#include <new>

namespace seats::rds::licensing {
namespace {
constexpr std::uint16_t firstChannelId=1001;
constexpr std::size_t transportBytes=7;
constexpr std::size_t securityBytes=4;
constexpr std::size_t fixedMcsBytes=6;

bool validFormat(BufferFormat format)noexcept {
    return format==BufferFormat::Preamble||format==BufferFormat::BasicSecurityHeader;
}
bool validRoute(McsRoute route)noexcept {
    return route.initiatorId>=firstChannelId&&route.channelId>=firstChannelId;
}
std::uint16_t be16(const std::uint8_t* bytes)noexcept {
    return static_cast<std::uint16_t>((static_cast<unsigned>(bytes[0])<<8)|bytes[1]);
}
std::uint16_t le16(const std::uint8_t* bytes)noexcept {
    return static_cast<std::uint16_t>((static_cast<unsigned>(bytes[1])<<8)|bytes[0]);
}
void putBe16(std::vector<std::uint8_t>& bytes,std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value>>8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}
FramingError failure(FramingError error,std::vector<std::uint8_t>& output)noexcept {
    output.clear();
    return error;
}
}

FramingError inspectMessage(Bytes message,BufferFormat format,Direction direction,
                            MessageInfo& info)noexcept {
    info={};
    if(!validFormat(format)||(direction!=Direction::ServerToClient&&direction!=Direction::ClientToServer))
        return FramingError::InvalidArgument;
    if(!message.data)return FramingError::NullInput;
    const std::size_t offset=format==BufferFormat::BasicSecurityHeader?securityBytes:0;
    if(message.size>maxMessageBytes+offset)return FramingError::TooLarge;
    if(message.size<offset+4)return FramingError::Truncated;
    if(offset&&(message.data[0]!=0x80||message.data[1]!=0||message.data[2]!=0||message.data[3]!=0))
        return FramingError::UnsupportedSecurity;
    const auto* p=message.data+offset;
    const auto version=static_cast<std::uint8_t>(p[1]&0x0f);
    if(version!=2&&version!=3)return FramingError::UnsupportedVersion;
    if(p[1]&0x70)return FramingError::UnsupportedFlags;
    if(le16(p+2)!=message.size-offset)return FramingError::LengthMismatch;
    bool server=false,client=false;
    switch(p[0]) {
    case 0x01:case 0x02:case 0x03:case 0x04:server=true;break;
    case 0x12:case 0x13:case 0x15:client=true;break;
    case 0xff:server=true;client=true;break;
    default:return FramingError::UnknownMessage;
    }
    if((direction==Direction::ServerToClient&&!server)||(direction==Direction::ClientToServer&&!client))
        return FramingError::WrongDirection;
    info={p[0],version,(p[1]&0x80)!=0,message.size-offset};
    return FramingError::None;
}

FramingError wrapServerMessage(Bytes message,BufferFormat format,McsRoute route,
                              std::vector<std::uint8_t>& frame)noexcept {
    if(!validRoute(route))return failure(FramingError::InvalidRoute,frame);
    MessageInfo info;
    auto error=inspectMessage(message,format,Direction::ServerToClient,info);
    if(error!=FramingError::None)return failure(error,frame);
    const std::size_t userBytes=info.preambleBytes+securityBytes;
    const std::size_t lengthBytes=userBytes<128?1:2;
    const std::size_t total=transportBytes+fixedMcsBytes+lengthBytes+userBytes;
    try {
        std::vector<std::uint8_t> result;
        result.reserve(total);
        result.insert(result.end(),{3,0});
        putBe16(result,static_cast<std::uint16_t>(total));
        result.insert(result.end(),{2,0xf0,0x80,0x68}); // X.224 + MCS SendDataIndication
        putBe16(result,static_cast<std::uint16_t>(route.initiatorId-firstChannelId));
        putBe16(result,route.channelId);
        result.push_back(0x70); // High priority, begin/end unsegmented.
        if(lengthBytes==1)result.push_back(static_cast<std::uint8_t>(userBytes));
        else putBe16(result,static_cast<std::uint16_t>(userBytes|0x8000));
        if(format==BufferFormat::Preamble)result.insert(result.end(),{0x80,0,0,0});
        result.insert(result.end(),message.data,message.data+message.size);
        frame.swap(result);
        return FramingError::None;
    }catch(...) {return failure(FramingError::AllocationFailure,frame);}
}

FramingError unwrapClientMessage(Bytes frame,McsRoute expectedRoute,BufferFormat format,
                                std::vector<std::uint8_t>& message)noexcept {
    if(!validFormat(format))return failure(FramingError::InvalidArgument,message);
    if(!validRoute(expectedRoute))return failure(FramingError::InvalidRoute,message);
    if(!frame.data)return failure(FramingError::NullInput,message);
    if(frame.size>maxFrameBytes)return failure(FramingError::TooLarge,message);
    if(frame.size<transportBytes+fixedMcsBytes+1)return failure(FramingError::Truncated,message);
    const auto* p=frame.data;
    if(p[0]!=3||p[1]!=0)return failure(FramingError::InvalidTpkt,message);
    if(be16(p+2)!=frame.size)return failure(FramingError::LengthMismatch,message);
    if(p[4]!=2||p[5]!=0xf0||p[6]!=0x80)return failure(FramingError::InvalidX224,message);
    if(p[7]!=0x64)return failure(FramingError::InvalidMcs,message);
    if(be16(p+8)!=expectedRoute.initiatorId-firstChannelId||be16(p+10)!=expectedRoute.channelId)
        return failure(FramingError::RouteMismatch,message);
    if(p[12]!=0x70)return failure(FramingError::UnsupportedSegmentation,message);
    std::size_t offset=14;
    std::size_t userBytes=p[13];
    if(p[13]&0x80) {
        // PER 11xxxxxx is fragmented encoding, outside this bounded adapter.
        if(p[13]&0x40)return failure(FramingError::UnsupportedLength,message);
        if(frame.size<15)return failure(FramingError::Truncated,message);
        userBytes=be16(p+13)&0x3fff;
        offset=15;
    }
    if(userBytes!=frame.size-offset)return failure(FramingError::LengthMismatch,message);
    MessageInfo info;
    const auto error=inspectMessage({p+offset,userBytes},BufferFormat::BasicSecurityHeader,
                                    Direction::ClientToServer,info);
    if(error!=FramingError::None)return failure(error,message);
    if(format==BufferFormat::Preamble)offset+=securityBytes;
    try {
        std::vector<std::uint8_t> result(p+offset,p+frame.size);
        message.swap(result);
        return FramingError::None;
    }catch(...) {return failure(FramingError::AllocationFailure,message);}
}

const char* framingErrorName(FramingError error)noexcept {
    switch(error) {
    case FramingError::None:return "none";
    case FramingError::NullInput:return "null-input";
    case FramingError::InvalidArgument:return "invalid-argument";
    case FramingError::InvalidRoute:return "invalid-route";
    case FramingError::Truncated:return "truncated";
    case FramingError::TooLarge:return "too-large";
    case FramingError::UnsupportedSecurity:return "unsupported-security";
    case FramingError::UnsupportedVersion:return "unsupported-version";
    case FramingError::UnsupportedFlags:return "unsupported-flags";
    case FramingError::UnknownMessage:return "unknown-message";
    case FramingError::WrongDirection:return "wrong-direction";
    case FramingError::LengthMismatch:return "length-mismatch";
    case FramingError::InvalidTpkt:return "invalid-tpkt";
    case FramingError::InvalidX224:return "invalid-x224";
    case FramingError::InvalidMcs:return "invalid-mcs";
    case FramingError::RouteMismatch:return "route-mismatch";
    case FramingError::UnsupportedSegmentation:return "unsupported-segmentation";
    case FramingError::UnsupportedLength:return "unsupported-length";
    case FramingError::AllocationFailure:return "allocation-failure";
    }
    return "unknown-error";
}
}
