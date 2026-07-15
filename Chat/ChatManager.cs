using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using SimpleBSStreamChat.Chat.Kick;
using SimpleBSStreamChat.Chat.Twitch;
using SimpleBSStreamChat.Chat.YouTube;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat
{
    /// <summary>
    /// Aggregates all chat services into one thread-safe message buffer.
    /// The UI polls <see cref="Version"/> from the main thread and copies a
    /// snapshot when it changes, so no Unity API is ever touched from here.
    /// </summary>
    public class ChatManager
    {
        private readonly object _lock = new object();
        private readonly List<ChatMessage> _messages = new List<ChatMessage>();
        private readonly List<IChatService> _services = new List<IChatService>();
        private readonly PluginConfig _config;
        private readonly IPALogger _log;
        private int _version;
        private volatile YouTubeLiveChatClient? _youTubeClient;
        // Persisted across YouTube client recreations (platform toggles, channel
        // changes, reconnects) so message-id dedup is never reset within a
        // session — otherwise re-enabling YouTube replays the recent backlog and
        // re-runs !request commands. Bounded inside InnerTubeChatSession.
        private readonly HashSet<string> _youTubeSeenIds = new HashSet<string>();
        private readonly Queue<string> _youTubeSeenIdOrder = new Queue<string>();

        public ViewerCountTracker? ViewerTracker { get; private set; }

        /// <summary>Raised for every message after it is buffered (from the
        /// producing background thread). The song-request handler subscribes
        /// here so command handling stays decoupled from display.</summary>
        public event Action<ChatMessage>? MessageAdded;

        public ChatManager(PluginConfig config, IPALogger log)
        {
            _config = config;
            _log = log;
        }

        public int Version
        {
            get { lock (_lock) return _version; }
        }

        public void Start()
        {
            StartPlatform(ChatPlatform.Twitch);
            StartPlatform(ChatPlatform.YouTube);
            StartPlatform(ChatPlatform.Kick);

            // Tracker survives service restarts so counts don't reset on toggles.
            if (ViewerTracker == null)
            {
                ViewerTracker = new ViewerCountTracker(_config, () => _youTubeClient?.CurrentSession, _log);
                ViewerTracker.Start();
            }
        }

        /// <summary>Reconnects with current config. Clears the feed only when asked
        /// (channel changed) — platform toggles keep already-received messages.</summary>
        public void Restart(bool clearMessages)
        {
            Stop(disposeTracker: false, waitForServices: false);
            if (clearMessages)
                ClearMessages();
            Start();
        }

        /// <summary>Reconnects only one platform after its settings changed.
        /// This keeps unrelated platforms, especially Kick's slower chat
        /// resolver, connected while Twitch/YouTube/Kick toggles are changed.</summary>
        public void RestartPlatform(ChatPlatform platform, bool clearMessages)
        {
            StopPlatform(platform, waitForServices: false);
            if (clearMessages)
                ClearMessages();
            StartPlatform(platform);

            if (ViewerTracker == null)
            {
                ViewerTracker = new ViewerCountTracker(_config, () => _youTubeClient?.CurrentSession, _log);
                ViewerTracker.Start();
            }
        }

        public void Stop() => Stop(disposeTracker: true);

        public void Stop(bool disposeTracker) => Stop(disposeTracker, waitForServices: true);

        private void Stop(bool disposeTracker, bool waitForServices)
        {
            if (disposeTracker)
            {
                try { ViewerTracker?.Dispose(); }
                catch (Exception ex) { _log.Warn($"Error stopping viewer tracker: {ex.Message}"); }
                ViewerTracker = null;
            }

            foreach (var service in _services)
            {
                service.MessageReceived -= AddMessage;
                service.StatusChanged -= OnStatusChanged;
                try { StopService(service, waitForServices); }
                catch (Exception ex) { _log.Warn($"Error stopping chat service: {ex.Message}"); }
            }
            _services.Clear();
            _youTubeClient = null;
        }

        private void StopPlatform(ChatPlatform platform, bool waitForServices)
        {
            for (var i = _services.Count - 1; i >= 0; i--)
            {
                var service = _services[i];
                if (GetPlatform(service) != platform)
                    continue;

                service.MessageReceived -= AddMessage;
                service.StatusChanged -= OnStatusChanged;
                _services.RemoveAt(i);
                try { StopService(service, waitForServices); }
                catch (Exception ex) { _log.Warn($"Error stopping {platform} chat service: {ex.Message}"); }
            }

            if (platform == ChatPlatform.YouTube)
                _youTubeClient = null;
        }

        private void StartPlatform(ChatPlatform platform)
        {
            if (IsPlatformRunning(platform))
                return;

            IChatService? service = null;
            switch (platform)
            {
                case ChatPlatform.Twitch:
                    if (!_config.EnableTwitch)
                        return;
                    if (string.IsNullOrWhiteSpace(_config.TwitchChannel))
                    {
                        AddSystem("Set \"TwitchChannel\" in UserData\\SimpleBSStreamChat.json to see your Twitch chat.");
                        return;
                    }
                    service = new TwitchIrcClient(_config.TwitchChannel, _log);
                    break;
                case ChatPlatform.YouTube:
                    if (!_config.EnableYouTube)
                        return;
                    if (string.IsNullOrWhiteSpace(_config.YouTubeChannel))
                    {
                        AddSystem("Set \"YouTubeChannel\" in UserData\\SimpleBSStreamChat.json to see your YouTube chat.");
                        return;
                    }
                    _youTubeClient = new YouTubeLiveChatClient(_config.YouTubeChannel, _log,
                        _youTubeSeenIds, _youTubeSeenIdOrder);
                    service = _youTubeClient;
                    break;
                case ChatPlatform.Kick:
                    if (!_config.EnableKick)
                        return;
                    if (string.IsNullOrWhiteSpace(_config.KickChannel))
                    {
                        AddSystem("Set \"KickChannel\" in UserData\\SimpleBSStreamChat.json to see your Kick chat.");
                        return;
                    }
                    service = new KickLiveChatClient(_config.KickChannel, _log);
                    break;
                default:
                    return;
            }

            Register(service);
            service.Start();
        }

        private bool IsPlatformRunning(ChatPlatform platform)
        {
            foreach (var service in _services)
            {
                if (GetPlatform(service) == platform)
                    return true;
            }
            return false;
        }

        private static ChatPlatform? GetPlatform(IChatService service)
        {
            if (service is TwitchIrcClient)
                return ChatPlatform.Twitch;
            if (service is YouTubeLiveChatClient)
                return ChatPlatform.YouTube;
            if (service is KickLiveChatClient)
                return ChatPlatform.Kick;
            return null;
        }

        private void StopService(IChatService service, bool waitForExit)
        {
            if (service is TwitchIrcClient twitch)
            {
                if (waitForExit)
                    twitch.Dispose();
                else
                    twitch.Stop(waitForExit: false);
                return;
            }

            if (service is YouTubeLiveChatClient youtube)
            {
                youtube.Dispose(waitForExit);
                return;
            }

            if (service is KickLiveChatClient kick)
            {
                kick.Dispose(waitForExit);
                return;
            }

            if (waitForExit)
                service.Dispose();
            else
                Task.Run(() =>
                {
                    try { service.Dispose(); }
                    catch (Exception ex) { _log.Warn($"Error stopping chat service: {ex.Message}"); }
                });
        }

        // This is a retention bound, not a UI setting. The panel derives its
        // visible row count from its actual geometry, while the chat service has
        // no knowledge of panel size or text wrapping.
        private const int MessageBufferCapacity = 64;

        public void AddMessage(ChatMessage message)
        {
            lock (_lock)
            {
                _messages.Add(message);
                while (_messages.Count > MessageBufferCapacity)
                    _messages.RemoveAt(0);
                _version++;
            }

            // Fire outside the lock so handlers never run under the buffer lock.
            try { MessageAdded?.Invoke(message); }
            catch (Exception ex) { _log.Warn($"MessageAdded handler failed: {ex.Message}"); }
        }

        /// <summary>Adds a system message to the feed (e.g. song-request feedback).</summary>
        public void AddSystemMessage(string text) => AddSystem(text);

        /// <summary>Copies the current messages into <paramref name="target"/> (cleared first).</summary>
        public void CopyMessages(List<ChatMessage> target)
        {
            lock (_lock)
            {
                target.Clear();
                target.AddRange(_messages);
            }
        }

        private void ClearMessages()
        {
            lock (_lock)
            {
                _messages.Clear();
                _version++;
            }
        }

        private void Register(IChatService service)
        {
            service.MessageReceived += AddMessage;
            service.StatusChanged += OnStatusChanged;
            _services.Add(service);
        }

        private void OnStatusChanged(string status)
        {
            _log.Info(status);
            if (_config.ShowSystemMessages)
                AddSystem(status);
        }

        private void AddSystem(string text) => AddMessage(ChatMessage.System(text));
    }
}
