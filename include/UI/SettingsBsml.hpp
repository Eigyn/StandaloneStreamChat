#pragma once

#include <string_view>

namespace StandaloneStreamChat::UI {
    // The settings screen: tabs, labels, ranges and hints.
    // Values bind to SettingsHost properties (C++ names, so camelCase ids).
    // The Filters windows sit at fixed spots on their tab (anchor-pos, size-delta;
    // centre pivots, measured from the tab's top centre) with no layout around
    // them: nested inside layouts, they came out wrong and moved again when
    // the pointer touched them or the tab was pressed.
    // A modifier-container doesn't size its children, so a plain <text> in
    // one needs size-delta-y; otherwise it keeps TMP's default 50-unit height.
    inline constexpr std::string_view SettingsBsml = R"BSML(
<vertical child-control-height='false'>
  <tab-selector tab-tag='tabs'/>

  <tab tags='tabs' tab-name='Connections'>
    <modifier-container child-expand-width='true' horizontal-fit='PreferredSize' pad-top='2'>
      <string-setting text='Twitch Channel' value='twitchChannel' apply-on-change='true' hover-hint='Your Twitch channel name (without #)'/>
      <toggle-setting text='Enable Twitch' value='enableTwitch' apply-on-change='true'/>
      <horizontal pref-height='4'/>
      <string-setting text='YouTube Channel' value='youTubeChannel' apply-on-change='true' hover-hint='Your @handle, channel id (UC...) or an 11-character video id'/>
      <toggle-setting text='Enable YouTube' value='enableYouTube' apply-on-change='true'/>
      <horizontal pref-height='4'/>
      <string-setting text='Kick Channel' value='kickChannel' apply-on-change='true' hover-hint='Your Kick channel slug, profile URL, or numeric chatroom id'/>
      <toggle-setting text='Enable Kick' value='enableKick' apply-on-change='true'/>
    </modifier-container>
  </tab>

  <tab tags='tabs' tab-name='Appearance'>
    <vertical horizontal-fit='PreferredSize' vertical-fit='PreferredSize' spacing='3'>
      <button text='Panel' on-click='openPanel' pref-width='60' pref-height='11' hover-hint='Size, curve, corners, color, opacity and text size of the chat panel.'/>
      <button text='Positions' on-click='openPositions' pref-width='60' pref-height='11' hover-hint='Where the chat panel sits in the menus and during songs.'/>
    </vertical>
  </tab>

