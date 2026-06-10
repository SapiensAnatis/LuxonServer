// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "ipc.hpp"

#include <cerrno>
#include <cstring>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <luxon/ser_ipc_binary.hpp>

namespace server {
namespace {
luxon::ser::IPCBinaryProtocol protocol;

bool ensure_socket_runtime_initialized() {
#ifdef _WIN32
    static const bool initialized = []() {
        WSADATA wsa_data{};
        return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();
    return initialized;
#else
    return true;
#endif
}

void close_socket(IPC::Socket socket) {
    if (socket == IPC::INVALID_OS_SOCKET)
        return;

#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool set_non_blocking(IPC::Socket socket) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1)
        return false;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

int last_socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool is_interrupted_error(int error) {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

bool is_would_block_error(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

#ifdef _WIN32
bool set_socket_inheritable(IPC::Socket socket, bool inheritable) {
    return SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, inheritable ? HANDLE_FLAG_INHERIT : 0) != 0;
}

std::optional<std::pair<IPC::Socket, IPC::Socket>> create_socket_pair() {
    IPC::Socket listener = IPC::INVALID_OS_SOCKET;
    IPC::Socket parent = IPC::INVALID_OS_SOCKET;
    IPC::Socket child = IPC::INVALID_OS_SOCKET;

    auto cleanup = [&]() {
        close_socket(listener);
        close_socket(parent);
        close_socket(child);
    };

    listener = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
    if (listener == IPC::INVALID_OS_SOCKET)
        return std::nullopt;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(listener, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        cleanup();
        return std::nullopt;
    }

    if (listen(listener, 1) == SOCKET_ERROR) {
        cleanup();
        return std::nullopt;
    }

    int addr_len = sizeof(addr);
    if (getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &addr_len) == SOCKET_ERROR) {
        cleanup();
        return std::nullopt;
    }

    parent = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
    if (parent == IPC::INVALID_OS_SOCKET) {
        cleanup();
        return std::nullopt;
    }

    if (connect(parent, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        cleanup();
        return std::nullopt;
    }

    child = accept(listener, nullptr, nullptr);
    if (child == IPC::INVALID_OS_SOCKET) {
        cleanup();
        return std::nullopt;
    }

    close_socket(listener);
    listener = IPC::INVALID_OS_SOCKET;

    if (!set_socket_inheritable(parent, false) || !set_socket_inheritable(child, true)) {
        cleanup();
        return std::nullopt;
    }

    return std::make_pair(parent, child);
}
#endif

#ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SEND_FLAGS = 0;
#endif
} // namespace

std::optional<IPC> IPC::create() {
    if (!ensure_socket_runtime_initialized())
        return std::nullopt;

#ifdef _WIN32
    // Create loopback stream socket pair
    auto pair = create_socket_pair();
    if (!pair.has_value())
        return std::nullopt;

    try {
        return IPC(pair->first, pair->second);
    } catch (const std::exception&) {
        close_socket(pair->first);
        close_socket(pair->second);
        return std::nullopt;
    }
#else
    Socket fds[2];
    // Create local domain stream socket pair
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
        return std::nullopt;

    try {
        return IPC(fds[0], fds[1]);
    } catch (const std::exception&) {
        close_socket(fds[0]);
        close_socket(fds[1]);
        return std::nullopt;
    }
#endif
}

IPC::IPC(Socket fd) : fd_(fd), child_fd_(INVALID_OS_SOCKET) {
    if (fd_ == INVALID_OS_SOCKET)
        return;

    if (!ensure_socket_runtime_initialized())
        throw std::runtime_error("Failed to initialize socket runtime for IPC");

    if (!set_non_blocking(fd_))
        throw std::runtime_error("Failed to configure IPC socket");
}

IPC::IPC(Socket parent_fd, Socket child_fd) : fd_(parent_fd), child_fd_(child_fd) {
    if (!ensure_socket_runtime_initialized())
        throw std::runtime_error("Failed to initialize socket runtime for IPC");

    if (fd_ != INVALID_OS_SOCKET && !set_non_blocking(fd_))
        throw std::runtime_error("Failed to configure parent IPC socket");

    if (child_fd_ != INVALID_OS_SOCKET && !set_non_blocking(child_fd_))
        throw std::runtime_error("Failed to configure child IPC socket");
}

IPC::~IPC() {
    if (fd_ != INVALID_OS_SOCKET)
        close_socket(fd_);
    if (child_fd_ != INVALID_OS_SOCKET)
        close_socket(child_fd_);
}

IPC::IPC(IPC&& other) noexcept
    : fd_(other.fd_), child_fd_(other.child_fd_), recv_buffer_(std::move(other.recv_buffer_)), send_buffer_(std::move(other.send_buffer_)) {
    other.fd_ = INVALID_OS_SOCKET;
    other.child_fd_ = INVALID_OS_SOCKET;
}

IPC& IPC::operator=(IPC&& other) noexcept {
    if (this != &other) {
        if (fd_ != INVALID_OS_SOCKET)
            close_socket(fd_);
        if (child_fd_ != INVALID_OS_SOCKET)
            close_socket(child_fd_);

        fd_ = other.fd_;
        child_fd_ = other.child_fd_;
        recv_buffer_ = std::move(other.recv_buffer_);
        send_buffer_ = std::move(other.send_buffer_);

        other.fd_ = INVALID_OS_SOCKET;
        other.child_fd_ = INVALID_OS_SOCKET;
    }
    return *this;
}

std::string IPC::socket_to_string(Socket socket) {
#ifdef _WIN32
    return std::to_string(static_cast<unsigned long long>(socket));
#else
    return std::to_string(static_cast<long long>(socket));
#endif
}

std::optional<IPC::Socket> IPC::socket_from_string(std::string_view value) {
#ifdef _WIN32
    unsigned long long parsed = 0;
    auto res = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (res.ec != std::errc() || res.ptr != value.data() + value.size())
        return std::nullopt;

    if (parsed > static_cast<unsigned long long>(std::numeric_limits<Socket>::max()))
        return std::nullopt;

    return static_cast<Socket>(parsed);
#else
    long long parsed = 0;
    auto res = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (res.ec != std::errc() || res.ptr != value.data() + value.size())
        return std::nullopt;

    if (parsed < static_cast<long long>(std::numeric_limits<Socket>::min()) || parsed > static_cast<long long>(std::numeric_limits<Socket>::max()))
        return std::nullopt;

    return static_cast<Socket>(parsed);
#endif
}

void IPC::close_child_fd() {
    if (child_fd_ != INVALID_OS_SOCKET) {
        close_socket(child_fd_);
        child_fd_ = INVALID_OS_SOCKET;
    }
}

void IPC::send_message(const luxon::ser::Message& msg) {
    if (fd_ == INVALID_OS_SOCKET)
        throw std::runtime_error("Attempted to send message over unconnected IPC!");

    auto payload_res = protocol.Serialize(msg);
    if (!payload_res.has_value())
        throw std::runtime_error("Failed to serialize IPC message: " + payload_res.error().message);

    const auto& payload = payload_res.value();

    // Make sure size can fit in 4-byte header framing
    if (payload.size() > UINT32_MAX)
        throw std::runtime_error("IPC message too large!");

    uint32_t network_len = htonl(static_cast<uint32_t>(payload.size()));

    std::vector<uint8_t> frame;
    frame.reserve(sizeof(network_len) + payload.size());

    const uint8_t *len_ptr = reinterpret_cast<const uint8_t *>(&network_len);
    frame.insert(frame.end(), len_ptr, len_ptr + sizeof(network_len));
    frame.insert(frame.end(), payload.begin(), payload.end());

    // Queue new frame in send buffer
    send_buffer_.insert(send_buffer_.end(), frame.begin(), frame.end());

    // Flush as much as possible to socket
    size_t written = 0;
    while (written < send_buffer_.size()) {
        const size_t remaining = send_buffer_.size() - written;

#ifdef _WIN32
        const int to_send = static_cast<int>(std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));

        int res = send(fd_, reinterpret_cast<const char *>(send_buffer_.data() + written), to_send, 0);

        if (res == SOCKET_ERROR) {
            int error = last_socket_error();
            if (is_interrupted_error(error))
                continue;
            if (is_would_block_error(error))
                break; // Socket is full, retain remaining data in buffer for next call

            close_socket(fd_);
            fd_ = INVALID_OS_SOCKET;
            throw std::runtime_error("Unrecoverable communication error in IPC: Connection reset?");
        }
#else
        ssize_t res = send(fd_, reinterpret_cast<const char *>(send_buffer_.data() + written), remaining, SEND_FLAGS);

        if (res < 0) {
            int error = last_socket_error();
            if (is_interrupted_error(error))
                continue;
            if (is_would_block_error(error))
                break; // Socket is full, retain remaining data in buffer for next call

            close_socket(fd_);
            fd_ = INVALID_OS_SOCKET;
            throw std::runtime_error("Unrecoverable communication error in IPC: Connection reset?");
        }
#endif

        if (res == 0) {
            close_socket(fd_);
            fd_ = INVALID_OS_SOCKET;
            throw std::runtime_error("Unrecoverable communication error in IPC: Connection reset?");
        }

        written += static_cast<size_t>(res);
    }

    // Erase bytes that were successfully sent
    if (written > 0)
        send_buffer_.erase(send_buffer_.begin(), send_buffer_.begin() + written);
}

std::optional<luxon::ser::Message> IPC::receive_message() {
    if (fd_ == INVALID_OS_SOCKET)
        return std::nullopt;

    // Drain socket and append to buffer
    uint8_t chunk[4096];
    while (true) {
#ifdef _WIN32
        int bytes_read = recv(fd_, reinterpret_cast<char *>(chunk), static_cast<int>(sizeof(chunk)), 0);
        if (bytes_read > 0) {
            recv_buffer_.insert(recv_buffer_.end(), chunk, chunk + bytes_read);
        } else if (bytes_read == 0) {
            // Connection closed by the other end gracefully
            close_socket(fd_);
            fd_ = INVALID_OS_SOCKET;
            throw std::runtime_error("Unrecoverable communication error in IPC: Connection reset");
        } else {
            int error = last_socket_error();
            if (is_interrupted_error(error))
                continue;
            if (is_would_block_error(error))
                break; // Buffer empty, stop reading

            // Socket Error
            close_socket(fd_);
            fd_ = INVALID_OS_SOCKET;
            throw std::runtime_error("Unrecoverable communication error in IPC: Socket error");
        }
#else
        ssize_t bytes_read = recv(fd_, chunk, sizeof(chunk), 0);
        if (bytes_read > 0) {
            recv_buffer_.insert(recv_buffer_.end(), chunk, chunk + bytes_read);
        } else if (bytes_read == 0) {
            // Connection closed by the other end gracefully
            close_socket(fd_);
            fd_ = INVALID_OS_SOCKET;
            throw std::runtime_error("Unrecoverable communication error in IPC: Connection reset");
        } else {
            int error = last_socket_error();
            if (is_interrupted_error(error))
                continue;
            if (is_would_block_error(error))
                break; // Buffer empty, stop reading

            // Socket Error
            close_socket(fd_);
            fd_ = INVALID_OS_SOCKET;
            throw std::runtime_error("Unrecoverable communication error in IPC: Socket error");
        }
#endif
    }

    // Check if there is enough data to parse framing length header
    if (recv_buffer_.size() < sizeof(uint32_t))
        return std::nullopt;

    uint32_t network_len;
    std::memcpy(&network_len, recv_buffer_.data(), sizeof(uint32_t));
    uint32_t payload_len = ntohl(network_len);

    // Ensure complete message has arrived
    if (recv_buffer_.size() < sizeof(uint32_t) + payload_len)
        return std::nullopt; // Partial message, wait for more data in future calls

    // Extract payload
    luxon::ser::ByteArray payload(recv_buffer_.begin() + sizeof(uint32_t), recv_buffer_.begin() + sizeof(uint32_t) + payload_len);

    // Discard processed sequence from buffer so subsequent messages drop down
    recv_buffer_.erase(recv_buffer_.begin(), recv_buffer_.begin() + sizeof(uint32_t) + payload_len);

    // Deserialize
    auto msg_res = protocol.Deserialize(payload);

    if (!msg_res.has_value())
        throw std::runtime_error("Failed to deserialize IPC message: " + msg_res.error().message);

    return std::move(msg_res.value());
}
} // namespace server
