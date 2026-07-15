using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using SimpleBSStreamChat.Chat;
using SixLabors.ImageSharp.Formats.Gif;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;
using SixLabors.ImageSharp.Processing;
using TMPro;
using UnityEngine;
using UnityEngine.TextCore;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.UI
{
    /// <summary>
    /// Shared runtime atlas for Unicode Twemoji and platform/third-party emotes.
    /// Fetch/decode work happens off-thread; atlas texture writes happen from
    /// the panel's Update pump on Unity's main thread.
    /// </summary>
    internal sealed class EmoteAtlas
    {
        private const int AtlasSize = 2048;
        private const int CellHeight = 96;
        private const int MaxEmoteWidth = CellHeight * 4;
        private const int MaxFramesPerEmote = 64;
        private const int MinFrameDelayMs = 10;
        private const int DefaultFrameDelayMs = 100;
        private const int MaxFrameDelayMs = 1000;
        private const int MaxAnimationCatchUpFrames = 12;
        private const int AtlasPadding = 2;
        private const float SpriteDisplayScale = 0.9f;
        private const string TwemojiCdnBase = "https://cdn.jsdelivr.net/gh/jdecked/twemoji@16.0.1/assets/72x72/";

        private enum EntryState { Pending, Ready, Missing }

        private sealed class Entry
        {
            public EntryState State;
            public string Key = "";
            public string Tag = "";
            public PackedRect Rect = new PackedRect(0, 0, 0, 0);
            public int InnerWidth;
            public int InnerHeight;
            public DecodedFrame[] Frames = Array.Empty<DecodedFrame>();
            public int FrameIndex;
            public float NextFrameAt;
            public long LastUsed;
            public bool Animated => Frames.Length > 1;
        }

        private sealed class PackedRect
        {
            public int X;
            public int Y;
            public int Width;
            public int Height;

            public PackedRect(int x, int y, int width, int height)
            {
                X = x;
                Y = y;
                Width = width;
                Height = height;
            }
        }

        private sealed class DecodedEmote
        {
            public int Width = CellHeight;
            public int Height = CellHeight;
            public DecodedFrame[] Frames = Array.Empty<DecodedFrame>();
        }

        private sealed class DecodedFrame
        {
            public Color32[] Pixels = Array.Empty<Color32>();
            public int DelayMs = DefaultFrameDelayMs;
        }

        private sealed class CompletedItem
        {
            public string Key = "";
            public DecodedEmote? Emote;
        }

        private static readonly HttpClient Http = new HttpClient
        {
            Timeout = TimeSpan.FromSeconds(15),
        };

        private readonly IPALogger? _log;
        private readonly string _cacheDir;
        private readonly Texture2D _atlas;
        private readonly Material? _curvedMaterial;
        private readonly Material? _flatMaterial;
        private readonly Dictionary<string, Entry> _entries = new Dictionary<string, Entry>();
        private readonly List<PackedRect> _freeRects = new List<PackedRect>();
        private readonly ConcurrentQueue<CompletedItem> _completed = new ConcurrentQueue<CompletedItem>();
        private readonly StringBuilder _keyBuilder = new StringBuilder(48);
        private readonly int[] _emojiCodepoints = new int[24];
        private readonly int[] _emojiProbe = new int[8];
        private long _useCounter;
        private long _evictionCounter;
        private bool _tablesDirty;
        private bool _textureDirty;

        public TMP_SpriteAsset SpriteAsset { get; }

        public EmoteAtlas(IPALogger? log)
        {
            _log = log;
            _cacheDir = Path.Combine(IPA.Utilities.UnityGame.UserDataPath, "SimpleBSStreamChat", "EmoteCache");
            Directory.CreateDirectory(_cacheDir);

            _atlas = new Texture2D(AtlasSize, AtlasSize, TextureFormat.RGBA32, false)
            {
                name = "SimpleBSStreamChatEmoteAtlas",
                wrapMode = TextureWrapMode.Clamp,
                filterMode = FilterMode.Point,
            };
            _atlas.SetPixels32(new Color32[AtlasSize * AtlasSize]);
            _atlas.Apply(false);
            _freeRects.Add(new PackedRect(0, 0, AtlasSize, AtlasSize));

            var noGlow = BeatSaberMarkupLanguage.Utilities.ImageResources.NoGlowMat;
            if (noGlow != null)
                _curvedMaterial = new Material(noGlow) { name = "SimpleBSStreamChatEmotes (Curved)", mainTexture = _atlas };
            var spriteShader = Shader.Find("TextMeshPro/Sprite");
            if (spriteShader != null)
                _flatMaterial = new Material(spriteShader) { name = "SimpleBSStreamChatEmotes (Flat)", mainTexture = _atlas };

            SpriteAsset = ScriptableObject.CreateInstance<TMP_SpriteAsset>();
            SpriteAsset.name = "SimpleBSStreamChatEmotes";
            SpriteAsset.spriteSheet = _atlas;
            SpriteAsset.material = _curvedMaterial != null ? _curvedMaterial : _flatMaterial;

            var face = new FaceInfo
            {
                pointSize = CellHeight,
                scale = 1f,
                lineHeight = CellHeight,
                ascentLine = CellHeight * 0.86f,
                descentLine = -CellHeight * 0.14f,
            };
            InitializeSpriteAsset(SpriteAsset, face);
        }

        private static void InitializeSpriteAsset(TMP_SpriteAsset asset, FaceInfo face)
        {
            const BindingFlags flags = BindingFlags.NonPublic | BindingFlags.Instance;
            var type = typeof(TMP_SpriteAsset);
            type.GetField("m_Version", flags)?.SetValue(asset, "1.1.0");
            type.GetField("m_FaceInfo", flags)?.SetValue(asset, face);
            EnsureList(type, asset, "spriteInfoList");
            EnsureList(type, asset, "m_SpriteGlyphTable");
            EnsureList(type, asset, "m_SpriteCharacterTable");
            asset.UpdateLookupTables();
        }

        private static void EnsureList(Type type, object target, string fieldName)
        {
            var field = type.GetField(fieldName, BindingFlags.NonPublic | BindingFlags.Instance)
                        ?? type.GetField(fieldName, BindingFlags.Public | BindingFlags.Instance);
            if (field != null && field.GetValue(target) == null)
                field.SetValue(target, Activator.CreateInstance(field.FieldType));
        }

        public void SetCurved(bool curved)
        {
            var material = curved
                ? (_curvedMaterial != null ? _curvedMaterial : _flatMaterial)
                : (_flatMaterial != null ? _flatMaterial : _curvedMaterial);
            if (material != null && SpriteAsset.material != material)
                SpriteAsset.material = material;
        }

        public void AppendTextWithEmoji(StringBuilder builder, string text, bool emojiImages)
        {
            if (!emojiImages || !MayContainEmoji(text))
            {
                builder.Append(text);
                return;
            }

            var i = 0;
            while (i < text.Length)
            {
                var length = TryReadEmoji(text, i, out var emojiKey);
                if (length == 0)
                {
                    if (!IsEmojiControl(text[i]))
                        builder.Append(text[i]);
                    i++;
                    continue;
                }

                var emote = new EmoteRef(
                    "u:" + emojiKey,
                    TwemojiCdnBase + emojiKey + ".png",
                    text.Substring(i, length),
                    EmoteFormat.Png);
                if (TryAppendEmote(builder, emote))
                    i += length;
                else
                {
                    AppendWithoutEmojiControls(builder, text, i, length);
                    i += length;
                }
            }
        }

        public bool TryAppendEmote(StringBuilder builder, EmoteRef emote)
        {
            if (string.IsNullOrWhiteSpace(emote.Key) || string.IsNullOrWhiteSpace(emote.ImageUrl))
                return false;

            var entry = GetOrRequest(emote);
            if (entry.State != EntryState.Ready)
                return false;

            entry.LastUsed = ++_useCounter;
            builder.Append(entry.Tag);
            return true;
        }

        public bool Pump(bool animate)
        {
            var newSprites = false;
            while (_completed.TryDequeue(out var item))
            {
                if (!_entries.TryGetValue(item.Key, out var entry) || entry.State != EntryState.Pending)
                    continue;

                if (item.Emote == null || item.Emote.Frames.Length == 0)
                {
                    entry.State = EntryState.Missing;
                    continue;
                }

                if (TryAddSprite(item.Key, item.Emote, entry))
                    newSprites = true;
                else
                    entry.State = EntryState.Missing;
            }

            if (animate)
                AnimateReadyEntries();

            if (_textureDirty)
            {
                _atlas.Apply(false);
                _textureDirty = false;
            }
            if (_tablesDirty)
            {
                SpriteAsset.UpdateLookupTables();
                _tablesDirty = false;
            }
            return newSprites;
        }

        private Entry GetOrRequest(EmoteRef emote)
        {
            if (_entries.TryGetValue(emote.Key, out var entry))
            {
                entry.LastUsed = ++_useCounter;
                return entry;
            }

            entry = new Entry
            {
                Key = emote.Key,
                State = EntryState.Pending,
                LastUsed = ++_useCounter,
            };
            _entries[emote.Key] = entry;

            if (File.Exists(MissingPath(emote)))
            {
                entry.State = EntryState.Missing;
                return entry;
            }

            System.Threading.Tasks.Task.Run(() => FetchAndDecodeAsync(emote));
            return entry;
        }

        private async System.Threading.Tasks.Task FetchAndDecodeAsync(EmoteRef emote)
        {
            try
            {
                byte[] bytes;
                var cachePath = CachePath(emote);
                if (File.Exists(cachePath))
                {
                    bytes = File.ReadAllBytes(cachePath);
                }
                else
                {
                    using (var response = await Http.GetAsync(emote.ImageUrl).ConfigureAwait(false))
                    {
                        if (!response.IsSuccessStatusCode)
                        {
                            if ((int)response.StatusCode == 404)
                                File.WriteAllBytes(MissingPath(emote), Array.Empty<byte>());
                            _completed.Enqueue(new CompletedItem { Key = emote.Key, Emote = null });
                            return;
                        }
                        bytes = await response.Content.ReadAsByteArrayAsync().ConfigureAwait(false);
                    }
                    File.WriteAllBytes(cachePath, bytes);
                }

                var decoded = DecodeImage(bytes, emote);
                _completed.Enqueue(new CompletedItem { Key = emote.Key, Emote = decoded });
            }
            catch (Exception ex)
            {
                _log?.Debug($"Emote fetch/decode failed for {emote.Key}: {ex.Message}");
                _completed.Enqueue(new CompletedItem { Key = emote.Key, Emote = null });
            }
        }

        private static DecodedEmote? DecodeImage(byte[] bytes, EmoteRef emote)
        {
            using (var image = Image.Load<Rgba32>(bytes))
            {
                if (image.Width <= 0 || image.Height <= 0)
                    return null;

                var targetHeight = CellHeight;
                var targetWidth = Math.Max(1, (int)Math.Round(image.Width * (CellHeight / (double)image.Height)));
                targetWidth = Math.Min(MaxEmoteWidth, Math.Max(16, targetWidth));

                var frameCount = emote.Format == EmoteFormat.WebP || LooksLikeWebP(bytes)
                    ? 1
                    : Math.Min(image.Frames.Count, MaxFramesPerEmote);
                var useDrawingGifDecoder = LooksLikeGif(bytes) && frameCount > 1;
                if (useDrawingGifDecoder)
                {
                    var delays = new int[Math.Max(1, frameCount)];
                    for (var i = 0; i < delays.Length; i++)
                        delays[i] = ReadFrameDelayMs(image.Frames[i]);

                    var drawingDecoded = DecodeGifWithSystemDrawing(bytes, targetWidth, targetHeight, delays);
                    if (drawingDecoded != null)
                        return drawingDecoded;
                }

                var frames = new DecodedFrame[Math.Max(1, frameCount)];
                for (var i = 0; i < frames.Length; i++)
                {
                    // CloneFrame is non-destructive. ExportFrame *removes* the frame
                    // and throws "Cannot remove last frame" on single-frame images,
                    // which broke every static emote — Twemoji PNGs included.
                    using (var frameImage = image.Frames.CloneFrame(i))
                    {
                        if (frameImage.Width != targetWidth || frameImage.Height != targetHeight)
                            frameImage.Mutate(x => x.Resize(new ResizeOptions
                            {
                                Size = new Size(targetWidth, targetHeight),
                                Sampler = KnownResamplers.NearestNeighbor,
                            }));

                        frames[i] = new DecodedFrame
                        {
                            Pixels = CopyPixels(frameImage),
                            DelayMs = ReadFrameDelayMs(image.Frames[i]),
                        };
                    }
                }

                return new DecodedEmote
                {
                    Width = targetWidth,
                    Height = targetHeight,
                    Frames = frames,
                };
            }
        }

        private static int ReadFrameDelayMs(ImageFrame<Rgba32> frame)
        {
            try
            {
                var delay = frame.Metadata.GetGifMetadata().FrameDelay;
                return delay > 0 ? Mathf.Clamp(delay * 10, MinFrameDelayMs, MaxFrameDelayMs) : DefaultFrameDelayMs;
            }
            catch
            {
                return DefaultFrameDelayMs;
            }
        }

        private static Color32[] CopyPixels(Image<Rgba32> image)
        {
            var pixels = new Color32[image.Width * image.Height];
            for (var y = 0; y < image.Height; y++)
            {
                var destY = image.Height - 1 - y;
                for (var x = 0; x < image.Width; x++)
                {
                    var pixel = image[x, y];
                    pixels[destY * image.Width + x] = pixel.A == 0
                        ? new Color32(0, 0, 0, 0)
                        : new Color32(pixel.R, pixel.G, pixel.B, pixel.A);
                }
            }
            return pixels;
        }

        private static DecodedEmote? DecodeGifWithSystemDrawing(byte[] bytes, int targetWidth,
            int targetHeight, int[] delays)
        {
            try
            {
                using (var stream = new MemoryStream(bytes))
                using (var image = System.Drawing.Image.FromStream(stream, useEmbeddedColorManagement: false,
                           validateImageData: false))
                {
                    if (image.FrameDimensionsList == null || image.FrameDimensionsList.Length == 0)
                        return null;

                    var dimension = new System.Drawing.Imaging.FrameDimension(image.FrameDimensionsList[0]);
                    var frameCount = Math.Min(delays.Length, Math.Min(image.GetFrameCount(dimension), MaxFramesPerEmote));
                    if (frameCount <= 0)
                        return null;

                    var frames = new DecodedFrame[frameCount];
                    for (var i = 0; i < frameCount; i++)
                    {
                        image.SelectActiveFrame(dimension, i);
                        using (var source = new System.Drawing.Bitmap(image.Width, image.Height,
                                   System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                        {
                            using (var graphics = System.Drawing.Graphics.FromImage(source))
                            {
                                graphics.CompositingMode = System.Drawing.Drawing2D.CompositingMode.SourceCopy;
                                graphics.Clear(System.Drawing.Color.Transparent);
                                graphics.DrawImageUnscaled(image, 0, 0);
                            }

                            if (source.Width == targetWidth && source.Height == targetHeight)
                            {
                                frames[i] = new DecodedFrame
                                {
                                    Pixels = CopyPixels(source),
                                    DelayMs = delays[i],
                                };
                            }
                            else
                            {
                                using (var resized = ResizeDrawingBitmap(source, targetWidth, targetHeight))
                                {
                                    frames[i] = new DecodedFrame
                                    {
                                        Pixels = CopyPixels(resized),
                                        DelayMs = delays[i],
                                    };
                                }
                            }
                        }
                    }

                    return new DecodedEmote
                    {
                        Width = targetWidth,
                        Height = targetHeight,
                        Frames = frames,
                    };
                }
            }
            catch
            {
                return null;
            }
        }

        private static System.Drawing.Bitmap ResizeDrawingBitmap(System.Drawing.Bitmap source,
            int targetWidth, int targetHeight)
        {
            var resized = new System.Drawing.Bitmap(targetWidth, targetHeight,
                System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var graphics = System.Drawing.Graphics.FromImage(resized))
            {
                graphics.CompositingMode = System.Drawing.Drawing2D.CompositingMode.SourceCopy;
                graphics.CompositingQuality = System.Drawing.Drawing2D.CompositingQuality.AssumeLinear;
                graphics.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.NearestNeighbor;
                graphics.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.Half;
                graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.None;
                graphics.Clear(System.Drawing.Color.Transparent);
                graphics.DrawImage(source,
                    new System.Drawing.Rectangle(0, 0, targetWidth, targetHeight),
                    new System.Drawing.Rectangle(0, 0, source.Width, source.Height),
                    System.Drawing.GraphicsUnit.Pixel);
            }
            return resized;
        }

        private static Color32[] CopyPixels(System.Drawing.Bitmap bitmap)
        {
            var width = bitmap.Width;
            var height = bitmap.Height;
            var pixels = new Color32[width * height];
            var rect = new System.Drawing.Rectangle(0, 0, width, height);
            var data = bitmap.LockBits(rect, System.Drawing.Imaging.ImageLockMode.ReadOnly,
                System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            try
            {
                var stride = data.Stride;
                var absStride = Math.Abs(stride);
                var bytes = new byte[absStride * height];
                Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);

                for (var y = 0; y < height; y++)
                {
                    var sourceY = stride > 0 ? y : height - 1 - y;
                    var sourceRow = sourceY * absStride;
                    var destY = height - 1 - y;
                    for (var x = 0; x < width; x++)
                    {
                        var offset = sourceRow + x * 4;
                        var b = bytes[offset];
                        var g = bytes[offset + 1];
                        var r = bytes[offset + 2];
                        var a = bytes[offset + 3];
                        pixels[destY * width + x] = a == 0
                            ? new Color32(0, 0, 0, 0)
                            : new Color32(r, g, b, a);
                    }
                }
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
            return pixels;
        }

        private static bool LooksLikeWebP(byte[] bytes) =>
            bytes.Length >= 12 &&
            bytes[0] == (byte)'R' && bytes[1] == (byte)'I' &&
            bytes[2] == (byte)'F' && bytes[3] == (byte)'F' &&
            bytes[8] == (byte)'W' && bytes[9] == (byte)'E' &&
            bytes[10] == (byte)'B' && bytes[11] == (byte)'P';

        private static bool LooksLikeGif(byte[] bytes) =>
            bytes.Length >= 6 &&
            bytes[0] == (byte)'G' && bytes[1] == (byte)'I' && bytes[2] == (byte)'F' &&
            bytes[3] == (byte)'8' && (bytes[4] == (byte)'7' || bytes[4] == (byte)'9') &&
            bytes[5] == (byte)'a';

        private bool TryAddSprite(string key, DecodedEmote emote, Entry entry)
        {
            var width = Mathf.Clamp(emote.Width, 1, MaxEmoteWidth);
            var height = Mathf.Clamp(emote.Height, 1, CellHeight);
            var packedWidth = width + AtlasPadding * 2;
            var packedHeight = height + AtlasPadding * 2;
            var rect = Allocate(packedWidth, packedHeight) ?? EvictAndAllocate(packedWidth, packedHeight);
            if (rect == null)
                return false;

            try
            {
                WriteFrameWithPadding(rect, width, height, emote.Frames[0].Pixels);
                _textureDirty = true;

                var metricsWidth = width * SpriteDisplayScale;
                var metricsHeight = CellHeight * SpriteDisplayScale;
                var glyphIndex = (uint)SpriteAsset.spriteGlyphTable.Count;
                var glyph = new TMP_SpriteGlyph
                {
                    index = glyphIndex,
                    metrics = new GlyphMetrics(metricsWidth, metricsHeight, 0f, CellHeight * 0.80f,
                        metricsWidth * 1.08f),
                    glyphRect = new GlyphRect(rect.X + AtlasPadding, rect.Y + AtlasPadding, width, height),
                    scale = 1f,
                };
                SpriteAsset.spriteGlyphTable.Add(glyph);
                SpriteAsset.spriteCharacterTable.Add(new TMP_SpriteCharacter(0xFFFE, glyph)
                {
                    name = key,
                    scale = 1f,
                });

                entry.Rect = rect;
                entry.InnerWidth = width;
                entry.InnerHeight = height;
                entry.Frames = emote.Frames;
                entry.FrameIndex = 0;
                entry.NextFrameAt = Time.unscaledTime + FrameDelaySeconds(entry.Frames[0]);
                entry.Tag = "<sprite name=\"" + key + "\">";
                entry.State = EntryState.Ready;
                entry.LastUsed = ++_useCounter;
                _tablesDirty = true;
                return true;
            }
            catch (Exception ex)
            {
                _log?.Warn($"Emote atlas add failed for {key}: {ex.Message}");
                return false;
            }
        }

        private void WriteFrameWithPadding(PackedRect rect, int width, int height, Color32[] pixels)
        {
            var padded = new Color32[rect.Width * rect.Height];
            for (var y = 0; y < rect.Height; y++)
            {
                var sourceY = Mathf.Clamp(y - AtlasPadding, 0, height - 1);
                for (var x = 0; x < rect.Width; x++)
                {
                    var sourceX = Mathf.Clamp(x - AtlasPadding, 0, width - 1);
                    padded[y * rect.Width + x] = pixels[sourceY * width + sourceX];
                }
            }
            _atlas.SetPixels32(rect.X, rect.Y, rect.Width, rect.Height, padded);
        }

        private PackedRect? Allocate(int width, int height)
        {
            var bestIndex = -1;
            var bestWaste = int.MaxValue;
            for (var i = 0; i < _freeRects.Count; i++)
            {
                var free = _freeRects[i];
                if (width > free.Width || height > free.Height)
                    continue;
                var waste = free.Width * free.Height - width * height;
                if (waste < bestWaste)
                {
                    bestWaste = waste;
                    bestIndex = i;
                }
            }

            if (bestIndex < 0)
                return null;

            var chosen = _freeRects[bestIndex];
            _freeRects.RemoveAt(bestIndex);
            var used = new PackedRect(chosen.X, chosen.Y, width, height);

            var rightWidth = chosen.Width - width;
            var topHeight = chosen.Height - height;
            if (rightWidth > 0)
                _freeRects.Add(new PackedRect(chosen.X + width, chosen.Y, rightWidth, height));
            if (topHeight > 0)
                _freeRects.Add(new PackedRect(chosen.X, chosen.Y + height, chosen.Width, topHeight));
            PruneFreeRects();
            return used;
        }

        private PackedRect? EvictAndAllocate(int width, int height)
        {
            for (var attempts = 0; attempts < 16; attempts++)
            {
                var victim = FindEvictionVictim();
                if (victim == null)
                    return null;

                Free(victim.Rect);
                RenameSprite(victim.Key, "__evicted_" + (++_evictionCounter).ToString());
                _entries.Remove(victim.Key);

                var rect = Allocate(width, height);
                if (rect != null)
                    return rect;
            }
            return null;
        }

        private Entry? FindEvictionVictim()
        {
            Entry? victim = null;
            foreach (var entry in _entries.Values)
            {
                if (entry.State != EntryState.Ready)
                    continue;
                if (victim == null || entry.LastUsed < victim.LastUsed)
                    victim = entry;
            }
            return victim;
        }

        private void RenameSprite(string oldName, string newName)
        {
            foreach (var character in SpriteAsset.spriteCharacterTable)
            {
                if (character.name == oldName)
                {
                    character.name = newName;
                    _tablesDirty = true;
                    return;
                }
            }
        }

        private void Free(PackedRect rect)
        {
            _freeRects.Add(new PackedRect(rect.X, rect.Y, rect.Width, rect.Height));
            PruneFreeRects();
        }

        private void PruneFreeRects()
        {
            for (var i = _freeRects.Count - 1; i >= 0; i--)
            {
                for (var j = _freeRects.Count - 1; j >= 0; j--)
                {
                    if (i == j)
                        continue;
                    if (Contains(_freeRects[j], _freeRects[i]))
                    {
                        _freeRects.RemoveAt(i);
                        break;
                    }
                }
            }
        }

        private static bool Contains(PackedRect outer, PackedRect inner) =>
            inner.X >= outer.X && inner.Y >= outer.Y &&
            inner.X + inner.Width <= outer.X + outer.Width &&
            inner.Y + inner.Height <= outer.Y + outer.Height;

        private void AnimateReadyEntries()
        {
            var now = Time.unscaledTime;
            foreach (var entry in _entries.Values)
            {
                if (entry.State != EntryState.Ready || !entry.Animated || now < entry.NextFrameAt)
                    continue;

                var advanced = false;
                for (var i = 0; i < MaxAnimationCatchUpFrames && now >= entry.NextFrameAt; i++)
                {
                    entry.FrameIndex = (entry.FrameIndex + 1) % entry.Frames.Length;
                    entry.NextFrameAt += FrameDelaySeconds(entry.Frames[entry.FrameIndex]);
                    advanced = true;
                }

                var frame = entry.Frames[entry.FrameIndex];
                if (now >= entry.NextFrameAt)
                    entry.NextFrameAt = now + FrameDelaySeconds(frame);

                if (!advanced)
                    continue;

                WriteFrameWithPadding(entry.Rect, entry.InnerWidth, entry.InnerHeight, frame.Pixels);
                _textureDirty = true;
            }
        }

        private static float FrameDelaySeconds(DecodedFrame frame) =>
            Mathf.Clamp(frame.DelayMs / 1000f, MinFrameDelayMs / 1000f, MaxFrameDelayMs / 1000f);

        public static void AppendWithoutEmojiControls(StringBuilder builder, string text)
        {
            AppendWithoutEmojiControls(builder, text, 0, text.Length);
        }

        private static void AppendWithoutEmojiControls(StringBuilder builder, string text, int start, int length)
        {
            var end = Math.Min(text.Length, start + length);
            for (var i = start; i < end; i++)
            {
                if (!IsEmojiControl(text[i]))
                    builder.Append(text[i]);
            }
        }

        private static bool IsEmojiControl(char c) =>
            c == '\ufe0f' || c == '\ufe0e' || c == '\u200d';

        private string CachePath(EmoteRef emote) =>
            Path.Combine(_cacheDir, SafeFileName("v2:" + emote.Key + ":" + StableHash(emote.ImageUrl)) + ".img");

        private string MissingPath(EmoteRef emote) =>
            Path.Combine(_cacheDir, SafeFileName("v2:" + emote.Key + ":" + StableHash(emote.ImageUrl)) + ".missing");

        private static string StableHash(string value)
        {
            unchecked
            {
                var hash = 2166136261u;
                foreach (var c in value ?? "")
                {
                    hash ^= c;
                    hash *= 16777619u;
                }
                return hash.ToString("x8");
            }
        }

        private static string SafeFileName(string key)
        {
            var builder = new StringBuilder(key.Length);
            foreach (var c in key)
            {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
                    builder.Append(c);
                else
                    builder.Append('_').Append(((int)c).ToString("x")).Append('_');
            }
            return builder.Length == 0 ? "emote" : builder.ToString();
        }

        public static bool MayContainEmoji(string text)
        {
            foreach (var c in text)
            {
                if (char.IsHighSurrogate(c) ||
                    c == '\u00a9' || c == '\u00ae' || c == '\u20e3' ||
                    (c >= '\u2190' && c <= '\u2bff') ||
                    (c >= '\u2900' && c <= '\u297f') ||
                    c == '\u203c' || c == '\u2049' || c == '\u2139' ||
                    c == '\u2122' || c == '\u3030' || c == '\u303d' ||
                    c == '\u3297' || c == '\u3299')
                    return true;
            }
            return false;
        }

        private int TryReadEmoji(string text, int start, out string key)
        {
            key = "";
            var codepointCount = 0;
            var i = start;
            var sawZwj = false;

            if (TryReadKeycap(text, start, out key, out var keycapLength))
                return keycapLength;

            if (!TryReadBase(text, ref i, _emojiCodepoints, ref codepointCount))
                return 0;

            while (i < text.Length && text[i] == '\u200d')
            {
                var j = i + 1;
                var probeCount = 0;
                if (!TryReadBase(text, ref j, _emojiProbe, ref probeCount))
                    break;
                if (codepointCount + 1 + probeCount > _emojiCodepoints.Length)
                    break;
                sawZwj = true;
                _emojiCodepoints[codepointCount++] = 0x200D;
                Array.Copy(_emojiProbe, 0, _emojiCodepoints, codepointCount, probeCount);
                codepointCount += probeCount;
                i = j;
            }

            _keyBuilder.Length = 0;
            for (var idx = 0; idx < codepointCount; idx++)
            {
                var cp = _emojiCodepoints[idx];
                if (cp == 0xFE0F && !sawZwj)
                    continue;
                if (_keyBuilder.Length > 0)
                    _keyBuilder.Append('-');
                _keyBuilder.Append(cp.ToString("x"));
            }
            if (_keyBuilder.Length == 0)
                return 0;
            key = _keyBuilder.ToString();
            return i - start;
        }

        private static bool TryReadKeycap(string text, int start, out string key, out int length)
        {
            key = "";
            length = 0;
            if (start >= text.Length || !IsKeycapBase(text[start]))
                return false;

            var i = start + 1;
            if (i < text.Length && text[i] == '\ufe0f')
                i++;
            if (i >= text.Length || text[i] != '\u20e3')
                return false;

            key = ((int)text[start]).ToString("x") + "-20e3";
            length = i - start + 1;
            return true;
        }

        private static bool TryReadBase(string text, ref int i, int[] codepoints, ref int codepointCount)
        {
            if (i >= text.Length)
                return false;
            var cp = char.ConvertToUtf32(text, i);
            var length = char.IsSurrogatePair(text, i) ? 2 : 1;

            if (IsRegionalIndicator(cp))
            {
                var j = i + length;
                if (j < text.Length)
                {
                    var cp2 = char.ConvertToUtf32(text, j);
                    if (IsRegionalIndicator(cp2))
                    {
                        if (!TryAppend(codepoints, ref codepointCount, cp) ||
                            !TryAppend(codepoints, ref codepointCount, cp2))
                            return false;
                        i = j + 2;
                        return true;
                    }
                }
                return false;
            }

            if (!IsEmojiBase(cp))
                return false;
            if (!TryAppend(codepoints, ref codepointCount, cp))
                return false;
            i += length;

            if (i < text.Length && text[i] == '\ufe0f')
            {
                if (!TryAppend(codepoints, ref codepointCount, 0xFE0F))
                    return false;
                i++;
            }
            if (i + 1 < text.Length && char.IsSurrogatePair(text, i))
            {
                var mod = char.ConvertToUtf32(text, i);
                if (mod >= 0x1F3FB && mod <= 0x1F3FF)
                {
                    if (!TryAppend(codepoints, ref codepointCount, mod))
                        return false;
                    i += 2;
                }
            }
            return true;
        }

        private static bool TryAppend(int[] codepoints, ref int codepointCount, int codepoint)
        {
            if (codepointCount >= codepoints.Length)
                return false;
            codepoints[codepointCount++] = codepoint;
            return true;
        }

        private static bool IsRegionalIndicator(int cp) => cp >= 0x1F1E6 && cp <= 0x1F1FF;

        private static bool IsKeycapBase(char c) =>
            c == '#' || c == '*' || (c >= '0' && c <= '9');

        private static bool IsEmojiBase(int cp) =>
            (cp >= 0x1F000 && cp <= 0x1FAFF) ||
            (cp >= 0x2600 && cp <= 0x27BF) ||
            (cp >= 0x2B00 && cp <= 0x2BFF) ||
            (cp >= 0x2190 && cp <= 0x21FF) ||
            (cp >= 0x2300 && cp <= 0x23FF) ||
            (cp >= 0x25A0 && cp <= 0x25FF) ||
            (cp >= 0x2900 && cp <= 0x297F) ||
            cp == 0x00A9 || cp == 0x00AE ||
            cp == 0x203C || cp == 0x2049 || cp == 0x2139 ||
            cp == 0x2122 || cp == 0x24C2 || cp == 0x3030 || cp == 0x303D ||
            cp == 0x3297 || cp == 0x3299;
    }
}