  <tab tags='tabs' tab-name='Requests'>
    <horizontal horizontal-fit='PreferredSize' spacing='4' pad-top='1' child-align='UpperCenter'>
      <vertical child-control-height='false' spacing='0'>
        <text text='Queue' font-size='3.2' font-color='#8FD3FF' bold='true' all-uppercase='true' align='BottomLeft' word-wrapping='false' pref-width='64' size-delta-y='5'/>
        <toggle-setting text='Enable Requests' pref-width='64' value='enableSongRequests' apply-on-change='true' hover-hint='Allow chat to add BeatSaver map requests with !bsr followed by the map key. Viewers take back their latest one with !wrongsong or !oops.'/>
        <list-setting text='Who Can Request' pref-width='64' value='whoCanRequest' options='whoCanRequestOptions' apply-on-change='true' hover-hint='Everyone, only mods (and you), or only you.'/>
        <toggle-setting text='Allow Duplicates' pref-width='64' value='allowDuplicates' apply-on-change='true' hover-hint='Allow the same map key to appear more than once in the queue.'/>
        <toggle-setting text='Persist Queue' pref-width='64' value='persistRequestQueue' apply-on-change='true' hover-hint='Restore queued requests after restarting the game.'/>
        <slider-setting text='Max Queue Size' pref-width='64' value='maxQueueSize' min='5' max='100' increment='5' integer-only='true' apply-on-change='true' hover-hint='Viewers cannot add requests once the queue is this long. Mods and broadcaster are exempt.'/>
      </vertical>
      <vertical child-control-height='false' spacing='0'>
        <text text='Viewers' font-size='3.2' font-color='#8FD3FF' bold='true' all-uppercase='true' align='BottomLeft' word-wrapping='false' pref-width='64' size-delta-y='5'/>
        <slider-setting text='Cooldown' pref-width='64' value='requestCooldown' min='0' max='600' increment='5' integer-only='true' apply-on-change='true' hover-hint='Per-viewer cooldown in seconds. Mods and broadcaster are exempt.'/>
        <slider-setting text='Max Per Viewer' pref-width='64' value='maxPerViewer' min='0' max='10' increment='1' integer-only='true' apply-on-change='true' hover-hint='Requests one viewer may have in the queue at once. 0 = no limit. Mods and broadcaster are exempt.'/>
        <text text='Maps' font-size='3.2' font-color='#8FD3FF' bold='true' all-uppercase='true' align='BottomLeft' word-wrapping='false' pref-width='64' size-delta-y='5'/>
        <slider-setting text='Min Rating' pref-width='64' value='minRating' min='0' max='100' increment='5' integer-only='true' apply-on-change='true' hover-hint='Lowest BeatSaver rating in percent a viewer request may have. 0 = off. Mods and broadcaster are exempt.'/>
        <slider-setting text='Max Length' pref-width='64' value='maxLength' min='0' max='20' increment='1' integer-only='true' apply-on-change='true' hover-hint='Longest song in minutes a viewer may request. 0 = off. Mods and broadcaster are exempt.'/>
        <slider-setting text='Max NPS' pref-width='64' value='maxNps' min='0' max='20' increment='0.5' apply-on-change='true' hover-hint='Notes per second a viewer request may need, on its easiest difficulty. 0 = off. Mods and broadcaster are exempt.'/>
      </vertical>
    </horizontal>
  </tab>

  <tab tags='tabs' tab-name='Filters'>
    <vertical bg='round-rect-panel' anchor-min-x='0.5' anchor-max-x='0.5' anchor-min-y='1' anchor-max-y='1' anchor-pos-x='-34' anchor-pos-y='-21' size-delta-x='64' size-delta-y='40' pad='2' spacing='1' child-control-height='false' child-align='UpperCenter'>
      <text text='Users' font-size='4' align='Center' word-wrapping='false' pref-width='60' size-delta-y='5'/>
      <list id='userFilterList' select-cell='userSelected' list-style='Simple' visible-cells='5' cell-size='6' list-width='60' expand-cell='true'/>
    </vertical>
    <horizontal bg='round-rect-panel' anchor-min-x='0.5' anchor-max-x='0.5' anchor-min-y='1' anchor-max-y='1' anchor-pos-x='-34' anchor-pos-y='-48' size-delta-x='64' size-delta-y='10' pad='1' spacing='1' child-align='MiddleCenter'>
      <button text='Add' on-click='addUser' pref-width='18' pref-height='8' hover-hint='Type a name to add to the list.'/>
      <button id='userModeButton' text='Blocklist' on-click='toggleUserMode' pref-width='24' pref-height='8' hover-hint='Blocklist: hide these users, and ignore their requests. Allowlist: show only these users (an empty Allowlist shows everyone; requests still work).'/>
      <button text='Remove' on-click='removeUser' pref-width='18' pref-height='8' hover-hint='Remove the picked name, or all of them.'/>
    </horizontal>
    <vertical bg='round-rect-panel' anchor-min-x='0.5' anchor-max-x='0.5' anchor-min-y='1' anchor-max-y='1' anchor-pos-x='34' anchor-pos-y='-21' size-delta-x='64' size-delta-y='40' pad='2' spacing='1' child-control-height='false' child-align='UpperCenter'>
      <text text='Words' font-size='4' align='Center' word-wrapping='false' pref-width='60' size-delta-y='5'/>
      <list id='wordFilterList' select-cell='wordSelected' list-style='Simple' visible-cells='5' cell-size='6' list-width='60' expand-cell='true'/>
    </vertical>
    <horizontal bg='round-rect-panel' anchor-min-x='0.5' anchor-max-x='0.5' anchor-min-y='1' anchor-max-y='1' anchor-pos-x='34' anchor-pos-y='-48' size-delta-x='64' size-delta-y='10' pad='1' spacing='1' child-align='MiddleCenter'>
      <button text='Add' on-click='addWord' pref-width='18' pref-height='8' hover-hint='Type a word or phrase to add to the list. Whole words only, any case.'/>
      <button id='wordModeButton' text='Blocklist' on-click='toggleWordMode' pref-width='24' pref-height='8' hover-hint='Blocklist: hide messages with these words, and ignore their requests. Allowlist: show only messages with one of them (an empty Allowlist shows everything; requests still work).'/>
      <button text='Remove' on-click='removeWord' pref-width='18' pref-height='8' hover-hint='Remove the picked word, or all of them.'/>
    </horizontal>
    <toggle-setting text='Hide Commands' anchor-min-x='0.5' anchor-max-x='0.5' anchor-min-y='1' anchor-max-y='1' anchor-pos-x='0' anchor-pos-y='-58.5' size-delta-x='64' size-delta-y='7' value='hideCommands' apply-on-change='true' hover-hint='Keep messages that start with ! out of the chat. !bsr and !wrongsong still work.'/>
    <modal-keyboard id='filterKeyboard' clear-on-open='true'/>
    <modal id='removeModal' click-off-closes='true' move-to-center='true' size-delta-x='60' size-delta-y='42'>
      <vertical pad='3' spacing='1' child-control-height='false' child-align='MiddleCenter'>
        <text id='removeText' text='Remove?' font-size='3.4' align='Center' word-wrapping='true' rich-text='false' pref-width='54' size-delta-y='10'/>
        <button id='removeSelectedButton' text='Remove' on-click='removeSelected' pref-width='40' pref-height='7'/>
        <button text='Remove All' on-click='removeAll' pref-width='40' pref-height='7'/>
        <button text='Cancel' on-click='cancelRemove' pref-width='40' pref-height='7'/>
      </vertical>
    </modal>
  </tab>

