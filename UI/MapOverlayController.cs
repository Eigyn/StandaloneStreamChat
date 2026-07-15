using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using BeatSaberMarkupLanguage;
using SimpleBSStreamChat.Chat.Requests;
using SimpleBSStreamChat.Songs;
using TMPro;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.UI
{
    /// <summary>
    /// A card drawn on a Screen-Space Overlay canvas, which in VR composites onto
    /// the desktop/flatscreen mirror only — never the headset. It has two modes:
    /// while a song plays it is a "now playing" card (name, author, cover, mapper +
    /// BeatSaver key, and a live elapsed|total timer); in the menu it becomes a
    /// request-queue panel ("!request is available" + the first few queued songs).
    /// Fields and screen position are configurable; the card is toggled by a
    /// button on the chat panel.
    /// </summary>
    public sealed class MapOverlayController : MonoBehaviour
    {
        private const int QueuePreviewCount = 3;
        private const float Margin = 40f;          // gap from the screen edge (1080p px)
        private const float CoverSize = 96f;
        // The cover slot is 96 CSS px; keep the backing texture at 2× for
        // crispness on high-DPI flatscreen mirrors while staying far smaller than
        // a full-resolution (256–1024 px) source cover.
        private const int CoverTextureSize = 192;
        private const float TextColumnWidth = 380f;

        private PluginConfig _config = null!;
        private IPALogger? _log;
        private BeatSaverService? _beatSaver;
        private MainThreadDispatcher? _dispatcher;
        private SongRequestManager? _requestManager;
        private readonly HttpClient _http = new HttpClient();

        private GameObject? _canvasGo;
        private RectTransform? _card;
        private GameObject? _songGroupGo;
        private GameObject? _coverGo;
        private RawImage? _cover;
        private TextMeshProUGUI? _titleText;
        private TextMeshProUGUI? _authorText;
        private TextMeshProUGUI? _mapperText;
        private GameObject? _durationRowGo;
        private TextMeshProUGUI? _durationText;
        private TextMeshProUGUI? _difficultyText;
        private GameObject? _requestsGroupGo;
        private TextMeshProUGUI? _requestTitleText;
        private TextMeshProUGUI[] _requestLines = Array.Empty<TextMeshProUGUI>();
        private readonly List<SongRequest> _queueSnapshot = new List<SongRequest>();

        private string? _currentLevelId;
        private string[] _currentMappers = Array.Empty<string>();
        private string? _resolvedKey;
        private string _currentDifficultyText = "";
        private string _totalDurationText = "";
        private AudioTimeSyncController? _audioSync;
        private StandardLevelScenesTransitionSetupDataSO? _playingTransition;
        private string? _shownDuration;
        private string? _shownDifficulty;
        private Texture2D? _coverTexture;
        private Material? _flatFontMaterial;
        private CancellationTokenSource? _keyCts;
        private LevelCollectionNavigationController? _levelCollectionController;
        private string? _levelCollectionScene;
        private float _nextPoll;
        private float _nextBuildAttempt;
        private readonly StringBuilder _builder = new StringBuilder(128);

        internal void Initialize(PluginConfig config, IPALogger log, BeatSaverService? beatSaver,
            MainThreadDispatcher dispatcher, SongRequestManager? requestManager)
        {
            _config = config;
            _log = log;
            _beatSaver = beatSaver;
            _dispatcher = dispatcher;
            _requestManager = requestManager;
        }

        private void Update()
        {
            if (_config == null)
                return;

            // Build lazily once the menu is up (fonts/BSML ready), with retries.
            if (_canvasGo == null)
            {
                if (Time.unscaledTime < _nextBuildAttempt)
                    return;
                if (SceneManager.GetActiveScene().name != "MainMenu")
                    return;
                _nextBuildAttempt = Time.unscaledTime + 2f;
                try
                {
                    BuildCanvas();
                }
                catch (Exception ex)
                {
                    _log?.Debug($"Map overlay not buildable yet ({ex.GetType().Name}), retrying in 2s");
                    return;
                }
                ApplySettings();
            }

            if (Time.unscaledTime >= _nextPoll)
            {
                _nextPoll = Time.unscaledTime + 0.2f;
                if (_config.OverlayEnabled)
                    UpdateMode();
            }
        }

        // Menu → request-queue panel (when requests are on); in a song → the
        // now-playing card. The selection is polled even while the request panel
        // shows, so the song card is ready the moment a song starts.
        private void UpdateMode()
        {
            var inMenu = SceneManager.GetActiveScene().name == "MainMenu";
            if (inMenu)
            {
                PollSelectedLevel();
                // Drop the cached gameplay transition so the next song re-resolves
                // it (SO instances linger after the scene unloads).
                _playingTransition = null;
            }

            var requestsMode = inMenu && _config.EnableSongRequests && _requestManager != null;
            var songMode = !inMenu && GetAudioSync() != null;
            if (songMode)
                PollPlayingLevel();
            var shouldShow = _config.OverlayEnabled && (requestsMode || songMode);
            if (_canvasGo != null && _canvasGo.activeSelf != shouldShow)
                _canvasGo.SetActive(shouldShow);
            if (!shouldShow)
                return;

            if (_requestsGroupGo != null && _requestsGroupGo.activeSelf != requestsMode)
                _requestsGroupGo.SetActive(requestsMode);
            if (_songGroupGo != null && _songGroupGo.activeSelf == requestsMode)
                _songGroupGo.SetActive(!requestsMode);

            if (requestsMode)
                UpdateRequestsPanel();
            else
                UpdateDurationLine();
        }

        private void UpdateRequestsPanel()
        {
            if (_requestManager == null || _requestLines.Length == 0)
                return;
            if (_requestTitleText != null)
                _requestTitleText.text = RequestCommandLabel() + " is available";

            _requestManager.CopyRequests(_queueSnapshot);
            for (var i = 0; i < _requestLines.Length; i++)
            {
                var line = _requestLines[i];
                if (line == null)
                    continue;
                string text;
                if (i < _queueSnapshot.Count)
                {
                    var request = _queueSnapshot[i];
                    var name = string.IsNullOrEmpty(request.SongName) ? request.Key : request.SongName;
                    text = $"{i + 1}. {name}   <alpha=#88>{request.RequesterName}";
                }
                else if (i == 0 && _queueSnapshot.Count == 0)
                {
                    text = "<alpha=#99>No requests yet";
                }
                else
                {
                    if (line.gameObject.activeSelf)
                        line.gameObject.SetActive(false);
                    continue;
                }
                if (!line.gameObject.activeSelf)
                    line.gameObject.SetActive(true);
                line.text = text;
            }
        }

        // First alias of the (comma-separated) request command; "!request" default.
        private string RequestCommandLabel()
        {
            var command = _config.RequestCommand;
            if (string.IsNullOrWhiteSpace(command))
                return "!request";
            var first = command.Split(',')[0].Trim();
            return first.Length == 0 ? "!request" : first;
        }

        // ---- Selection tracking -------------------------------------------------

        private void PollSelectedLevel()
        {
            if (SceneManager.GetActiveScene().name != "MainMenu")
                return;
            BeatmapLevel? level = null;
            var difficultyText = "";
            try
            {
                var nav = GetLevelCollectionController();
                if (nav != null)
                {
                    level = nav.beatmapLevel;
                    difficultyText = DifficultyLabel(nav.beatmapKey.difficulty);
                }
            }
            catch (Exception ex)
            {
                _log?.Debug($"Map overlay selection poll failed: {ex.Message}");
                return;
            }

            if (level == null || string.IsNullOrEmpty(level.levelID))
                return;
            if (level.levelID == _currentLevelId)
            {
                if (_currentDifficultyText != difficultyText)
                {
                    _currentDifficultyText = difficultyText;
                    _shownDifficulty = null;
                    UpdateDurationLine();
                }
                return;
            }
            OnLevelChanged(level, difficultyText);
        }

        // In a song, the now-playing map comes from the actual gameplay transition
        // data, not the last menu selection. This is what makes the card populate
        // for BeatLeader/ScoreSaber replays (and any play started without walking
        // the level list), where the menu selection is stale or was never set —
        // only the clock, read live from the audio sync, worked before.
        private void PollPlayingLevel()
        {
            BeatmapLevel? level = null;
            var difficultyText = "";
            try
            {
                // Cache the transition (it doesn't change mid-song), so the
                // relatively costly Resources scan doesn't run every poll during
                // gameplay. Cleared on return to the menu (UpdateMode).
                if (_playingTransition == null || _playingTransition.beatmapLevel == null)
                    _playingTransition = FindPlayingTransition();
                if (_playingTransition != null)
                {
                    level = _playingTransition.beatmapLevel;
                    difficultyText = DifficultyLabel(_playingTransition.beatmapKey.difficulty);
                }
            }
            catch (Exception ex)
            {
                _log?.Debug($"Map overlay playing-level poll failed: {ex.Message}");
                return;
            }

            if (level == null || string.IsNullOrEmpty(level.levelID))
                return; // couldn't resolve; keep whatever the card already shows
            if (level.levelID == _currentLevelId)
            {
                if (_currentDifficultyText != difficultyText)
                {
                    _currentDifficultyText = difficultyText;
                    _shownDifficulty = null;
                    UpdateDurationLine();
                }
                return;
            }
            OnLevelChanged(level, difficultyText);
        }

        // The standard-level transition data is a ScriptableObject, so it survives
        // the scene load and is findable without Zenject/Harmony (which this mod
        // avoids); replays go through it too. Older instances linger, so take the
        // most recent one that actually carries a level.
        private static StandardLevelScenesTransitionSetupDataSO? FindPlayingTransition()
        {
            StandardLevelScenesTransitionSetupDataSO? best = null;
            foreach (var candidate in Resources.FindObjectsOfTypeAll<StandardLevelScenesTransitionSetupDataSO>())
            {
                if (candidate != null && candidate.beatmapLevel != null)
                    best = candidate;
            }
            return best;
        }

        private LevelCollectionNavigationController? GetLevelCollectionController()
        {
            var scene = SceneManager.GetActiveScene().name;
            if (_levelCollectionScene != scene)
            {
                _levelCollectionScene = scene;
                _levelCollectionController = null;
            }
            if (_levelCollectionController == null)
                _levelCollectionController = BeatSaberUI.DiContainer?.TryResolve<LevelCollectionNavigationController>();
            return _levelCollectionController;
        }

        private void OnLevelChanged(BeatmapLevel level, string difficultyText)
        {
            _currentLevelId = level.levelID;
            _currentMappers = level.allMappers ?? Array.Empty<string>();
            _resolvedKey = null;
            _currentDifficultyText = difficultyText;

            RefreshTexts(level);
            LoadCover(level);
            ResolveKey(level);
        }

        private void RefreshTexts(BeatmapLevel level)
        {
            if (_titleText != null)
                _titleText.text = level.songName ?? "";
            if (_authorText != null)
                _authorText.text = level.songAuthorName ?? "";
            _totalDurationText = level.songDuration > 0f ? Clock(level.songDuration) : "";
            _shownDuration = null; // force the duration line to refresh
            _shownDifficulty = null;
            UpdateDurationLine();
            UpdateMapperText();
        }

        // Duration (Clock) and Difficulty share one row but are independent
        // fields: each refreshes only when its own toggle is on, so turning Clock
        // off no longer suppresses Difficulty (and vice versa).
        private void UpdateDurationLine()
        {
            if (_durationText != null && _config.OverlayShowDuration)
            {
                // In a song, mirror the in-game progress timer (elapsed | total);
                // in the menu, show the selected map's total length.
                var ats = GetAudioSync();
                string text;
                if (ats != null && ats.isAudioLoaded && ats.songLength > 0f)
                    text = $"{Clock(ats.songTime)} | {Clock(ats.songLength)}";
                else
                    text = _totalDurationText;
                if (_shownDuration != text)
                {
                    _shownDuration = text;
                    _durationText.text = text;
                }
            }

            if (_difficultyText != null && _config.OverlayShowDifficulty)
            {
                var difficulty = CurrentDifficultyText();
                if (_shownDifficulty != difficulty)
                {
                    _shownDifficulty = difficulty;
                    _difficultyText.text = difficulty;
                    _difficultyText.gameObject.SetActive(!string.IsNullOrEmpty(difficulty));
                }
            }
        }

        // The audio clock only exists inside a song; drop it back in the menu so
        // the line reverts to the total length.
        private AudioTimeSyncController? GetAudioSync()
        {
            if (SceneManager.GetActiveScene().name == "MainMenu")
            {
                _audioSync = null;
                return null;
            }
            if (_audioSync == null)
                _audioSync = FindObjectOfType<AudioTimeSyncController>();
            return _audioSync;
        }

        // Kept current by PollSelectedLevel (menu) and PollPlayingLevel (song),
        // so it's correct in both — including replays.
        private string CurrentDifficultyText() => _currentDifficultyText;

        private void UpdateMapperText()
        {
            if (_mapperText == null)
                return;
            _builder.Length = 0;
            for (var i = 0; i < _currentMappers.Length; i++)
            {
                var mapper = _currentMappers[i];
                if (string.IsNullOrWhiteSpace(mapper))
                    continue;
                if (_builder.Length > 0)
                    _builder.Append(", ");
                _builder.Append(mapper.Trim());
            }
            if (!string.IsNullOrEmpty(_resolvedKey))
            {
                if (_builder.Length > 0)
                    _builder.Append("   ");
                _builder.Append("<alpha=#AA>").Append(_resolvedKey);
            }
            _mapperText.text = _builder.ToString();
        }

        private void LoadCover(BeatmapLevel level)
        {
            if (_cover == null || !_config.OverlayShowCover)
                return;
            var media = level.previewMediaData;
            if (media == null)
            {
                SetCover(null);
                return;
            }

            var idGuard = level.levelID;
            try
            {
                var task = media.GetCoverSpriteAsync();
                task.ContinueWith(t =>
                {
                    _dispatcher?.Enqueue(() =>
                    {
                        if (_currentLevelId != idGuard)
                            return; // selection moved on before the cover arrived
                        if (t.Status == TaskStatus.RanToCompletion && t.Result != null && t.Result.texture != null)
                            SetCover(CopySpriteTexture(t.Result));
                        else
                            SetCover(null);
                    });
                });
            }
            catch (Exception ex)
            {
                _log?.Debug($"Map overlay cover load failed: {ex.Message}");
            }
        }

        // Own a copy of the cover so it survives the menu scene unloading when a
        // song starts (the level's own sprite gets released at that point).
        private Texture2D? CopySpriteTexture(Sprite sprite)
        {
            var src = sprite.texture;
            if (src == null)
                return null;

            Rect rect;
            try
            {
                rect = sprite.textureRect;
            }
            catch
            {
                rect = new Rect(0, 0, src.width, src.height);
            }
            if (rect.width < 1f || rect.height < 1f)
                rect = new Rect(0, 0, src.width, src.height);

            return DownsampleToCover(src, rect);
        }

        // Render the requested sub-rect of any source texture down to a small
        // square cover at display resolution. Uses a GPU blit rather than
        // Graphics.CopyTexture: covers are frequently GPU-compressed (BC/DXT) or a
        // different size/format than the destination, which makes CopyTexture emit
        // "format/size mismatch" errors (41 of them in one browse session) and
        // keep a wasteful full-resolution copy. Blitting handles any source format
        // and downsamples in one step, so there are zero copy errors and the kept
        // texture is display-sized.
        private Texture2D? DownsampleToCover(Texture src, Rect srcRectPixels)
        {
            if (src.width <= 0 || src.height <= 0)
                return null;

            // Blit only the sprite's sub-rect: map the destination's full 0..1 uv
            // onto [rect.x, rect.x+rect.width] / source size.
            var scale = new Vector2(srcRectPixels.width / src.width, srcRectPixels.height / src.height);
            var offset = new Vector2(srcRectPixels.x / src.width, srcRectPixels.y / src.height);

            RenderTexture? rt = null;
            var prev = RenderTexture.active;
            try
            {
                rt = RenderTexture.GetTemporary(CoverTextureSize, CoverTextureSize, 0,
                    RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
                Graphics.Blit(src, rt, scale, offset);
                RenderTexture.active = rt;
                var tex = new Texture2D(CoverTextureSize, CoverTextureSize, TextureFormat.RGBA32, false)
                {
                    wrapMode = TextureWrapMode.Clamp,
                    filterMode = FilterMode.Bilinear,
                };
                tex.ReadPixels(new Rect(0, 0, CoverTextureSize, CoverTextureSize), 0, 0);
                tex.Apply(false, false);
                return tex;
            }
            catch (Exception ex)
            {
                _log?.Debug($"Map overlay cover downsample failed: {ex.Message}");
                return null;
            }
            finally
            {
                RenderTexture.active = prev;
                if (rt != null)
                    RenderTexture.ReleaseTemporary(rt);
            }
        }

        private void SetCover(Texture2D? texture)
        {
            if (_coverTexture != null && _coverTexture != texture)
                Destroy(_coverTexture);
            _coverTexture = texture;
            if (_cover != null)
            {
                _cover.texture = texture;
                _cover.enabled = texture != null;
            }
            // The cover slot starts collapsed (texture loads a frame later); show
            // it once art actually arrives, so the layout reflows around it.
            if (_coverGo != null)
                _coverGo.SetActive(_config.OverlayShowCover && texture != null);
        }

        private void ResolveKey(BeatmapLevel level)
        {
            CancelKeyResolve();
            if (_beatSaver == null || (!_config.OverlayShowMapper && !_config.OverlayShowCover))
                return;
            var hash = ExtractHash(level.levelID);
            if (hash == null)
                return; // OST / non-custom map: mapper only, no key

            _keyCts = new CancellationTokenSource();
            var token = _keyCts.Token;
            var idGuard = level.levelID;
            _ = ResolveKeyAsync(hash, token, idGuard);
        }

        private async Task ResolveKeyAsync(string hash, CancellationToken token, string idGuard)
        {
            var info = await _beatSaver!.FetchByHashAsync(hash, token).ConfigureAwait(false);
            if (info == null)
                return;

            var key = info.Value.Key;
            if (!string.IsNullOrEmpty(key))
            {
                _dispatcher?.Enqueue(() =>
                {
                    if (_currentLevelId != idGuard || token.IsCancellationRequested)
                        return;
                    _resolvedKey = key;
                    UpdateMapperText();
                });
            }

            if (_config.OverlayShowCover && !string.IsNullOrEmpty(info.Value.CoverUrl))
                await LoadBeatSaverCoverAsync(info.Value.CoverUrl, token, idGuard).ConfigureAwait(false);
        }

        private async Task LoadBeatSaverCoverAsync(string url, CancellationToken token, string idGuard)
        {
            byte[] bytes;
            try
            {
                using (var response = await _http.GetAsync(url, token).ConfigureAwait(false))
                {
                    if (!response.IsSuccessStatusCode)
                        return;
                    bytes = await response.Content.ReadAsByteArrayAsync().ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (Exception ex)
            {
                _log?.Debug($"Map overlay BeatSaver cover download failed: {ex.Message}");
                return;
            }

            _dispatcher?.Enqueue(() =>
            {
                if (_currentLevelId != idGuard || token.IsCancellationRequested)
                    return;

                var texture = new Texture2D(2, 2, TextureFormat.RGBA32, false)
                {
                    wrapMode = TextureWrapMode.Clamp,
                    filterMode = FilterMode.Bilinear,
                };
                if (!ImageConversion.LoadImage(texture, bytes))
                {
                    Destroy(texture);
                    return;
                }
                // Downsample to the display size and drop the full-resolution
                // decode immediately so the overlay never holds a large cover.
                var cover = DownsampleToCover(texture, new Rect(0, 0, texture.width, texture.height));
                Destroy(texture);
                if (cover == null)
                    return;
                SetCover(cover);
            });
        }

        private static string? ExtractHash(string levelID)
        {
            const string prefix = "custom_level_";
            if (string.IsNullOrEmpty(levelID) || !levelID.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                return null;
            var rest = levelID.Substring(prefix.Length);
            // The content hash is the leading 40 hex chars; WIP/variant ids append more.
            return rest.Length >= 40 ? rest.Substring(0, 40) : rest;
        }

        private static string Clock(float seconds)
        {
            if (float.IsNaN(seconds) || seconds < 0f)
                seconds = 0f;
            var total = Mathf.RoundToInt(seconds);
            return $"{total / 60}:{total % 60:00}";
        }

        private static string DifficultyLabel(BeatmapDifficulty difficulty)
        {
            switch (difficulty)
            {
                case BeatmapDifficulty.Easy: return "Easy";
                case BeatmapDifficulty.Normal: return "Normal";
                case BeatmapDifficulty.Hard: return "Hard";
                case BeatmapDifficulty.Expert: return "Expert";
                case BeatmapDifficulty.ExpertPlus: return "Expert+";
                default: return "";
            }
        }

        // ---- Settings ----------------------------------------------------------

        /// <summary>Toggle the whole card (driven by the chat-panel button).</summary>
        public void SetEnabled(bool enabled)
        {
            if (!enabled)
            {
                if (_canvasGo != null)
                    _canvasGo.SetActive(false);
                return;
            }

            _nextPoll = 0f; // refresh selection immediately on show
            if (_canvasGo != null)
                UpdateMode();
        }

        /// <summary>Re-reads every overlay setting (per-field visibility + position).</summary>
        public void ApplySettings()
        {
            if (_canvasGo == null)
                return;
            _titleText?.gameObject.SetActive(_config.OverlayShowSongName);
            _authorText?.gameObject.SetActive(_config.OverlayShowSongAuthor);
            _mapperText?.gameObject.SetActive(_config.OverlayShowMapper);
            _durationText?.gameObject.SetActive(_config.OverlayShowDuration);
            // Difficulty is independent of the Clock/duration toggle; only its own
            // toggle (and having a value) gates it.
            _difficultyText?.gameObject.SetActive(_config.OverlayShowDifficulty &&
                !string.IsNullOrEmpty(_currentDifficultyText));
            // With the Clock present, Difficulty sits in the right column
            // (right-aligned). With the Clock off, Difficulty takes the Clock's
            // left spot, so left-align it instead of leaving it stranded mid-row.
            if (_difficultyText != null)
                _difficultyText.alignment = _config.OverlayShowDuration
                    ? TextAlignmentOptions.Right
                    : TextAlignmentOptions.Left;
            // The shared row is visible if either field is enabled.
            if (_durationRowGo != null)
                _durationRowGo.SetActive(_config.OverlayShowDuration || _config.OverlayShowDifficulty);
            if (_coverGo != null)
                _coverGo.SetActive(_config.OverlayShowCover && _cover != null && _cover.texture != null);
            ApplyPosition();

            // A field or the cover may have just been re-enabled; make sure its
            // content is populated for the level already selected.
            _currentLevelId = null;
            _nextPoll = 0f;
            UpdateMode();
        }

        private void ApplyPosition()
        {
            if (_card == null)
                return;
            Vector2 anchor;
            Vector2 pos;
            switch (_config.OverlayPosition)
            {
                case 1: anchor = new Vector2(0.5f, 1f); pos = new Vector2(0f, -Margin); break;       // top center
                case 2: anchor = new Vector2(1f, 1f); pos = new Vector2(-Margin, -Margin); break;    // top right
                case 3: anchor = new Vector2(1f, 0.5f); pos = new Vector2(-Margin, 0f); break;       // right
                case 4: anchor = new Vector2(1f, 0f); pos = new Vector2(-Margin, Margin); break;     // bottom right
                case 5: anchor = new Vector2(0.5f, 0f); pos = new Vector2(0f, Margin); break;        // bottom center
                case 6: anchor = new Vector2(0f, 0f); pos = new Vector2(Margin, Margin); break;      // bottom left
                case 7: anchor = new Vector2(0f, 0.5f); pos = new Vector2(Margin, 0f); break;        // left
                default: anchor = new Vector2(0f, 1f); pos = new Vector2(Margin, -Margin); break;    // top left
            }
            _card.anchorMin = anchor;
            _card.anchorMax = anchor;
            _card.pivot = anchor; // pivot at the anchored corner so the card grows inward
            _card.anchoredPosition = pos;
        }

        // ---- Construction ------------------------------------------------------

        private void BuildCanvas()
        {
            _canvasGo = new GameObject("SimpleBSStreamChatOverlay");
            DontDestroyOnLoad(_canvasGo);
            var canvas = _canvasGo.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.sortingOrder = 5000;
            var scaler = _canvasGo.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1920f, 1080f);
            scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
            scaler.matchWidthOrHeight = 0.5f;

            // Card: a padded, content-fitted box holding whichever of the two
            // mutually exclusive groups is active, with a stretched background
            // behind them that is excluded from the layout.
            var cardGo = new GameObject("Card", typeof(RectTransform));
            _card = (RectTransform)cardGo.transform;
            _card.SetParent(_canvasGo.transform, false);
            var cardLayout = cardGo.AddComponent<VerticalLayoutGroup>();
            cardLayout.padding = new RectOffset(18, 18, 14, 14);
            cardLayout.spacing = 0f;
            cardLayout.childAlignment = TextAnchor.UpperLeft;
            cardLayout.childControlWidth = true;
            cardLayout.childControlHeight = true;
            cardLayout.childForceExpandWidth = false;
            cardLayout.childForceExpandHeight = false;
            var cardFitter = cardGo.AddComponent<ContentSizeFitter>();
            cardFitter.horizontalFit = ContentSizeFitter.FitMode.PreferredSize;
            cardFitter.verticalFit = ContentSizeFitter.FitMode.PreferredSize;

            var bgGo = new GameObject("Background", typeof(RectTransform));
            var bgRect = (RectTransform)bgGo.transform;
            bgRect.SetParent(_card, false);
            bgRect.anchorMin = Vector2.zero;
            bgRect.anchorMax = Vector2.one;
            bgRect.offsetMin = Vector2.zero;
            bgRect.offsetMax = Vector2.zero;
            var bgImage = bgGo.AddComponent<Image>();
            bgImage.color = new Color(0f, 0f, 0f, 0.72f);
            bgImage.raycastTarget = false;
            bgGo.AddComponent<LayoutElement>().ignoreLayout = true;

            BuildSongGroup();
            BuildRequestsGroup();

            _log?.Info("Map overlay canvas created.");
        }

        // The now-playing group: [cover][name / author / mapper+key / timer].
        private void BuildSongGroup()
        {
            _songGroupGo = new GameObject("SongGroup", typeof(RectTransform));
            _songGroupGo.transform.SetParent(_card, false);
            var groupLayout = _songGroupGo.AddComponent<HorizontalLayoutGroup>();
            groupLayout.spacing = 14f;
            groupLayout.childAlignment = TextAnchor.MiddleLeft;
            groupLayout.childControlWidth = true;
            groupLayout.childControlHeight = true;
            groupLayout.childForceExpandWidth = false;
            groupLayout.childForceExpandHeight = false;

            _coverGo = new GameObject("Cover", typeof(RectTransform));
            _coverGo.transform.SetParent(_songGroupGo.transform, false);
            _cover = _coverGo.AddComponent<RawImage>();
            _cover.raycastTarget = false;
            _cover.enabled = false;
            var coverLayout = _coverGo.AddComponent<LayoutElement>();
            coverLayout.preferredWidth = CoverSize;
            coverLayout.preferredHeight = CoverSize;
            coverLayout.minWidth = CoverSize;
            coverLayout.minHeight = CoverSize;

            var columnGo = new GameObject("Text", typeof(RectTransform));
            columnGo.transform.SetParent(_songGroupGo.transform, false);
            var columnLayout = columnGo.AddComponent<VerticalLayoutGroup>();
            columnLayout.childAlignment = TextAnchor.MiddleLeft;
            columnLayout.childControlWidth = true;
            columnLayout.childControlHeight = true;
            columnLayout.childForceExpandWidth = true;
            columnLayout.childForceExpandHeight = false;
            columnLayout.spacing = 2f;
            columnGo.AddComponent<LayoutElement>().preferredWidth = TextColumnWidth;

            _titleText = CreateLine((RectTransform)columnGo.transform, 30f, FontStyles.Bold, Color.white);
            _authorText = CreateLine((RectTransform)columnGo.transform, 22f, FontStyles.Normal, new Color(0.85f, 0.85f, 0.85f));
            _mapperText = CreateLine((RectTransform)columnGo.transform, 20f, FontStyles.Normal, new Color(0.78f, 0.78f, 0.78f));
            BuildDurationRow((RectTransform)columnGo.transform);
        }

        private void BuildDurationRow(RectTransform parent)
        {
            _durationRowGo = new GameObject("DurationDifficultyRow", typeof(RectTransform));
            _durationRowGo.transform.SetParent(parent, false);
            var rowLayout = _durationRowGo.AddComponent<HorizontalLayoutGroup>();
            rowLayout.spacing = 18f;
            rowLayout.childAlignment = TextAnchor.MiddleLeft;
            rowLayout.childControlWidth = true;
            rowLayout.childControlHeight = true;
            rowLayout.childForceExpandWidth = false;
            rowLayout.childForceExpandHeight = false;
            _durationRowGo.AddComponent<LayoutElement>().preferredWidth = TextColumnWidth;

            _durationText = CreateLine((RectTransform)_durationRowGo.transform, 22f, FontStyles.Normal,
                new Color(0.85f, 0.85f, 0.85f));
            _durationText.gameObject.AddComponent<LayoutElement>().preferredWidth = 150f;

            _difficultyText = CreateLine((RectTransform)_durationRowGo.transform, 22f, FontStyles.Bold,
                new Color(0.85f, 0.85f, 0.85f));
            _difficultyText.alignment = TextAlignmentOptions.Right;
            _difficultyText.gameObject.AddComponent<LayoutElement>().preferredWidth = 150f;
        }

        // The request-queue group: a status title + the first few queued songs.
        private void BuildRequestsGroup()
        {
            _requestsGroupGo = new GameObject("RequestsGroup", typeof(RectTransform));
            _requestsGroupGo.transform.SetParent(_card, false);
            var groupLayout = _requestsGroupGo.AddComponent<VerticalLayoutGroup>();
            groupLayout.childAlignment = TextAnchor.UpperLeft;
            groupLayout.childControlWidth = true;
            groupLayout.childControlHeight = true;
            groupLayout.childForceExpandWidth = true;
            groupLayout.childForceExpandHeight = false;
            groupLayout.spacing = 4f;
            _requestsGroupGo.AddComponent<LayoutElement>().preferredWidth = TextColumnWidth;

            _requestTitleText = CreateLine((RectTransform)_requestsGroupGo.transform, 26f, FontStyles.Bold,
                new Color(0.45f, 0.85f, 1f));
            _requestLines = new TextMeshProUGUI[QueuePreviewCount];
            for (var i = 0; i < _requestLines.Length; i++)
                _requestLines[i] = CreateLine((RectTransform)_requestsGroupGo.transform, 21f, FontStyles.Normal,
                    new Color(0.9f, 0.9f, 0.9f));

            _requestsGroupGo.SetActive(false);
        }

        private TextMeshProUGUI CreateLine(RectTransform parent, float fontSize, FontStyles style, Color color)
        {
            var text = BeatSaberUI.CreateText(parent, "", Vector2.zero, Vector2.zero);
            text.fontSize = fontSize;
            text.fontStyle = style;
            text.color = color;
            text.alignment = TextAlignmentOptions.Left;
            text.enableWordWrapping = false;
            text.overflowMode = TextOverflowModes.Ellipsis;
            text.raycastTarget = false;
            ApplyFlatFontMaterial(text);
            return text;
        }

        // BeatSaberUI.CreateText hands back the game's curved-canvas font material.
        // The curved shader shears/clips glyphs unless a CurvedCanvasSettings feeds
        // it a radius, which a flat Screen-Space Overlay canvas has none of — so
        // clone the material once with the CURVED keyword off and share it.
        private void ApplyFlatFontMaterial(TextMeshProUGUI text)
        {
            if (_flatFontMaterial == null)
            {
                var baseMaterial = text.fontSharedMaterial;
                if (baseMaterial == null)
                    return;
                _flatFontMaterial = new Material(baseMaterial)
                {
                    name = baseMaterial.name + " (SimpleBSStreamChat Overlay Flat)"
                };
                _flatFontMaterial.DisableKeyword("CURVED");
            }
            text.fontSharedMaterial = _flatFontMaterial;
            text.SetMaterialDirty();
        }

        private void OnDestroy()
        {
            CancelKeyResolve();
            if (_coverTexture != null)
                Destroy(_coverTexture);
            if (_flatFontMaterial != null)
                Destroy(_flatFontMaterial);
            _http.Dispose();
            if (_canvasGo != null)
                Destroy(_canvasGo);
        }

        private void CancelKeyResolve()
        {
            var cts = _keyCts;
            if (cts == null)
                return;
            _keyCts = null;
            cts.Cancel();
            cts.Dispose();
        }
    }
}
