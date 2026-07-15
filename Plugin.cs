using IPA;
using IPA.Config.Stores;
using SimpleBSStreamChat.Chat;
using SimpleBSStreamChat.Chat.Requests;
using SimpleBSStreamChat.Songs;
using SimpleBSStreamChat.UI;
using UnityEngine;
using Conf = IPA.Config.Config;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat
{
    [Plugin(RuntimeOptions.SingleStartInit)]
    public class Plugin
    {
        internal static IPALogger Log { get; private set; } = null!;

        private ChatManager? _chatManager;
        private SongRequestManager? _requestManager;
        private BeatSaverService? _beatSaver;
        private RequestQueueStorage? _requestStorage;
        private SongRequestPipeline? _requestPipeline;
        private SongInstaller? _songInstaller;
        private LevelNavigator? _levelNavigator;
        private GameObject? _rootGo;

        [Init]
        public Plugin(IPALogger logger, Conf conf)
        {
            Log = logger;
            PluginConfig.Instance = conf.Generated<PluginConfig>();
            Log.Info("SimpleBSStreamChat initialized.");
        }

        [OnStart]
        public void OnStart()
        {
            _rootGo = new GameObject("SimpleBSStreamChat");
            UnityEngine.Object.DontDestroyOnLoad(_rootGo);
            var dispatcher = _rootGo.AddComponent<MainThreadDispatcher>();

            _chatManager = new ChatManager(PluginConfig.Instance, Log);

            _beatSaver = new BeatSaverService(Log);
            _requestStorage = new RequestQueueStorage(Log);
            _requestManager = new SongRequestManager(PluginConfig.Instance, Log, _beatSaver, _requestStorage);
            _songInstaller = new SongInstaller(dispatcher, Log);
            _levelNavigator = new LevelNavigator(dispatcher, Log);
            _requestPipeline = new SongRequestPipeline(_requestManager, _beatSaver, _songInstaller, _levelNavigator, dispatcher, Log);
            _requestManager.SetPipeline(_requestPipeline);
            _requestManager.Notice += _chatManager.AddSystemMessage;
            _chatManager.MessageAdded += _requestManager.OnChatMessage;
            _requestManager.RestorePersistedQueue();

            var overlay = _rootGo.AddComponent<MapOverlayController>();
            overlay.Initialize(PluginConfig.Instance, Log, _beatSaver, dispatcher, _requestManager);

            var panel = _rootGo.AddComponent<ChatPanelController>();
            panel.Initialize(_chatManager, PluginConfig.Instance, Log, _requestManager, overlay);

            _chatManager.Start();
            Log.Info("SimpleBSStreamChat started.");
        }

        [OnExit]
        public void OnExit()
        {
            if (_chatManager != null && _requestManager != null)
            {
                _chatManager.MessageAdded -= _requestManager.OnChatMessage;
                _requestManager.Notice -= _chatManager.AddSystemMessage;
            }
            _requestManager?.SaveQueue();
            _requestManager?.Shutdown();
            _chatManager?.Stop();
            _requestPipeline?.Dispose();
            _beatSaver?.Dispose();
            if (_rootGo != null)
                UnityEngine.Object.Destroy(_rootGo);
            Log.Info("SimpleBSStreamChat stopped.");
        }
    }
}
