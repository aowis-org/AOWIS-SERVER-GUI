#ifndef MAP_ZOOM_ANIMATION_H
#define MAP_ZOOM_ANIMATION_H

#include <algorithm>
#include <cmath>

// Continuous camera zoom motion, independent of any integer imagery LOD.
//
// All quantities are in *octaves* (log2 of the map scale): the 2D camera
// uses its continuous Web Mercator zoom level directly, the globe uses
// -log2(orbit distance), so "positive" is always "zoom in".
//
// Why a critically damped spring instead of the previous design:
// the former keyboard path combined two different motions -- an exponential
// ease toward a tap target (which starts at its *maximum* velocity, 3.4 oct/s
// in the first frame) and, after 0.12 s, a constant-rate hold applied
// directly to the camera which cancelled the remaining ease. Measured at
// 60 Hz that produced 3.4 -> 1.2 -> 1.5 oct/s (lurch, sag, surge) and an
// instantaneous 1.5 -> 0 oct/s stop on key release.
//
// Here there is exactly one motion. Every input only moves the *target*:
//   * a wheel notch / button / key tap adds an impulse to the target,
//   * a held key moves the target at a constant rate,
// and the camera follows that target as a critically damped spring. The
// camera velocity is therefore continuous for every input sequence: no
// velocity jump on press, on the tap->hold transition, on reversal, or on
// release, and no overshoot past the target.
//
// The spring is integrated with its exact closed-form solution for a target
// moving linearly during the step, so the trajectory is identical for any
// frame rate or frame-time jitter (verified in map_zoom_animation_test.cpp).
namespace MapZoomAnimation
{
// Spring stiffness. Determines both responsiveness and the release tail:
// a held key at rate r trails its target by 2*r/omega (0.15 octave at
// 1.5 oct/s), which is the smooth deceleration distance after release.
constexpr double SpringAngularFrequency = 20.0;
constexpr double KeyboardTapOctaves = 0.35;
// A large frame gap (debugger, window drag, tab switch) must not launch the
// camera; the spring continues from where it was instead.
constexpr double MaximumStepSeconds = 0.1;
constexpr double SettlePositionEpsilon = 1e-5;
constexpr double SettleVelocityEpsilon = 1e-3;
// Any camera change not written by this motion (footer zoom field, view
// synchronization, fit-to-bounds) cancels the motion instead of fighting it.
constexpr double ExternalChangeEpsilon = 1e-7;
}

class MapZoomMotion
{
public:
    void setLimits(double minimum_octaves, double maximum_octaves)
    {
        this->minimum = std::min(minimum_octaves, maximum_octaves);
        this->maximum = std::max(minimum_octaves, maximum_octaves);
        this->target_octaves = clamp(this->target_octaves);
    }

    bool active() const { return this->is_active; }
    int holdDirection() const { return this->hold_direction; }
    double target() const { return this->target_octaves; }
    double velocity() const { return this->velocity_octaves_per_second; }

    // Stops immediately; the camera keeps its current value.
    void cancel()
    {
        this->is_active = false;
        this->hold_direction = 0;
        this->velocity_octaves_per_second = 0.0;
    }

    // Discrete request (wheel notch, zoom button). Successive requests in the
    // same direction accumulate; a request against a pending target starts
    // from the visible camera so reversal responds immediately.
    void addImpulse(double current_octaves, double octaves)
    {
        if (!std::isfinite(current_octaves) || !std::isfinite(octaves) || octaves == 0.0)
            return;
        begin(current_octaves);
        const double base = octaves * (this->target_octaves - current_octaves) > 0.0
            ? this->target_octaves
            : current_octaves;
        this->target_octaves = clamp(base + octaves);
    }

    // Held-key request. direction is -1, 0 (released) or +1. A press moves
    // the target immediately at a constant rate (no hold delay, therefore no
    // tap->hold transition). On release, a press shorter than a tap is
    // completed to the full tap distance, so a quick tap is a defined step.
    void setHold(double current_octaves, int direction, double octaves_per_second,
                 double tap_octaves)
    {
        direction = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
        if (!std::isfinite(current_octaves))
            return;
        if (direction == this->hold_direction)
        {
            setHoldRate(octaves_per_second);
            return;
        }

        if (direction != 0 || this->hold_direction != 0)
            begin(current_octaves);

        if (this->hold_direction != 0)
        {
            const double travelled = double(this->hold_direction)
                * (this->target_octaves - this->hold_origin);
            if (travelled < this->hold_tap_octaves)
            {
                this->target_octaves = clamp(this->hold_origin
                    + double(this->hold_direction) * this->hold_tap_octaves);
            }
        }

        if (direction != 0)
        {
            // Same direction as a pending glide: keep accumulating (tap-tap-
            // tap zooms further). Opposite direction: drop the stale target.
            if (double(direction) * (this->target_octaves - current_octaves) <= 0.0)
                this->target_octaves = clamp(current_octaves);
            this->hold_origin = this->target_octaves;
            this->hold_tap_octaves = std::max(0.0, tap_octaves);
        }
        this->hold_direction = direction;
        setHoldRate(octaves_per_second);
    }

