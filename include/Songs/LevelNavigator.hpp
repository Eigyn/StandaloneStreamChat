#pragma once

#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/LevelFilter.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace StandaloneStreamChat::Songs {
    inline constexpr std::string_view ReturnToMenu = "return to the main menu or Solo and select again";

    // No Unity objects retained across frames.
    struct SelectionState {
        struct PreviousView {
            int controllerId;
            int category;
            bool changedCategory = false;
            std::optional<GlobalNamespace::LevelFilter> filter;  // managed fields copied below
            std::optional<std::string> characteristic, searchText;
            std::optional<std::vector<std::string>> limitIds;
        };
        enum class Phase { OpenSolo, ChooseAll, WaitForAll, Filter, WaitForResults, Confirm };
        Phase phase = Phase::OpenSolo;
        float confirmedSince = -1.0f;
        std::string filteredLevelId;
        std::optional<PreviousView> previousView;
    };

    enum class SelectionResult { Pending, Selected, Failed };

    /// Advances one menu step. Selected requires the exact filtered level to
    /// remain selected across frames. Never dismisses a flow or starts gameplay.
    SelectionResult AdvanceSelection(GlobalNamespace::BeatmapLevel* level, SelectionState& state, std::string& error);
    bool SongListIdle();
    /// Remove the temporary ID restriction without disturbing the selected row.
    /// The next user search can then use the complete song library again.
    void ReleaseSelectionFilter(SelectionState const& state, bool selected = false);
}
