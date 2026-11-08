#include "Songs/LevelNavigator.hpp"
#include "Songs/MenuState.hpp"
#include "logging.hpp"

#include "bsml/shared/Helpers/getters.hpp"
#ifdef SSC_BS_1_40
#include "beatsaber-hook/shared/utils/byref.hpp"
#else
#include "beatsaber-hook/shared/byref.hpp"
#endif
#include "songcore/shared/SongCore.hpp"

#include "GlobalNamespace/BeatmapDifficultyMask.hpp"
#include "GlobalNamespace/LevelCollectionNavigationController.hpp"
#include "GlobalNamespace/LevelCollectionTableView.hpp"
#include "GlobalNamespace/LevelCollectionViewController.hpp"
#include "GlobalNamespace/LevelFilter.hpp"
#include "GlobalNamespace/LevelFilteringNavigationController.hpp"
#include "GlobalNamespace/LevelSearchViewController.hpp"
#include "GlobalNamespace/LevelSelectionFlowCoordinator.hpp"
#include "GlobalNamespace/LevelSelectionNavigationController.hpp"
#include "GlobalNamespace/MainFlowCoordinator.hpp"
#include "GlobalNamespace/PlayerSensitivityFlag.hpp"
#include "GlobalNamespace/SelectLevelCategoryViewController.hpp"
#include "GlobalNamespace/SoloFreePlayFlowCoordinator.hpp"
#include "GlobalNamespace/SongPackMask.hpp"
#include "HMUI/IconSegmentedControl.hpp"
#include "HMUI/ViewController.hpp"
#include "System/Collections/Generic/IReadOnlyCollection_1.hpp"
#include "System/Collections/Generic/IReadOnlyList_1.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Time.hpp"

#include <exception>

namespace StandaloneStreamChat::Songs {
    namespace {
        using Category = GlobalNamespace::SelectLevelCategoryViewController::LevelCategory;

        GlobalNamespace::SoloFreePlayFlowCoordinator* Solo() {
            auto main = BSML::Helpers::GetMainFlowCoordinator();
            return main ? main->_soloFreePlayFlowCoordinator.ptr() : nullptr;
        }

        bool Matches(GlobalNamespace::BeatmapLevel* actual, GlobalNamespace::BeatmapLevel* expected) {
            return actual && actual->levelID == expected->levelID;
        }

        bool ContainsOnlyRequestedLevel(GlobalNamespace::LevelCollectionTableView* table, GlobalNamespace::BeatmapLevel* level) {
            auto levels = table ? table->_beatmapLevels : nullptr;
            return levels && levels->i___System__Collections__Generic__IReadOnlyCollection_1_T_()->get_Count() == 1 &&
                   Matches(levels->get_Item(0), level);
        }

        bool SelectCategory(GlobalNamespace::LevelFilteringNavigationController* filtering, Category category) {
            auto selector = filtering->_selectLevelCategoryViewController;
            auto segments = selector ? selector->_levelFilterCategoryIconSegmentedControl : nullptr;
            auto categories = selector ? selector->_levelCategoryInfos : nullptr;
            if (!segments || !categories)
                return false;
            for (int i = 0; i < static_cast<int>(categories.size()); ++i) {
                if (categories[i]->levelCategory == category) {
                    segments->SelectCellWithNumber(i);
                    selector->LevelFilterCategoryIconSegmentedControlDidSelectCell(segments, i);
                    return true;
                }
            }
            return false;
        }

        void Refresh(GlobalNamespace::LevelSearchViewController* search, GlobalNamespace::LevelFilter& filter) {
#ifdef SSC_BS_1_40
            search->Refresh(byref(filter));
#else
            search->Refresh(by_ref(filter));
#endif
        }

        SelectionState::PreviousView CaptureView(GlobalNamespace::LevelFilteringNavigationController* filtering) {
            SelectionState::PreviousView previous{filtering->GetInstanceID(), static_cast<int>(filtering->get_selectedLevelCategory())};
            if (auto search = filtering->_levelSearchViewController) {
                auto& filter = previous.filter.emplace(search->_currentSearchFilter);
                if (filter.characteristicSerializedName)
                    previous.characteristic = static_cast<std::string>(filter.characteristicSerializedName);
                if (filter.searchText)
                    previous.searchText = static_cast<std::string>(filter.searchText);
                if (filter.limitIds) {
                    auto& ids = previous.limitIds.emplace();
                    for (auto id : filter.limitIds)
                        ids.push_back(static_cast<std::string>(id));
                }
                filter.characteristicSerializedName = nullptr;
                filter.searchText = nullptr;
                filter.limitIds = nullptr;
            }
            return previous;
        }

