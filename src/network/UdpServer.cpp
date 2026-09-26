#include "network/UdpServer.hpp"

#include <cerrno>
#include <system_error>
#include <stdexcept>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cnc {
UdpServer::UdpServer(const std::string& address, std::uint16_t port) {
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &local.sin_addr) != 1) {
        throw std::invalid_argument("--bind requires a numeric IPv4 address");
    }
    fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) throw std::system_error(errno, std::generic_category(), "socket");
    if (bind(fd_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0) {
        const auto error = errno;
        close(fd_);
        fd_ = -1;
        throw std::system_error(error, std::generic_category(), "bind " + address + ":" + std::to_string(port));
    }
    socklen_t length = sizeof(local);
    if (getsockname(fd_, reinterpret_cast<sockaddr*>(&local), &length) < 0) {
        const auto error = errno;
        close(fd_);
        fd_ = -1;
        throw std::system_error(error, std::generic_category(), "getsockname");
    }
    port_ = ntohs(local.sin_port);
}

UdpServer::~UdpServer() { if (fd_ >= 0) close(fd_); }

std::optional<Datagram> UdpServer::receive(int timeout_ms) {
    pollfd descriptor{fd_, POLLIN, 0};
    const int result = poll(&descriptor, 1, timeout_ms);
    if (result < 0) {
        if (errno == EINTR) return std::nullopt;
        throw std::system_error(errno, std::generic_category(), "poll");
    }
    if (result == 0) return std::nullopt;
    if ((descriptor.revents & (POLLERR | POLLNVAL | POLLHUP)) != 0) {
        throw std::runtime_error("UDP socket reported an error");
    }
    Datagram datagram;
    socklen_t length = sizeof(datagram.sender);
    const auto count = recvfrom(fd_, datagram.bytes.data(), datagram.bytes.size(), 0,
                               reinterpret_cast<sockaddr*>(&datagram.sender), &length);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return std::nullopt;
        throw std::system_error(errno, std::generic_category(), "recvfrom");
    }
    datagram.size = static_cast<std::size_t>(count);
    return datagram;
}

bool UdpServer::send(const Response& response, const sockaddr_in& destination) {
    ssize_t count;
    do {
        count = sendto(fd_, &response, sizeof(response), MSG_DONTWAIT,
                       reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    } while (count < 0 && errno == EINTR);
    return count == static_cast<ssize_t>(sizeof(response));
}

std::string UdpServer::endpoint(const sockaddr_in& address) {
    std::array<char, INET_ADDRSTRLEN> buffer{};
    if (!inet_ntop(AF_INET, &address.sin_addr, buffer.data(), buffer.size())) {
        throw std::system_error(errno, std::generic_category(), "inet_ntop");
    }
    return std::string(buffer.data()) + ":" + std::to_string(ntohs(address.sin_port));
}
} // namespace cnc