  <tab tags='tabs' tab-name='General'>
    <modifier-container child-expand-width='true' horizontal-fit='PreferredSize' pad-top='2'>
      <toggle-setting text='Show Chat Window' value='showWindow' apply-on-change='true' hover-hint='Master switch for the floating chat panel'/>
      <toggle-setting text='Highlight Messages' value='highlightMessages' apply-on-change='true' hover-hint='Dark backdrop behind each message for readability'/>
      <toggle-setting text='Platform Icons' value='platformIcons' apply-on-change='true' hover-hint='Platform and user badge images in front of messages. Off = just name and message.'/>
      <toggle-setting text='Emoji Images' value='emojiImages' apply-on-change='true' hover-hint='Show stream emotes as inline images. Off = plain text names.'/>
      <toggle-setting text='Animated Emotes' value='animatedEmotes' apply-on-change='true' hover-hint='Optional GIF playback. Default off to conserve memory. Limited to 4 animations, 2 MiB of frame data, and 10 updates per second.'/>
      <toggle-setting text='Show Viewer Counts' value='showViewerCounts' apply-on-change='true' hover-hint='Live viewer numbers per platform at the top of the panel'/>
      <toggle-setting text='Show System Messages' value='showSystem' apply-on-change='true' hover-hint='Connection status lines in the chat feed'/>
      <button text='Recenter Window' on-click='recenterWindow' pref-width='46' pref-height='8' hover-hint='Move the chat panel back to its default position.'/>
      <text text='v)BSML" VERSION R"BSML(' font-size='3' align='Center' font-color='#999999' word-wrapping='false' size-delta-y='4'/>
    </modifier-container>
  </tab>
</vertical>
)BSML";

    // The Panel page, from the Appearance tab.
    inline constexpr std::string_view PanelBsml = R"BSML(
