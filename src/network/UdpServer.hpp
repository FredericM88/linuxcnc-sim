#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <netinet/in.h>
#include "protocol/OriginalProtocol.hpp"

namespace cnc {
struct Datagram {
    // One extra byte makes oversized datagrams invalid even after truncation.
    std::array<std::uint8_t, request_size + 1> bytes{};
    std::size_t size{};
    sockaddr_in sender{};
};

class UdpServer {
public:
    UdpServer(const std::string& address, std::uint16_t port);
    ~UdpServer();
    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;
    std::optional<Datagram> receive(int timeout_ms);
    bool send(const Response& response, const sockaddr_in& destination);
    std::uint16_t port() const { return port_; }
    static std::string endpoint(const sockaddr_in& address);
private:
    int fd_{-1};
    std::uint16_t port_{};
};
} // namespace cnc
