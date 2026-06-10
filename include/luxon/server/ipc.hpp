// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <optional>
#include <vector>
#include <cstdint>
#include <string>
#include <string_view>
#include <luxon/ser_interface.hpp>
#ifdef _WIN32
#include <winsock2.h>
#endif

namespace server {
/// \brief Abstracts Inter-Process Communication (IPC) for games
/// Provides reliable message stream
class IPC {
public:
#ifdef _WIN32
    using Socket = SOCKET;
    static constexpr Socket INVALID_OS_SOCKET = INVALID_SOCKET;
#else
    using Socket = int;
    static constexpr Socket INVALID_OS_SOCKET = -1;
#endif

    ///
    /// \brief Creates new IPC channel
    /// \return IPC instance if successful
    ///
    static std::optional<IPC> create();

    ///
    /// \brief Constructs an empty, closed IPC channel
    ///
    IPC() {}

    ///
    /// \brief Connects to existing IPC channel using given socket handle
    /// \param fd Socket handle to existing IPC socket
    ///
    explicit IPC(Socket fd);

    ~IPC();

    IPC(IPC&& other) noexcept;
    IPC& operator=(IPC&& other) noexcept;
    IPC(const IPC&) = delete;
    IPC& operator=(const IPC&) = delete;

    ///
    /// \brief Returns socket handle for use in this process
    /// \return Socket handle or INVALID_OS_SOCKET if not open
    ///
    Socket get_fd() const { return fd_; }

    ///
    /// \brief Returns child socket handle for use in child process
    /// \return Child socket handle or INVALID_OS_SOCKET if not created via create()
    ///
    Socket get_child_fd() const { return child_fd_; }

    ///
    ///  \brief Closes the child socket handle in the parent process after process creation
    ///
    void close_child_fd();

    ///
    /// \brief Serializes and sends message to other side
    /// \param msg Luxon serialization message to send
    ///
    void send_message(const luxon::ser::Message& msg);

    ///
    /// \brief Receives and deserializes exactly one message if fully available
    /// \return Deserialized message, or std::nullopt if none/incomplete
    ///
    std::optional<luxon::ser::Message> receive_message();

    ///
    /// \brief Converts a socket handle to a string for process passing
    /// \param socket Socket handle to encode
    /// \return String representation of the socket handle
    ///
    static std::string socket_to_string(Socket socket);

    ///
    /// \brief Converts a string back to a socket handle
    /// \param value String representation of the socket handle
    /// \return Parsed socket handle if valid
    ///
    static std::optional<Socket> socket_from_string(std::string_view value);

    ///
    /// \brief Checks if IPC channel is currently open
    ///
    bool is_open() const { return fd_ != INVALID_OS_SOCKET; }

private:
    IPC(Socket parent_fd, Socket child_fd);

    Socket fd_{INVALID_OS_SOCKET};
    Socket child_fd_{INVALID_OS_SOCKET};

    // Buffers to handle stream fragmentation
    std::vector<uint8_t> recv_buffer_;
    std::vector<uint8_t> send_buffer_;
};
} // namespace server
