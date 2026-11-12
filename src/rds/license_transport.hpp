#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace seats::rds::licensing {
struct Bytes {
    const std::uint8_t* data=nullptr;
    std::size_t size=0;
};
enum class BufferFormat { Preamble, BasicSecurityHeader };
enum class Direction { ServerToClient, ClientToServer };
struct McsRoute {
    std::uint16_t initiatorId;
    std::uint16_t channelId;
};
struct MessageInfo {
    std::uint8_t type=0;
    std::uint8_t version=0;
    bool extendedErrors=false;
    std::size_t preambleBytes=0;
};
enum class FramingError {
    None, NullInput, InvalidArgument, InvalidRoute, Truncated, TooLarge,
    UnsupportedSecurity, UnsupportedVersion, UnsupportedFlags, UnknownMessage,
    WrongDirection, LengthMismatch, InvalidTpkt, InvalidX224, InvalidMcs,
    RouteMismatch, UnsupportedSegmentation, UnsupportedLength, AllocationFailure
};

// [MS-RDPBCGR] limits an MCS Send Data PDU to 16,383 bytes.
// 8 MCS header bytes + 4 Basic Security Header bytes + this message.
inline constexpr std::size_t maxMessageBytes=16371;
inline constexpr std::size_t maxFrameBytes=16390;

// These functions validate FRAMING only. They neither validate license
// signatures/MACs nor grant a license/session. Output is empty on failure.
// The caller explicitly selects the service buffer format; it is never guessed.
FramingError inspectMessage(Bytes message,BufferFormat format,Direction direction,
                            MessageInfo& info)noexcept;
FramingError wrapServerMessage(Bytes message,BufferFormat format,McsRoute route,
                              std::vector<std::uint8_t>& frame)noexcept;
FramingError unwrapClientMessage(Bytes frame,McsRoute expectedRoute,BufferFormat format,
                                std::vector<std::uint8_t>& message)noexcept;
const char* framingErrorName(FramingError error)noexcept;
}