    void setHoldRate(double octaves_per_second)
    {
        this->hold_rate = std::isfinite(octaves_per_second)
            ? std::max(0.0, octaves_per_second) : 0.0;
    }

    // Integrates one frame and returns the camera value to apply. current is
    // the camera value as presently stored by the model.
    double advance(double current_octaves, double elapsed_seconds)
    {
        if (!this->is_active || !std::isfinite(current_octaves))
            return current_octaves;
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0)
            return current_octaves;

        if (std::abs(current_octaves - this->written_octaves)
            > MapZoomAnimation::ExternalChangeEpsilon)
        {
            // Someone else moved the camera. Adopt it as the new rest state;
            // a still-held key continues from there on the next frame.
            this->velocity_octaves_per_second = 0.0;
            this->target_octaves = clamp(current_octaves);
            this->hold_origin = this->target_octaves;
            this->written_octaves = current_octaves;
            if (this->hold_direction == 0)
                this->is_active = false;
            return current_octaves;
        }

        const double step = std::min(elapsed_seconds, MapZoomAnimation::MaximumStepSeconds);
        const double omega = MapZoomAnimation::SpringAngularFrequency;

        double rate = double(this->hold_direction) * this->hold_rate;
        if ((rate > 0.0 && this->target_octaves >= this->maximum)
            || (rate < 0.0 && this->target_octaves <= this->minimum))
        {
            rate = 0.0;
        }

        // Exact solution of x'' = omega^2 (g(t) - x) - 2 omega x' for the
        // linearly moving goal g(t) = g0 + rate*t. With e = x - g, the
        // substitution f = e + 2 rate/omega is a homogeneous critically
        // damped oscillator: f(t) = (f0 + (f0' + omega f0) t) exp(-omega t).
        const double lag = 2.0 * rate / omega;
        const double f0 = current_octaves - this->target_octaves + lag;
        const double df0 = this->velocity_octaves_per_second - rate;
        const double b = df0 + omega * f0;
        const double decay = std::exp(-omega * step);
        const double f = (f0 + b * step) * decay;
        const double df = (df0 - omega * b * step) * decay;

        const double moved_target = this->target_octaves + rate * step;
        double next = moved_target + f - lag;
        this->velocity_octaves_per_second = rate + df;
        this->target_octaves = clamp(moved_target);

        if (next >= this->maximum || next <= this->minimum)
        {
            next = clamp(next);
            this->velocity_octaves_per_second = 0.0;
        }

        if (this->hold_direction == 0
            && std::abs(next - this->target_octaves) <= MapZoomAnimation::SettlePositionEpsilon
            && std::abs(this->velocity_octaves_per_second)
                <= MapZoomAnimation::SettleVelocityEpsilon)
        {
            next = this->target_octaves;
            this->velocity_octaves_per_second = 0.0;
            this->is_active = false;
        }
        this->written_octaves = next;
        return next;
    }

private:
    double clamp(double value) const
    {
        return std::clamp(value, this->minimum, this->maximum);
    }

    void begin(double current_octaves)
    {
        if (this->is_active)
            return;
        this->is_active = true;
        this->velocity_octaves_per_second = 0.0;
        this->target_octaves = clamp(current_octaves);
        this->hold_origin = this->target_octaves;
        this->written_octaves = current_octaves;
    }

    double minimum = -1e9;
    double maximum = 1e9;
    double target_octaves = 0.0;
    double written_octaves = 0.0;
    double velocity_octaves_per_second = 0.0;
    double hold_origin = 0.0;
    double hold_rate = 0.0;
    double hold_tap_octaves = 0.0;
    int hold_direction = 0;
    bool is_active = false;
};

#endif // MAP_ZOOM_ANIMATION_H
