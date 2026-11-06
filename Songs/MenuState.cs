using BeatSaberMarkupLanguage;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace SimpleBSStreamChat.Songs
{
    /// <summary>
    /// The single source of truth for "may the streamer act on a request right now".
    /// Used by the request UI (to gray the Accept button), the navigator, and the
    /// SongCore refresh so all three agree on exactly one rule and can never diverge.
    /// </summary>
    internal static class MenuState
    {
        /// <summary>
        /// True only where accepting is allowed: the top-level main menu (nothing
        /// presented) or the solo song list. False on the results/completion screen,
        /// in a song, or under any non-solo flow (campaign/party, this mod's settings).
        /// </summary>
        public static bool CanAcceptNow()
        {
            if (SceneManager.GetActiveScene().name != "MainMenu")
                return false;
            var main = BeatSaberUI.MainFlowCoordinator;
            if (main == null)
                return false;
            var child = main.childFlowCoordinator;
            if (child == null)
                return true;                        // top-level menu
            if (!(child is SoloFreePlayFlowCoordinator))
                return false;                       // campaign/party/settings/etc.
            return !IsSoloResultsShowing();          // solo: song list yes, results no
        }

        /// <summary>
        /// True while the solo results/completion screen is presented. It lives in the
        /// "MainMenu" scene on the solo flow, so a scene-name check can't see it —
        /// detect the active <c>ResultsViewController</c> directly (the same
        /// Resources-scan pattern the map overlay already uses).
        /// </summary>
        public static bool IsSoloResultsShowing()
        {
            foreach (var results in Resources.FindObjectsOfTypeAll<ResultsViewController>())
            {
                if (results != null && results.gameObject.activeInHierarchy)
                    return true;
            }
            return false;
        }
    }
}
