#include "../src/rds/license_transport.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace seats::rds::licensing;
namespace {
unsigned checks=0;
void check(bool condition,const char* label) {
    ++checks;
    if(!condition){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}
}
Bytes bytes(const std::vector<std::uint8_t>& value){return {value.data(),value.size()};}
std::vector<std::uint8_t> opaque(std::size_t size,std::uint8_t type=1) {
    std::vector<std::uint8_t> result(size,0xa5);
    if(size>=4){result[0]=type;result[1]=3;result[2]=static_cast<std::uint8_t>(size);
        result[3]=static_cast<std::uint8_t>(size>>8);}
    return result;
}
constexpr McsRoute route{1001,1003};
// Original fixture from the published field definitions: ERR_NO_LICENSE,
// ST_TOTAL_ABORT, empty BB_ERROR_BLOB. It grants nothing.
const std::vector<std::uint8_t> denied{
    0xff,3,16,0,2,0,0,0,1,0,0,0,4,0,0,0
};
const std::vector<std::uint8_t> deniedServer{
    3,0,0,34,2,0xf0,0x80,0x68,0,0,3,0xeb,0x70,20,
    0x80,0,0,0,0xff,3,16,0,2,0,0,0,1,0,0,0,4,0,0,0
};
std::vector<std::uint8_t> clientFrame(){
    auto result=deniedServer;result[7]=0x64;return result;
}
void rejectClient(std::vector<std::uint8_t> input,FramingError expected,const char* label) {
    std::vector<std::uint8_t> result{0xaa,0xbb};
    check(unwrapClientMessage(bytes(input),route,BufferFormat::Preamble,result)==expected,label);
    check(result.empty(),"rejected packet clears previous output");
}
}
int main() {
    std::vector<std::uint8_t> frame,result;
    MessageInfo info;
    check(wrapServerMessage(bytes(denied),BufferFormat::Preamble,route,frame)==FramingError::None,
          "wrap published-field error fixture");
    check(frame==deniedServer,"exact TPKT/X224/MCS/security bytes against independent fixture");
    auto client=clientFrame();
    check(unwrapClientMessage(bytes(client),route,BufferFormat::Preamble,result)==FramingError::None,
          "unwrap client error alert");
    check(result==denied,"license error bytes preserved without conversion to success");
    check(inspectMessage(bytes(result),BufferFormat::Preamble,Direction::ClientToServer,info)==FramingError::None
          &&info.type==0xff&&info.version==3&&!info.extendedErrors&&info.preambleBytes==16,
          "inspect only framing metadata");
    check(unwrapClientMessage(bytes(client),route,BufferFormat::BasicSecurityHeader,result)==FramingError::None,
          "explicit security format");
    check(result==std::vector<std::uint8_t>(deniedServer.begin()+14,deniedServer.end()),
          "security header retained only when selected");
    check(wrapServerMessage(bytes(result),BufferFormat::BasicSecurityHeader,route,frame)==FramingError::None
          &&frame==deniedServer,"security header not duplicated");
    check(wrapServerMessage(bytes(result),BufferFormat::Preamble,route,frame)!=FramingError::None&&frame.empty(),
          "no implicit buffer format detection");

    for(std::uint8_t type:std::array<std::uint8_t,8>{1,2,3,4,0x12,0x13,0x15,0xff}) {
        auto packet=opaque(12,type);
        const bool server=(type<=4||type==0xff);
        check((inspectMessage(bytes(packet),BufferFormat::Preamble,Direction::ServerToClient,info)==FramingError::None)==server,
              "server direction allowlist");
        check((inspectMessage(bytes(packet),BufferFormat::Preamble,Direction::ClientToServer,info)==FramingError::None)==!((type<=4)&&type!=0xff),
              "client direction allowlist");
    }
    for(std::size_t size:{std::size_t(4),std::size_t(123),std::size_t(124),maxMessageBytes}) {
        auto packet=opaque(size);
        check(wrapServerMessage(bytes(packet),BufferFormat::Preamble,route,frame)==FramingError::None,
              "bounded payload including PER transition");
        check(frame[13]==(size==maxMessageBytes?0xbf:size<124?static_cast<std::uint8_t>(size+4):0x80),
              "PER determinant boundary");
        const auto offset=frame[13]&0x80?15u:14u;
        check(frame.size()==size+offset+4,"bounded exact frame length");
        // Convert this opaque framing-only fixture to a client message. No
        // cryptographic/body validation is claimed for these boundary cases.
        frame[7]=0x64;frame[offset+4]=0x13;packet[0]=0x13;
        check(unwrapClientMessage(bytes(frame),route,BufferFormat::Preamble,result)==FramingError::None&&result==packet,
              "opaque payload preserved at boundary");
    }
    auto large=opaque(maxMessageBytes+1);
    check(wrapServerMessage(bytes(large),BufferFormat::Preamble,route,frame)==FramingError::TooLarge&&frame.empty(),
          "oversized message rejected before allocation");
    for(std::size_t length=0;length<deniedServer.size();++length) {
        result={0xee};
        check(unwrapClientMessage({client.data(),length},route,BufferFormat::Preamble,result)!=FramingError::None&&result.empty(),
              "every truncated frame rejected without output");
    }
    check(wrapServerMessage({nullptr,16},BufferFormat::Preamble,route,frame)==FramingError::NullInput,
          "null server buffer");
    check(unwrapClientMessage({nullptr,34},route,BufferFormat::Preamble,result)==FramingError::NullInput,
          "null client buffer");
    check(wrapServerMessage(bytes(denied),BufferFormat::Preamble,{1000,1003},frame)==FramingError::InvalidRoute,
          "initiator underflow prohibited");
    check(wrapServerMessage(bytes(denied),BufferFormat::Preamble,{1001,0},frame)==FramingError::InvalidRoute,
          "invalid channel prohibited");
    check(unwrapClientMessage(bytes(client),{1002,1003},BufferFormat::Preamble,result)==FramingError::RouteMismatch,
          "initiator correlation");
    check(unwrapClientMessage(bytes(client),{1001,1004},BufferFormat::Preamble,result)==FramingError::RouteMismatch,
          "channel correlation");
    check(inspectMessage(bytes(denied),static_cast<BufferFormat>(99),Direction::ServerToClient,info)==FramingError::InvalidArgument,
          "invalid format");
    check(inspectMessage(bytes(denied),BufferFormat::Preamble,static_cast<Direction>(99),info)==FramingError::InvalidArgument,
          "invalid direction");

    auto bad=client;bad[0]=4;rejectClient(bad,FramingError::InvalidTpkt,"TPKT version");
    bad=client;bad[1]=1;rejectClient(bad,FramingError::InvalidTpkt,"TPKT reserved");
    bad=client;bad[3]--;rejectClient(bad,FramingError::LengthMismatch,"TPKT length");
    bad=client;bad.push_back(0);rejectClient(bad,FramingError::LengthMismatch,"concatenated/trailing bytes");
    for(auto index:{4u,5u,6u}) {
        bad=client;bad[index]^=1;rejectClient(bad,FramingError::InvalidX224,"X224 fixed header");
    }
    rejectClient(deniedServer,FramingError::InvalidMcs,"server indication rejected as client request");
    bad=client;bad[12]=0x60;rejectClient(bad,FramingError::UnsupportedSegmentation,"segmented message rejected");
    bad=client;bad[13]=0xc0;rejectClient(bad,FramingError::UnsupportedLength,"PER fragmentation rejected");
    bad=client;bad[13]--;rejectClient(bad,FramingError::LengthMismatch,"MCS declared length mismatch");
    for(auto index:{14u,15u,16u,17u}) {
        bad=client;bad[index]^=8;rejectClient(bad,FramingError::UnsupportedSecurity,"encrypted/unsupported security flags");
    }
    bad=client;bad[18]=1;rejectClient(bad,FramingError::WrongDirection,"server payload in client envelope");
    bad=client;bad[18]=0x22;rejectClient(bad,FramingError::UnknownMessage,"unknown message type");
    bad=client;bad[19]=4;rejectClient(bad,FramingError::UnsupportedVersion,"unsupported version");
    bad=client;bad[19]=0x13;rejectClient(bad,FramingError::UnsupportedFlags,"unknown preamble flags");
    bad=client;bad[20]++;rejectClient(bad,FramingError::LengthMismatch,"inner preamble length mismatch");
    bad=client;bad[19]=0x82;
    check(unwrapClientMessage(bytes(bad),route,BufferFormat::Preamble,result)==FramingError::None&&result[1]==0x82,
          "supported version and extended error bit preserved");

    // Accept an unfragmented two-byte length even for a small payload.
    bad=client;bad.insert(bad.begin()+13,0x80);++bad[3];
    check(unwrapClientMessage(bytes(bad),route,BufferFormat::Preamble,result)==FramingError::None&&result==denied,
          "two-byte PER length for short payload");
    // Both public transformations support input sharing the output vector:
    result=denied;
    check(wrapServerMessage(bytes(result),BufferFormat::Preamble,route,result)==FramingError::None&&result==deniedServer,
          "alias-safe server wrap");
    result=client;
    check(unwrapClientMessage(bytes(result),route,BufferFormat::Preamble,result)==FramingError::None&&result==denied,
          "alias-safe client unwrap");
    check(std::string(framingErrorName(FramingError::UnsupportedSecurity))=="unsupported-security",
          "static non-sensitive diagnostic");
    std::cout<<"PASS "<<checks<<" offline licensing framing checks; no licensing engine, service, or session executed.\n";
}
