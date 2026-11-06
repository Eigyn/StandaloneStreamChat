using System;
using System.Linq;
using BeatSaberMarkupLanguage;
using HMUI;
using SongCore;
using SimpleBSStreamChat.UI;
using UnityEngine;
using UnityEngine.SceneManagement;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Songs
{
    internal sealed class LevelNavigator
    {
        private const string ReturnToMenuMessage = "return to menu and accept again";

        private readonly MainThreadDispatcher _dispatcher;
        private readonly IPALogger _log;

        public LevelNavigator(MainThreadDispatcher dispatcher, IPALogger log)
        {
            _dispatcher = dispatcher;
            _log = log;
        }

        public bool TryGoToLevel(BeatmapLevel level, out string error)
        {
            error = "";
            if (level == null)
            {
                error = "Map failed to load";
                return false;
            }
            if (SceneManager.GetActiveScene().name != "MainMenu")
            {
                error = ReturnToMenuMessage;
                return false;
            }
            if (Loader.CustomLevelsPack == null)
            {
                error = "Custom Levels pack is not ready";
                return false;
            }

            Action? unsubscribeSelectionHook = null;
            try
            {
                var main = BeatSaberUI.MainFlowCoordinator;
                var solo = BeatSaberUI.DiContainer.Resolve<SoloFreePlayFlowCoordinator>();
                var filtering = BeatSaberUI.DiContainer.Resolve<LevelFilteringNavigationController>();
                var collection = BeatSaberUI.DiContainer.Resolve<LevelCollectionNavigationController>();
                var child = main.childFlowCoordinator;

                // Anything presented that is NOT the solo flow (campaign/party, this
                // mod's own settings, etc.) is unsafe to navigate from — park the
                // request with the hint instead of tearing that flow down.
                if (child != null && child != solo)
                {
                    error = ReturnToMenuMessage;
                    return false;
                }
                // Solo flow with the results/completion screen up: accepting is
                // disabled there (the ✓ is grayed), so refuse to match it.
                if (child == solo && MenuState.IsSoloResultsShowing())
                {
                    error = ReturnToMenuMessage;
                    return false;
                }

                var key = PickBeatmapKey(level);
                SetLastSelectedBeatmap(key);

                if (child == solo)
                {
                    // Already in the solo flow — e.g. the song list you're returned
                    // to from the results screen. Select on the LIVE coordinator
                    // rather than dismiss + re-present the same instance: that
                    // same-frame teardown is what corrupted the HMUI stack and locked
                    // the menu. SelectLevel is non-destructive, so it is safe even
                    // while the results screen sits on top. Best-effort: it lands the
                    // selection when the requested level is in the collection being
                    // shown (the usual case after playing a custom request); the
                    // enqueued calls are wrapped by the dispatcher, so a miss just
                    // logs and the map is still installed. Guarded so a user who backs
                    // out before it fires isn't yanked.
                    _dispatcher.Enqueue(() => { if (IsSoloForemost()) collection.SelectLevel(level); });
                    _dispatcher.EnqueueAfterFrames(3, () => { if (IsSoloForemost()) collection.SelectLevel(level); });
                    _log.Info($"Selected requested map on the live solo flow: {level.levelID} / {level.songName}");
                    return true;
                }

                // child == null: present the solo flow fresh, which guarantees the
                // All Songs pack and the selection.
                unsubscribeSelectionHook = SelectRequestedLevelAfterAllSongsLoads(filtering, collection, level);

                var state = new LevelSelectionFlowCoordinator.State(
                    SelectLevelCategoryViewController.LevelCategory.All,
                    Loader.CustomLevelsPack,
                    key ?? default(BeatmapKey),
                    level);
                solo.Setup(state);
                main.PresentFlowCoordinator(solo, null, ViewController.AnimationDirection.Horizontal, immediately: true);

                // If All Songs was already warm, the filtered-pack event can happen
                // during presentation; this delayed select covers that path too.
                // Guard each delayed select: if the user has since backed out (the
                // solo flow is no longer the presented child), do nothing.
                _dispatcher.EnqueueAfterFrames(2, () => { if (main.childFlowCoordinator == solo) collection.SelectLevel(level); });
                _dispatcher.EnqueueAfterFrames(12, () => { if (main.childFlowCoordinator == solo) collection.SelectLevel(level); });

                _log.Info($"Navigated to requested map via All Songs: {level.levelID} / {level.songName}");
                return true;
            }
            catch (Exception ex)
            {
                unsubscribeSelectionHook?.Invoke();
                error = "navigation failed: " + ex.Message;
                _log.Warn($"Failed to navigate to requested map '{level.levelID}': {ex.Message}");
                return false;
            }
        }

        private Action SelectRequestedLevelAfterAllSongsLoads(LevelFilteringNavigationController filtering,
            LevelCollectionNavigationController collection, BeatmapLevel level)
        {
            Action<LevelFilteringNavigationController, BeatmapLevelPack, GameObject, LevelSelectionOptions> handler = null!;
            handler = (controller, pack, noDataInfoPrefab, options) =>
            {
                if (!options.isFiltered)
                    return;

                filtering.didSelectBeatmapLevelPackEvent -= handler;
                // Only select if the solo flow is still the presented child; the
                // user may have backed out before this fired.
                _dispatcher.Enqueue(() => { if (IsSoloForemost()) collection.SelectLevel(level); });
                _dispatcher.EnqueueAfterFrames(1, () => { if (IsSoloForemost()) collection.SelectLevel(level); });
            };
            filtering.didSelectBeatmapLevelPackEvent += handler;
            _dispatcher.EnqueueAfterFrames(300, () => filtering.didSelectBeatmapLevelPackEvent -= handler);
            return () => filtering.didSelectBeatmapLevelPackEvent -= handler;
        }

        private static bool IsSoloForemost() =>
            BeatSaberUI.MainFlowCoordinator?.childFlowCoordinator is SoloFreePlayFlowCoordinator;

        private static void SetLastSelectedBeatmap(BeatmapKey? key)
        {
            if (!key.HasValue)
                return;

            var playerDataModel = BeatSaberUI.DiContainer.TryResolve<PlayerDataModel>();
            if (playerDataModel == null)
                return;

            var value = key.Value;
            playerDataModel.playerData.SetLastSelectedBeatmapDifficulty(value.difficulty);
            if (value.beatmapCharacteristic != null)
                playerDataModel.playerData.SetLastSelectedBeatmapCharacteristic(value.beatmapCharacteristic);
        }

        private static BeatmapKey? PickBeatmapKey(BeatmapLevel level)
        {
            var keys = level.GetBeatmapKeys()
                .Where(k => k.IsValid())
                .OrderBy(k => IsStandard(k.beatmapCharacteristic) ? 0 : 1)
                .ThenBy(k => DifficultyDistanceFromNormal(k.difficulty))
                .ThenBy(k => k.beatmapCharacteristic.sortingOrder)
                .ThenBy(k => (int)k.difficulty)
                .ToList();
            return keys.Count == 0 ? (BeatmapKey?)null : keys[0];
        }

        private static bool IsStandard(BeatmapCharacteristicSO characteristic)
        {
            var serialized = characteristic.serializedName;
            return string.Equals(serialized, "Standard", StringComparison.OrdinalIgnoreCase);
        }

        private static int DifficultyDistanceFromNormal(BeatmapDifficulty difficulty)
        {
            switch (difficulty)
            {
                case BeatmapDifficulty.Normal: return 0;
                case BeatmapDifficulty.Hard: return 1;
                case BeatmapDifficulty.Easy: return 2;
                case BeatmapDifficulty.Expert: return 3;
                case BeatmapDifficulty.ExpertPlus: return 4;
                default: return 10;
            }
        }
    }
}
