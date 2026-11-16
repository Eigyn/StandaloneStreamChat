#pragma once

#include <chrono>

namespace StandaloneStreamChat::Net {
    /// Notices a connection that went quiet without closing. A path that
    /// dies without a FIN or RST (a NAT timeout, a Wi-Fi roam) never makes a
    /// read fail, so a chat client would otherwise wait on it forever.
    ///
    /// Call Heard() whenever bytes arrive and Poll() after every read. With a
    /// probe delay set, Poll() asks once per quiet spell for a probe (a PING)
    /// that the server must answer; it reports the connection dead once the
    /// silence reaches the dead delay. Not thread-safe.
    class SilenceWatchdog {
       public:
        using Clock = std::chrono::steady_clock;
        enum class Action { None, Probe, Dead };

        /// `probeAfter` of zero never probes: the server keeps the line busy itself.
        SilenceWatchdog(Clock::time_point now, Clock::duration probeAfter, Clock::duration deadAfter)
            : _lastHeard(now), _probeAfter(probeAfter), _deadAfter(deadAfter) {}

        void Heard(Clock::time_point now) {
            _lastHeard = now;
            _probed = false;
        }

        /// Changes how much silence counts as dead, from now on.
        void SetDeadAfter(Clock::duration deadAfter) { _deadAfter = deadAfter; }
        Clock::duration DeadAfter() const { return _deadAfter; }

        Action Poll(Clock::time_point now) {
            auto silent = now - _lastHeard;
            if (silent >= _deadAfter)
                return Action::Dead;
            if (_probeAfter > Clock::duration::zero() && !_probed && silent >= _probeAfter) {
                _probed = true;
                return Action::Probe;
            }
            return Action::None;
        }

       private:
        Clock::time_point _lastHeard;
        Clock::duration _probeAfter;
        Clock::duration _deadAfter;
        bool _probed = false;
    };
}
