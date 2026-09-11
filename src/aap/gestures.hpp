#pragma once

#include "aap/head_tracking.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace aap
{

enum class Gesture
{
    Nod,  // "yes" - vertical
    Shake // "no"  - horizontal
};

constexpr std::string_view GestureName(Gesture gesture)
{
    return gesture == Gesture::Nod ? "nod" : "shake";
}

// The AirPods stream raw motion; they do not classify gestures themselves, so
// nod and shake are recognised here from the acceleration channels.
//
// A gesture is an oscillation: repeated direction reversals on one axis while
// the other axis stays quiet. The detector tracks turning points on each axis
// and scores the recent run on amplitude, rhythm, sign alternation and
// isolation from the other axis. This mirrors LibrePods' Android detector,
// including its thresholds - they are the only ones known to work.
class GestureDetector
{
public:
    void Reset()
    {
        horizontal.Clear();
        vertical.Clear();
        peakIntervals.clear();
        movementIntervals.clear();
        lastTurningPoint = Clock::time_point{};
    }

    std::optional<Gesture> Feed(const HeadSample &sample)
    {
        const auto now = Clock::now();

        if (InCooldown(now))
        {
            return std::nullopt;
        }

        // Values this large are the sensor settling right after start, not motion.
        if (std::abs(sample.horizontalAccel) > kMaxValidValue ||
            std::abs(sample.verticalAccel) > kMaxValidValue)
        {
            return std::nullopt;
        }

        horizontal.Push(sample.horizontalAccel, now, *this);
        vertical.Push(sample.verticalAccel, now, *this);

        const auto gesture = Classify();
        if (gesture)
        {
            Reset();
            cooldownUntil = now + kCooldown;
        }

        return gesture;
    }

private:
    using Clock = std::chrono::steady_clock;

    static constexpr double kDirectionChangeSensitivity = 150.0;
    static constexpr double kMinDirectionChange = 50.0;
    static constexpr double kTurningPointThreshold = 400.0;
    static constexpr double kAmplitudeNormaliser = 600.0;
    static constexpr double kRhythmTolerance = 0.5;
    static constexpr double kMinConfidence = 0.7;
    static constexpr double kFastMovementMs = 300.0;
    static constexpr int16_t kMaxValidValue = 6000;
    static constexpr size_t kMinExtremes = 3;
    static constexpr size_t kMaxExtremes = 4;
    static constexpr size_t kSmoothingWindow = 3;
    static constexpr size_t kHistoryLength = 100;
    static constexpr size_t kIntervalHistory = 5;
    static constexpr auto kCooldown = std::chrono::milliseconds(1200);

    struct TurningPoint
    {
        uint64_t index = 0;
        double value = 0.0;
    };

    // One acceleration channel: smoothed history plus the turning points found
    // in it. Peaks and troughs share a list; the sign of the value tells them
    // apart and that is all the scoring needs.
    struct Axis
    {
        std::deque<double> history;
        std::deque<double> smoothing;
        std::vector<TurningPoint> extremes;
        std::optional<bool> increasing;
        uint64_t samples = 0;

        void Clear()
        {
            history.clear();
            smoothing.clear();
            extremes.clear();
            increasing.reset();
            samples = 0;
        }

        void Push(int16_t raw, Clock::time_point now, GestureDetector &owner)
        {
            smoothing.push_back(static_cast<double>(raw));
            if (smoothing.size() > kSmoothingWindow)
            {
                smoothing.pop_front();
            }

            history.push_back(Mean(smoothing));
            if (history.size() > kHistoryLength)
            {
                history.pop_front();
            }

            ++samples;
            DetectTurningPoint(now, owner);
        }

        void DetectTurningPoint(Clock::time_point now, GestureDetector &owner)
        {
            if (history.size() < 2)
            {
                return;
            }

            const double current = history.back();
            const double previous = history[history.size() - 2];

            // Tighten the reversal threshold when the channel is quiet, so a
            // slow deliberate nod still registers.
            const double threshold = std::max(
                kMinDirectionChange, std::min(kDirectionChangeSensitivity, RecentVariance() / 3.0)
            );

            bool rising = increasing.value_or(current > previous);
            const bool reversedDown = rising && current < previous - threshold;
            const bool reversedUp = !rising && current > previous + threshold;

            if (reversedDown || reversedUp)
            {
                if (std::abs(previous) > kTurningPointThreshold)
                {
                    extremes.push_back({samples, previous});
                    owner.RecordInterval(now);
                }

                rising = reversedUp;
            }

            // Latched on every sample, not only on a reversal: the direction
            // has to survive the flat stretch between turns, or the turn that
            // ends it is read as a continuation and never scores.
            increasing = rising;
        }

        double RecentVariance() const
        {
            constexpr size_t kWindow = 4;

            if (history.size() < kWindow)
            {
                return 0.0;
            }

            double mean = 0.0;
            for (size_t i = history.size() - kWindow; i < history.size(); ++i)
            {
                mean += history[i];
            }
            mean /= static_cast<double>(kWindow);

            double variance = 0.0;
            for (size_t i = history.size() - kWindow; i < history.size(); ++i)
            {
                variance += (history[i] - mean) * (history[i] - mean);
            }

            return variance / static_cast<double>(kWindow);
        }
    };

    bool InCooldown(Clock::time_point now) const
    {
        return cooldownUntil != Clock::time_point{} && now < cooldownUntil;
    }

    void RecordInterval(Clock::time_point now)
    {
        if (lastTurningPoint != Clock::time_point{})
        {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTurningPoint);

            Append(peakIntervals, static_cast<double>(elapsed.count()) / 1000.0);
            Append(movementIntervals, static_cast<double>(elapsed.count()));
        }

        lastTurningPoint = now;
    }

    std::optional<Gesture> Classify() const
    {
        const size_t required = RequiredExtremes();

        if (vertical.extremes.size() >= required &&
            Confidence(vertical, horizontal, required) >= kMinConfidence)
        {
            return Gesture::Nod;
        }

        if (horizontal.extremes.size() >= required &&
            Confidence(horizontal, vertical, required) >= kMinConfidence)
        {
            return Gesture::Shake;
        }

        return std::nullopt;
    }

    // A fast gesture produces turning points cheaply, so demand one more of
    // them before believing it.
    size_t RequiredExtremes() const
    {
        if (movementIntervals.empty())
        {
            return kMinExtremes;
        }

        return Mean(movementIntervals) < kFastMovementMs ? kMaxExtremes : kMinExtremes;
    }

    double Confidence(const Axis &axis, const Axis &other, size_t required) const
    {
        constexpr double kAmplitudeWeight = 0.4;
        constexpr double kRhythmWeight = 0.2;
        constexpr double kAlternationWeight = 0.2;
        constexpr double kIsolationWeight = 0.2;
        constexpr double kIsolationGain = 1.2;
        constexpr double kNoAlternationScore = 0.5;
        // Guards the ratio when the other axis is perfectly still.
        constexpr double kIsolationEpsilon = 0.1;

        const auto recent = std::span(axis.extremes).last(required);

        double amplitude = 0.0;
        for (const auto &point : recent)
        {
            amplitude += std::abs(point.value);
        }
        amplitude /= static_cast<double>(recent.size());

        bool alternating = true;
        for (size_t i = 1; i < recent.size(); ++i)
        {
            if ((recent[i].value > 0.0) == (recent[i - 1].value > 0.0))
            {
                alternating = false;
                break;
            }
        }

        const double otherAmplitude = MeanMagnitude(other.history, recent.size() * 2);
        const double isolation =
            std::min(1.0, amplitude / (otherAmplitude + kIsolationEpsilon) * kIsolationGain);

        return (std::min(1.0, amplitude / kAmplitudeNormaliser) * kAmplitudeWeight) +
               (RhythmConsistency() * kRhythmWeight) +
               ((alternating ? 1.0 : kNoAlternationScore) * kAlternationWeight) +
               (isolation * kIsolationWeight);
    }

    // Nodding and shaking are periodic; a run of evenly spaced turning points
    // separates them from an incidental head movement.
    double RhythmConsistency() const
    {
        if (peakIntervals.size() < 2)
        {
            return 0.0;
        }

        const double mean = Mean(peakIntervals);
        if (mean == 0.0)
        {
            return 0.0;
        }

        double spread = 0.0;
        for (const double interval : peakIntervals)
        {
            const double deviation = (interval / mean) - 1.0;
            spread += deviation * deviation;
        }
        spread /= static_cast<double>(peakIntervals.size());

        return std::max(0.0, 1.0 - std::min(1.0, spread / kRhythmTolerance));
    }

    template <typename Container>
    static double Mean(const Container &values)
    {
        if (values.empty())
        {
            return 0.0;
        }

        double sum = 0.0;
        for (const double value : values)
        {
            sum += value;
        }

        return sum / static_cast<double>(values.size());
    }

    static double MeanMagnitude(const std::deque<double> &values, size_t count)
    {
        if (values.empty())
        {
            return 0.0;
        }

        const size_t taken = std::min(count, values.size());
        double sum = 0.0;
        for (size_t i = values.size() - taken; i < values.size(); ++i)
        {
            sum += std::abs(values[i]);
        }

        return sum / static_cast<double>(taken);
    }

    static void Append(std::deque<double> &values, double value)
    {
        values.push_back(value);
        if (values.size() > kIntervalHistory)
        {
            values.pop_front();
        }
    }

    Axis horizontal;
    Axis vertical;
    std::deque<double> peakIntervals;
    std::deque<double> movementIntervals;
    Clock::time_point lastTurningPoint;
    Clock::time_point cooldownUntil;
};

} // namespace aap
