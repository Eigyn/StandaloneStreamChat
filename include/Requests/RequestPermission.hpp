#pragma once

namespace StandaloneStreamChat::Requests {
    /// Config::whoCanRequest values. Anything else (a typo in the config
    /// file) is read as BroadcasterOnly: a mistake can close requests,
    /// never open them to everyone.
    enum WhoCanRequest : int { Everyone = 0, ModeratorsAndBroadcaster = 1, BroadcasterOnly = 2 };

    inline bool IsWhoCanRequest(int value) { return value >= Everyone && value <= BroadcasterOnly; }

    inline bool MayRequest(int whoCanRequest, bool isBroadcaster, bool isModerator) {
        switch (whoCanRequest) {
            case Everyone:
                return true;
            case ModeratorsAndBroadcaster:
                return isBroadcaster || isModerator;
            default:
                return isBroadcaster;
        }
    }
}
