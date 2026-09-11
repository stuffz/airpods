#pragma once

#include "aap/packets.hpp"
#include "bt/l2cap_socket.hpp"
#include "core/logger.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace aap
{

// Drives the AAP handshake and hands finished packets upward. The three-step
// opening is mandatory: the buds ignore everything until the handshake lands,
// and send no reports until notifications are requested.
//
//   handshake  ->  ack       ->  declare features
//   features   ->  ack       ->  request notifications  ->  reports flow
//
// Not every firmware acknowledges the feature declaration, so the notification
// request is also sent on a timer after the handshake is acknowledged.
class Session
{
public:
    using PacketHandler = std::function<void(std::span<const uint8_t>)>;

    bool Connect(const std::string &address)
    {
        if (!socket.Connect(address, kPsm))
        {
            return false;
        }

        ready = false;
        notificationsRequested = false;
        handshakeAckedAt = Clock::time_point{};

        if (!Send(kHandshake, "handshake"))
        {
            socket.Close();
            return false;
        }

        return true;
    }

    void Close()
    {
        socket.Close();
        ready = false;
    }

    bool IsOpen() const { return socket.IsOpen(); }

    int Descriptor() const { return socket.Descriptor(); }

    bool IsReady() const { return ready; }

    void OnBattery(PacketHandler handler) { batteryHandler = std::move(handler); }

    void OnHeadTracking(PacketHandler handler) { headTrackingHandler = std::move(handler); }

    void OnNoiseMode(PacketHandler handler) { noiseModeHandler = std::move(handler); }

    void OnProximityKeys(PacketHandler handler) { keysHandler = std::move(handler); }

    void OnReady(std::function<void()> handler) { readyHandler = std::move(handler); }

    bool StartHeadTracking(HeadTrackingVariant variant)
    {
        return Send(StartHeadTrackingPacket(variant), "start head tracking");
    }

    bool StopHeadTracking(HeadTrackingVariant variant)
    {
        return Send(StopHeadTrackingPacket(variant), "stop head tracking");
    }

    bool RequestProximityKeys() { return Send(kRequestProximityKeys, "proximity key request"); }

    bool SetNoiseMode(NoiseMode mode) { return Send(NoiseControlPacket(mode), "noise mode"); }

    // Reads at most one packet, waiting up to timeoutMs. False means the link
    // is gone and the caller should reconnect or exit.
    bool Poll(int timeoutMs)
    {
        std::array<uint8_t, kReceiveBufferSize> buffer{};
        std::span<uint8_t> packet;

        const auto result = socket.Receive(buffer, packet, timeoutMs);

        if (result == bt::L2capSocket::ReadResult::Closed)
        {
            LOG_ERROR("AirPods closed the control link");
            Close();
            return false;
        }

        if (result == bt::L2capSocket::ReadResult::Packet)
        {
            Dispatch(packet);
        }

        RequestNotificationsIfOverdue();
        return true;
    }

private:
    using Clock = std::chrono::steady_clock;

    static constexpr size_t kReceiveBufferSize = 1024;
    // How long to wait for the feature acknowledgement before giving up on it.
    static constexpr auto kFeaturesAckTimeout = std::chrono::milliseconds(1500);

    void Dispatch(std::span<const uint8_t> packet)
    {
        LOG_DEBUG("<- " + ToHex(packet));

        if (StartsWith(packet, kHandshakeAck))
        {
            handshakeAckedAt = Clock::now();
            Send(kSetSpecificFeatures, "feature declaration");
            return;
        }

        if (StartsWith(packet, kFeaturesAck))
        {
            RequestNotifications();
            return;
        }

        if (StartsWith(packet, kProximityKeysReply))
        {
            if (keysHandler)
            {
                keysHandler(packet);
            }
            return;
        }

        if (StartsWith(packet, kNoiseControl))
        {
            if (noiseModeHandler)
            {
                noiseModeHandler(packet);
            }
            return;
        }

        if (StartsWith(packet, kBatteryReport))
        {
            if (batteryHandler)
            {
                batteryHandler(packet);
            }
            return;
        }

        if (StartsWith(packet, kHeadTracking))
        {
            if (headTrackingHandler)
            {
                headTrackingHandler(packet);
            }
            return;
        }
    }

    void RequestNotificationsIfOverdue()
    {
        if (notificationsRequested || handshakeAckedAt == Clock::time_point{})
        {
            return;
        }

        if (Clock::now() - handshakeAckedAt < kFeaturesAckTimeout)
        {
            return;
        }

        LOG_DEBUG("No feature acknowledgement; requesting notifications anyway");
        RequestNotifications();
    }

    void RequestNotifications()
    {
        if (notificationsRequested)
        {
            return;
        }

        notificationsRequested = true;

        if (!Send(kRequestNotifications, "notification request"))
        {
            return;
        }

        ready = true;

        if (readyHandler)
        {
            readyHandler();
        }
    }

    bool Send(std::span<const uint8_t> packet, const char *what)
    {
        LOG_DEBUG(std::string("-> ") + what + ": " + ToHex(packet));

        if (socket.Send(packet))
        {
            return true;
        }

        LOG_ERROR(std::string("Failed to send ") + what);
        return false;
    }

    static std::string ToHex(std::span<const uint8_t> packet)
    {
        constexpr char kDigits[] = "0123456789abcdef";

        std::string hex;
        hex.reserve(packet.size() * 2);

        for (const uint8_t byte : packet)
        {
            hex.push_back(kDigits[byte >> 4]);
            hex.push_back(kDigits[byte & 0x0f]);
        }

        return hex;
    }

    bt::L2capSocket socket;
    PacketHandler batteryHandler;
    PacketHandler headTrackingHandler;
    PacketHandler noiseModeHandler;
    PacketHandler keysHandler;
    std::function<void()> readyHandler;
    Clock::time_point handshakeAckedAt;
    bool notificationsRequested = false;
    bool ready = false;
};

} // namespace aap
