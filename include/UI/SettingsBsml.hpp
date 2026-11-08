#pragma once

#include <string_view>

namespace StandaloneStreamChat::UI {
    // The settings screen: tabs, labels, ranges and hints.
    // Values bind to SettingsHost properties (C++ names, so camelCase ids).
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
      </vertical>
    </horizontal>
  </tab>

  <tab tags='tabs' tab-name='Requests'>
    <horizontal horizontal-fit='PreferredSize' pad-top='2'>
      <vertical child-control-height='false' spacing='0'>
        <toggle-setting text='Enable Requests' value='enableSongRequests' apply-on-change='true' hover-hint='Allow chat to add BeatSaver map requests with !bsr followed by the map key.'/>
        <toggle-setting text='Allow Duplicates' value='allowDuplicates' apply-on-change='true' hover-hint='Allow the same map key to appear more than once in the queue.'/>
        <toggle-setting text='Persist Queue' value='persistRequestQueue' apply-on-change='true' hover-hint='Restore queued requests after restarting the game.'/>
        <slider-setting text='Cooldown' pref-width='64' value='requestCooldown' min='0' max='600' increment='5' integer-only='true' apply-on-change='true' hover-hint='Per-viewer cooldown in seconds. Mods and broadcaster are exempt.'/>
      </vertical>
    </horizontal>
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
      <text text='v)BSML" VERSION R"BSML(' font-size='3' align='Center' font-color='#999999'/>
    </modifier-container>
  </tab>
</vertical>
)BSML";
}