        GlobalNamespace::LevelFilter RestoreFilter(SelectionState::PreviousView const& previous) {
            auto filter = *previous.filter;
            if (previous.characteristic)
                filter.characteristicSerializedName = *previous.characteristic;
            if (previous.searchText)
                filter.searchText = *previous.searchText;
            if (previous.limitIds) {
                filter.limitIds = ArrayW<StringW>(previous.limitIds->size());
                for (size_t i = 0; i < previous.limitIds->size(); ++i)
                    filter.limitIds[i] = (*previous.limitIds)[i];
            }
            return filter;
        }
    }

    SelectionResult AdvanceSelection(GlobalNamespace::BeatmapLevel* level, SelectionState& state, std::string& error) {
        try {
            auto main = BSML::Helpers::GetMainFlowCoordinator();
            auto solo = Solo();
            if (!level || !main || !solo) {
                error = "requested map or Solo is not ready";
                return SelectionResult::Failed;
            }
            if (state.phase == SelectionState::Phase::OpenSolo) {
                if (MenuState::AtMainMenuRoot()) {
                    // Wait for SongCore's levels, then open straight into All
                    // Songs, where every request is selected.
                    auto pack = SongCore::API::Loading::GetCustomLevelPack();
                    if (!pack)
                        return SelectionResult::Pending;
                    auto initial = GlobalNamespace::LevelSelectionFlowCoordinator::State::New_ctor(pack, level);
                    initial->levelCategory.hasValue = true;
                    initial->levelCategory.value = Category::All;
                    solo->Setup(initial);
                    main->PresentFlowCoordinator(solo, nullptr, HMUI::ViewController::AnimationDirection::Horizontal, false, false);
                    state.phase = SelectionState::Phase::ChooseAll;
                    return SelectionResult::Pending;
                }
                if (!MenuState::SongListShowing()) {
                    error = ReturnToMenu;
                    return SelectionResult::Failed;
                }
                state.phase = SelectionState::Phase::ChooseAll;
            }
            // Let presentation settle, but never pull the player out of another flow.
            if (main->get_childFlowCoordinator().ptr() != static_cast<HMUI::FlowCoordinator*>(solo) || MenuState::ResultsShowing()) {
                error = ReturnToMenu;
                return SelectionResult::Failed;
            }
            if (main->get_isInTransition() || solo->get_isInTransition())
                return SelectionResult::Pending;
            if (!MenuState::SongListShowing()) {
                error = ReturnToMenu;
                return SelectionResult::Failed;
            }
            if (!SongListIdle())
                return SelectionResult::Pending;

            auto navigation = solo->levelSelectionNavigationController;
            auto filtering = navigation->_levelFilteringNavigationController;
            auto collection = navigation->_levelCollectionNavigationController;
            if (!filtering || !collection)
                return SelectionResult::Pending;
            auto view = collection->_levelCollectionViewController;
            auto table = view ? view->_levelCollectionTableView : nullptr;
            switch (state.phase) {
                case SelectionState::Phase::ChooseAll: {
                    // Already selected counts only in All Songs; a selection in
                    // Custom Songs or a pack still moves to All Songs.
                    if (table && filtering->get_selectedLevelCategory() == Category::All && Matches(table->_selectedBeatmapLevel, level)) {
                        state.phase = SelectionState::Phase::Confirm;
                        return SelectionResult::Pending;
                    }
                    if (!state.previousView)
                        state.previousView = CaptureView(filtering);
                    if (filtering->get_selectedLevelCategory() != Category::All) {
                        auto selector = filtering->_selectLevelCategoryViewController;
                        auto segments = selector ? selector->_levelFilterCategoryIconSegmentedControl : nullptr;
                        auto categories = selector ? selector->_levelCategoryInfos : nullptr;
                        if (!segments || !categories)
                            return SelectionResult::Pending;
                        state.previousView->changedCategory = true;
                        if (!SelectCategory(filtering, Category::All)) {
                            error = "All Songs is unavailable";
                            return SelectionResult::Failed;
                        }
                    }
                    state.phase = SelectionState::Phase::WaitForAll;
                    return SelectionResult::Pending;
                }
                case SelectionState::Phase::WaitForAll: {
                    auto search = filtering->_levelSearchViewController;
                    if (filtering->get_selectedLevelCategory() != Category::All || !search || search->_isInTransition ||
                        !search->get_gameObject()->get_activeInHierarchy())
                        return SelectionResult::Pending;
                    state.phase = SelectionState::Phase::Filter;
                    return SelectionResult::Pending;
                }
                case SelectionState::Phase::Filter: {
                    auto search = filtering->_levelSearchViewController;
                    if (!search || search->_isInTransition)
                        return SelectionResult::Pending;
                    GlobalNamespace::LevelFilter filter{};
                    filter.songOwned = true;
                    filter.songNotOwned = false;
                    filter.songUnplayed = false;
                    filter.difficulties = GlobalNamespace::BeatmapDifficultyMask(0);
                    filter.songPacks = GlobalNamespace::SongPackMask::get_all();
                    filter.characteristicSerializedName = nullptr;
                    filter.minBpm = 0.0f;
                    filter.maxBpm = 0.0f;
                    filter.sensitivity = GlobalNamespace::PlayerSensitivityFlag::Unknown;
                    filter.searchText = u"";
                    filter.limitIds = ArrayW<StringW>({level->levelID});
                    state.filteredLevelId = static_cast<std::string>(level->levelID);
                    Refresh(search, filter);
                    INFO("Filtering All Songs to requested level {}", static_cast<std::string>(level->levelID));
                    state.phase = SelectionState::Phase::WaitForResults;
                    return SelectionResult::Pending;
                }
                case SelectionState::Phase::WaitForResults: {
                    // Inspect the resulting collection, not just a completed call or delay.
                    if (!ContainsOnlyRequestedLevel(table, level))
                        return SelectionResult::Pending;
                    collection->SelectLevel(level);
                    state.phase = SelectionState::Phase::Confirm;
                    return SelectionResult::Pending;
                }
                case SelectionState::Phase::Confirm: {
                    if (!table || filtering->get_selectedLevelCategory() != Category::All || !Matches(table->_selectedBeatmapLevel, level) ||
                        (!state.filteredLevelId.empty() && !ContainsOnlyRequestedLevel(table, level))) {
                        if (state.filteredLevelId.empty())
                            state.phase = SelectionState::Phase::ChooseAll;
                        state.confirmedSince = -1.0f;
                        return SelectionResult::Pending;
                    }
                    auto now = UnityEngine::Time::get_unscaledTime();
                    if (state.confirmedSince < 0.0f) {
                        state.confirmedSince = now;
                        return SelectionResult::Pending;
                    }
                    if (now - state.confirmedSince < 0.1f)
                        return SelectionResult::Pending;
                    INFO("Confirmed requested level selected: {}", static_cast<std::string>(level->levelID));
                    ReleaseSelectionFilter(state, true);
                    return SelectionResult::Selected;
                }
                default:
                    return SelectionResult::Pending;
            }
        } catch (std::exception const& ex) {
            ERROR("Selecting requested map failed: {}", ex.what());
            error = "could not select the requested map";
        } catch (...) {
            ERROR("Selecting requested map failed");
            error = "could not select the requested map";
        }
        return SelectionResult::Failed;
    }

