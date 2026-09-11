#pragma once

#include "core/logger.hpp"

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>

namespace bt
{

// The kernel's L2CAP SEQPACKET sockets preserve SDU boundaries, so one recv()
// yields exactly one protocol packet. Nothing above this layer reassembles.
class L2capSocket
{
public:
    L2capSocket() = default;

    ~L2capSocket() { Close(); }

    L2capSocket(const L2capSocket &) = delete;
    L2capSocket &operator=(const L2capSocket &) = delete;

    bool Connect(const std::string &address, uint16_t psm)
    {
        Close();

        const auto peer = ParseAddress(address);
        if (!peer)
        {
            LOG_ERROR("Not a Bluetooth address: " + address);
            return false;
        }

        fd = ::socket(AF_BLUETOOTH, SOCK_SEQPACKET | SOCK_CLOEXEC, BTPROTO_L2CAP);
        if (fd < 0)
        {
            LOG_ERROR(std::string("socket(AF_BLUETOOTH): ") + std::strerror(errno));
            return false;
        }

        sockaddr_l2 addr{};
        addr.l2_family = AF_BLUETOOTH;
        addr.l2_psm = htobs(psm);
        addr.l2_bdaddr = *peer;
        addr.l2_bdaddr_type = BDADDR_BREDR;

        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            LOG_ERROR("connect(" + address + "): " + std::strerror(errno));
            Close();
            return false;
        }

        return true;
    }

    void Close()
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }

    bool IsOpen() const { return fd >= 0; }

    // Handed out so an external event loop can watch the link rather than this
    // class owning a blocking poll. Read-only; the socket stays owned here.
    int Descriptor() const { return fd; }

    bool Send(std::span<const uint8_t> packet) const
    {
        if (!IsOpen())
        {
            return false;
        }

        const ssize_t written = ::send(fd, packet.data(), packet.size(), MSG_NOSIGNAL);
        if (written != static_cast<ssize_t>(packet.size()))
        {
            LOG_ERROR(std::string("send: ") + std::strerror(errno));
            return false;
        }

        return true;
    }

    enum class ReadResult
    {
        Packet,
        Timeout,
        Closed
    };

    // Returns one whole packet in `out`, sized to the bytes actually received.
    ReadResult Receive(std::span<uint8_t> buffer, std::span<uint8_t> &out, int timeoutMs) const
    {
        out = buffer.first(0);

        if (!IsOpen())
        {
            return ReadResult::Closed;
        }

        pollfd pfd{fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, timeoutMs);

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                return ReadResult::Timeout;
            }
            LOG_ERROR(std::string("poll: ") + std::strerror(errno));
            return ReadResult::Closed;
        }

        if (ready == 0)
        {
            return ReadResult::Timeout;
        }

        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        {
            return ReadResult::Closed;
        }

        const ssize_t received = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (received < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                return ReadResult::Timeout;
            }
            LOG_ERROR(std::string("recv: ") + std::strerror(errno));
            return ReadResult::Closed;
        }

        if (received == 0)
        {
            return ReadResult::Closed;
        }

        out = buffer.first(static_cast<size_t>(received));
        return ReadResult::Packet;
    }

private:
    // Hand-rolled rather than str2ba() so the build needs only the BlueZ
    // headers, not libbluetooth. bdaddr_t stores the address in reverse order.
    static std::optional<bdaddr_t> ParseAddress(const std::string &address)
    {
        constexpr size_t kByteCount = 6;
        constexpr size_t kAddressLength = (kByteCount * 3) - 1; // "XX:XX:XX:XX:XX:XX"
        constexpr size_t kNibbleShift = 4;

        if (address.size() != kAddressLength)
        {
            return std::nullopt;
        }

        bdaddr_t parsed{};

        for (size_t i = 0; i < kByteCount; ++i)
        {
            const size_t offset = i * 3;

            if (i > 0 && address[offset - 1] != ':')
            {
                return std::nullopt;
            }

            const auto high = HexDigit(address[offset]);
            const auto low = HexDigit(address[offset + 1]);

            if (!high || !low)
            {
                return std::nullopt;
            }

            parsed.b[kByteCount - 1 - i] = static_cast<uint8_t>((*high << kNibbleShift) | *low);
        }

        return parsed;
    }

    static std::optional<uint8_t> HexDigit(char digit)
    {
        if (digit >= '0' && digit <= '9')
        {
            return static_cast<uint8_t>(digit - '0');
        }

        if (digit >= 'a' && digit <= 'f')
        {
            return static_cast<uint8_t>(digit - 'a' + 10);
        }

        if (digit >= 'A' && digit <= 'F')
        {
            return static_cast<uint8_t>(digit - 'A' + 10);
        }

        return std::nullopt;
    }

    int fd = -1;
};

} // namespace bt