<horizontal horizontal-fit='PreferredSize' spacing='4' pad-top='2' child-align='UpperCenter'>
  <vertical child-control-height='false' spacing='0'>
    <slider-setting text='Curve' pref-width='64' value='curveAmount' min='0' max='10' increment='0.5' apply-on-change='true' hover-hint='Window curve. 0 = flat, higher = stronger curve.'/>
    <slider-setting text='Corners' pref-width='64' value='cornerRadius' min='0' max='10' increment='0.5' apply-on-change='true' hover-hint='Window corner rounding. 0 = square corners.'/>
    <slider-setting text='Width' pref-width='64' value='panelWidth' min='70' max='300' increment='5' apply-on-change='true' hover-hint='Chat window width.'/>
    <slider-setting text='Height' pref-width='64' value='panelHeight' min='30' max='200' increment='5' apply-on-change='true' hover-hint='Chat window height.'/>
    <color-setting text='Color' pref-width='64' value='backgroundColor' apply-on-change='true' hover-hint='Window backdrop color.'/>
  </vertical>
  <vertical child-control-height='false' spacing='0'>
    <slider-setting text='Font' pref-width='64' value='fontSize' min='2' max='6' increment='0.2' apply-on-change='true' hover-hint='Chat text size.'/>
    <slider-setting text='Counter' pref-width='64' value='viewerCountSize' min='1.5' max='6' increment='0.1' apply-on-change='true' hover-hint='Size of the viewer-count numbers and platform icons at the top of the chat panel.'/>
    <slider-setting text='Opacity' pref-width='64' value='backgroundOpacity' min='0' max='1' increment='0.05' apply-on-change='true' hover-hint='Window backdrop transparency. 0 = invisible backdrop, text stays.'/>
    <slider-setting text='Highlight' pref-width='64' value='highlightOpacity' min='0.1' max='1' increment='0.05' apply-on-change='true' hover-hint='Darkness of the backdrop behind each message (with Highlight Messages on).'/>
    <button text='Reset Default Appearance' on-click='resetAppearance' pref-width='64' pref-height='8' hover-hint='Put every setting on this page back to its default. The panel position is kept.'/>
  </vertical>
</horizontal>
)BSML";

    // The Positions page, from the Appearance tab. A column as wide as the
    // others: a long wrapped text would make the page as wide as its one line.
    inline constexpr std::string_view PositionsBsml = R"BSML(
<horizontal horizontal-fit='PreferredSize' pad-top='2' child-align='UpperCenter'>
  <vertical child-control-height='false' spacing='1'>
    <text text='Menus' font-size='3.2' font-color='#8FD3FF' bold='true' all-uppercase='true' align='BottomLeft' word-wrapping='false' pref-width='64' size-delta-y='5'/>
    <button text='Capture Menu Position' on-click='captureMenuPosition' pref-width='64' pref-height='8' hover-hint='Save where the panel is now as its place in the menus.'/>
    <text text='Songs' font-size='3.2' font-color='#8FD3FF' bold='true' all-uppercase='true' align='BottomLeft' word-wrapping='false' pref-width='64' size-delta-y='5'/>
    <toggle-setting text='Separate Playing Position' pref-width='64' value='separatePlayingPosition' apply-on-change='true' hover-hint='During songs, move the panel to the captured playing position. Off = it stays at the menu position. Dragging it in the pause menu saves the playing position.'/>
    <button text='Capture Playing Position' on-click='capturePlayingPosition' pref-width='64' pref-height='8' hover-hint='Save where the panel is now as its place during songs, and turn Separate Playing Position on.'/>
    <text text='Drag the panel, then capture it.' font-size='3' font-color='#AAAAAA' align='Center' word-wrapping='false' pref-width='64' size-delta-y='4'/>
    <text text='Dragging alone saves nothing here.' font-size='3' font-color='#AAAAAA' align='Center' word-wrapping='false' pref-width='64' size-delta-y='4'/>
    <text text='Leaving puts it back at its menu spot.' font-size='3' font-color='#AAAAAA' align='Center' word-wrapping='false' pref-width='64' size-delta-y='4'/>
    <text text='Unlock it (padlock) to drag it.' font-size='3' font-color='#AAAAAA' align='Center' word-wrapping='false' pref-width='64' size-delta-y='4'/>
  </vertical>
</horizontal>
)BSML";
}
