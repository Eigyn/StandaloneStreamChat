#pragma once

// Where the player is in the menus, shared by the request list (it dims
// Accept), the installer (it only refreshes SongCore and opens a song where
// that's safe) and the navigator, so they can never disagree. Main thread.
namespace SimpleBSStreamChat::Songs::MenuState {
    /// Any menu screen: not during a song or a menu transition.
    bool InMenu();
    /// The solo or party results screen. Accepting from it used to crash the
    /// game (seen on the PC build), so Accept stays off there.
    bool ResultsShowing();
    /// Accept is live on the main menu and Solo song list only.
    bool CanAccept();
    /// The main menu itself, with nothing presented over it.
    bool AtMainMenuRoot();
    /// Solo mode's song list, not its results, practice or search screens.
    bool SongListShowing();
    /// Either of the above: where SongCore may reload and a song may be
    /// opened without closing anything the player has open.
    bool CanOpenSong();
}