    void ReleaseSelectionFilter(SelectionState const& state, bool selected) {
        if (!state.previousView)
            return;
        try {
            auto solo = Solo();
            if (!solo || !solo->levelSelectionNavigationController)
                return;
            auto filtering = solo->levelSelectionNavigationController->_levelFilteringNavigationController;
            auto const& previous = *state.previousView;
            if (!filtering || filtering->GetInstanceID() != previous.controllerId)
                return;
            auto search = filtering ? filtering->_levelSearchViewController : nullptr;
            if (!search)
                return;
            auto filter = search->_currentSearchFilter;
            bool ownsFilter = !state.filteredLevelId.empty() && filter.limitIds && filter.limitIds.size() == 1 &&
                              static_cast<std::string>(filter.limitIds[0]) == state.filteredLevelId;
            if (selected && ownsFilter) {
                filter.limitIds = nullptr;
                search->_currentSearchFilter = filter;
            } else if (!selected && (ownsFilter || state.filteredLevelId.empty())) {
                bool menuVisible = MenuState::SongListShowing();
                if (previous.changedCategory && menuVisible && filtering->get_selectedLevelCategory() == Category::All)
                    SelectCategory(filtering, static_cast<Category>(previous.category));
                if (ownsFilter && previous.filter) {
                    filter = RestoreFilter(previous);
                    if (menuVisible && !previous.changedCategory)
                        Refresh(search, filter);
                    else
                        search->_currentSearchFilter = filter;
                }
            }
        } catch (...) {
            WARNING("Could not release the temporary request filter");
        }
    }

    bool SongListIdle() {
        auto solo = Solo();
        if (!solo || !solo->levelSelectionNavigationController)
            return false;
        auto filtering = solo->levelSelectionNavigationController->_levelFilteringNavigationController;
        return filtering && filtering->_cancellationTokenSource == nullptr;
    }
}
