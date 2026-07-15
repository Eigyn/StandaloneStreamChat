using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;
using SimpleBSStreamChat.Chat.YouTube;
using IPALogger = IPA.Logging.Logger;

namespace SimpleBSStreamChat.Chat.Emotes
{
    internal sealed class ThirdPartyEmoteCatalog : IDisposable
    {
        private readonly object _lock = new object();
        private readonly HttpClient _http = InnerTubeChatSession.CreateHttpClient();
        private Dictionary<string, EmoteRef> _emotes = new Dictionary<string, EmoteRef>();
        private string? _loadedKey;
        private string? _loadingKey;
        private int _version;

        public int Version => Volatile.Read(ref _version);

        public void EnsureStarted(PluginConfig config, IPALogger? log)
        {
            var key = BuildKey(config);
            lock (_lock)
            {
                if (key == _loadedKey || key == _loadingKey)
                    return;
                _loadingKey = key;
            }

            var snapshot = new SettingsSnapshot(config);
            Task.Run(() => LoadAsync(snapshot, key, log));
        }

        public bool TryGet(string token, out EmoteRef emote)
        {
            lock (_lock)
                return _emotes.TryGetValue(token, out emote!);
        }

        public void Dispose()
        {
            _http.Dispose();
        }

        private async Task LoadAsync(SettingsSnapshot settings, string key, IPALogger? log)
        {
            var global = new Dictionary<string, EmoteRef>();
            var channel = new Dictionary<string, EmoteRef>();
            try
            {
                if (settings.Enabled)
                {
                    var providerOrder = settings.ProviderOrder;
                    foreach (var provider in providerOrder)
                    {
                        if (provider == "7TV" && settings.SevenTv)
                            await LoadSevenTvGlobalAsync(global).ConfigureAwait(false);
                        else if (provider == "BTTV" && settings.Bttv)
                            await LoadBttvGlobalAsync(global).ConfigureAwait(false);
                        else if (provider == "FFZ" && settings.Ffz)
                            await LoadFfzGlobalAsync(global).ConfigureAwait(false);
                    }

                    var twitchId = string.IsNullOrWhiteSpace(settings.TwitchChannel)
                        ? null
                        : await ResolveTwitchIdAndMaybeFfzRoomAsync(settings.TwitchChannel, channel, settings.Ffz)
                            .ConfigureAwait(false);

                    if (!string.IsNullOrWhiteSpace(twitchId))
                    {
                        foreach (var provider in providerOrder)
                        {
                            if (provider == "7TV" && settings.SevenTv)
                                await LoadSevenTvChannelAsync(twitchId!, channel).ConfigureAwait(false);
                            else if (provider == "BTTV" && settings.Bttv)
                                await LoadBttvChannelAsync(twitchId!, channel).ConfigureAwait(false);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                log?.Debug("Third-party emote load failed: " + ex.Message);
            }

            var merged = new Dictionary<string, EmoteRef>(global);
            foreach (var pair in channel)
                merged[pair.Key] = pair.Value;

            lock (_lock)
            {
                _emotes = merged;
                _loadedKey = key;
                _loadingKey = null;
                _version++;
            }
            log?.Debug($"Third-party emotes loaded: {merged.Count}");
        }

        private async Task LoadBttvGlobalAsync(Dictionary<string, EmoteRef> map)
        {
            var json = await GetJsonAsync("https://api.betterttv.net/3/cached/emotes/global").ConfigureAwait(false);
            if (!(json is JArray array))
                return;
            AddBttvArray(map, array, "bttv:g:");
        }

        private async Task LoadBttvChannelAsync(string twitchId, Dictionary<string, EmoteRef> map)
        {
            var json = await GetJsonAsync("https://api.betterttv.net/3/cached/users/twitch/" + twitchId)
                .ConfigureAwait(false);
            if (!(json is JObject root))
                return;
            AddBttvArray(map, root["channelEmotes"] as JArray, "bttv:c:");
            AddBttvArray(map, root["sharedEmotes"] as JArray, "bttv:s:");
        }

        private static void AddBttvArray(Dictionary<string, EmoteRef> map, JArray? array, string keyPrefix)
        {
            if (array == null)
                return;
            foreach (var item in array)
            {
                var id = item["id"]?.ToString();
                var code = item["code"]?.ToString();
                if (string.IsNullOrWhiteSpace(id) || string.IsNullOrWhiteSpace(code))
                    continue;
                AddFirst(map, code!, new EmoteRef(
                    keyPrefix + id,
                    "https://cdn.betterttv.net/emote/" + id + "/2x",
                    code!,
                    EmoteFormat.Unknown));
            }
        }

        private async Task LoadFfzGlobalAsync(Dictionary<string, EmoteRef> map)
        {
            var json = await GetJsonAsync("https://api.frankerfacez.com/v1/set/global").ConfigureAwait(false);
            AddFfzSets(map, json as JObject, "ffz:g:");
        }

        private async Task<string?> ResolveTwitchIdAndMaybeFfzRoomAsync(string channel,
            Dictionary<string, EmoteRef> map, bool includeFfzEmotes)
        {
            var slug = channel.Trim().TrimStart('#', '@').ToLowerInvariant();
            if (slug.Length == 0)
                return null;

            var json = await GetJsonAsync("https://api.frankerfacez.com/v1/room/" + Uri.EscapeDataString(slug))
                .ConfigureAwait(false);
            if (!(json is JObject root))
                return null;

            if (includeFfzEmotes)
                AddFfzSets(map, root, "ffz:c:");

            return root["room"]?["twitch_id"]?.ToString();
        }

        private static void AddFfzSets(Dictionary<string, EmoteRef> map, JObject? root, string keyPrefix)
        {
            if (!(root?["sets"] is JObject sets))
                return;
            foreach (var set in sets.Properties())
            {
                if (!(set.Value["emoticons"] is JArray emoticons))
                    continue;
                foreach (var emote in emoticons)
                {
                    var id = emote["id"]?.ToString();
                    var name = emote["name"]?.ToString();
                    var url = BestFfzUrl(emote["urls"] as JObject);
                    if (string.IsNullOrWhiteSpace(id) || string.IsNullOrWhiteSpace(name) ||
                        string.IsNullOrWhiteSpace(url))
                        continue;
                    AddFirst(map, name!, new EmoteRef(keyPrefix + id, url!, name!, EmoteFormat.Unknown));
                }
            }
        }

        private static string? BestFfzUrl(JObject? urls)
        {
            if (urls == null)
                return null;
            var value = urls["4"]?.ToString() ?? urls["2"]?.ToString() ?? urls["1"]?.ToString();
            if (string.IsNullOrWhiteSpace(value))
                return null;
            return value!.StartsWith("//", StringComparison.Ordinal) ? "https:" + value : value;
        }

        private async Task LoadSevenTvGlobalAsync(Dictionary<string, EmoteRef> map)
        {
            var json = await GetJsonAsync("https://7tv.io/v3/emote-sets/global").ConfigureAwait(false);
            AddSevenTvEmotes(map, json?["emotes"] as JArray, "7tv:g:");
        }

        private async Task LoadSevenTvChannelAsync(string twitchId, Dictionary<string, EmoteRef> map)
        {
            var json = await GetJsonAsync("https://7tv.io/v3/users/twitch/" + twitchId).ConfigureAwait(false);
            AddSevenTvEmotes(map, json?["emote_set"]?["emotes"] as JArray, "7tv:c:");
        }

        private static void AddSevenTvEmotes(Dictionary<string, EmoteRef> map, JArray? emotes, string keyPrefix)
        {
            if (emotes == null)
                return;
            foreach (var emote in emotes)
            {
                var id = emote["id"]?.ToString() ?? emote["data"]?["id"]?.ToString();
                var name = emote["name"]?.ToString();
                var host = emote["data"]?["host"] ?? emote["host"];
                var url = BestSevenTvUrl(host as JObject, id);
                if (string.IsNullOrWhiteSpace(id) || string.IsNullOrWhiteSpace(name) ||
                    string.IsNullOrWhiteSpace(url))
                    continue;
                AddFirst(map, name!, new EmoteRef(keyPrefix + id, url!, name!, EmoteFormat.WebP));
            }
        }

        private static string? BestSevenTvUrl(JObject? host, string? id)
        {
            if (host != null)
            {
                var baseUrl = host["url"]?.ToString();
                if (!string.IsNullOrWhiteSpace(baseUrl) && host["files"] is JArray files)
                {
                    string? chosen = null;
                    foreach (var file in files)
                    {
                        var format = file["format"]?.ToString();
                        var name = file["name"]?.ToString();
                        if (!string.Equals(format, "WEBP", StringComparison.OrdinalIgnoreCase) ||
                            string.IsNullOrWhiteSpace(name))
                            continue;
                        chosen = name;
                        if (name!.StartsWith("2x", StringComparison.OrdinalIgnoreCase))
                            break;
                    }
                    if (chosen != null)
                    {
                        var prefix = baseUrl!.StartsWith("//", StringComparison.Ordinal) ? "https:" + baseUrl : baseUrl;
                        return prefix.TrimEnd('/') + "/" + chosen;
                    }
                }
            }

            return string.IsNullOrWhiteSpace(id) ? null : "https://cdn.7tv.app/emote/" + id + "/2x.webp";
        }

        private async Task<JToken?> GetJsonAsync(string url)
        {
            try
            {
                using (var response = await _http.GetAsync(url).ConfigureAwait(false))
                {
                    if (!response.IsSuccessStatusCode)
                        return null;
                    var json = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                    return JToken.Parse(json);
                }
            }
            catch
            {
                return null;
            }
        }

        private static void AddFirst(Dictionary<string, EmoteRef> map, string name, EmoteRef emote)
        {
            if (!map.ContainsKey(name))
                map[name] = emote;
        }

        private static string BuildKey(PluginConfig config) =>
            (config.EmojiImages ? "on" : "off") + "|" +
            config.TwitchChannel.Trim().ToLowerInvariant() + "|" +
            (config.EmoteProviderPriority ?? "");

        private sealed class SettingsSnapshot
        {
            public SettingsSnapshot(PluginConfig config)
            {
                Enabled = config.EmojiImages;
                SevenTv = true;
                Bttv = true;
                Ffz = true;
                TwitchChannel = config.TwitchChannel ?? "";
                ProviderOrder = ParseProviderOrder(config.EmoteProviderPriority);
            }

            public bool Enabled { get; }
            public bool SevenTv { get; }
            public bool Bttv { get; }
            public bool Ffz { get; }
            public string TwitchChannel { get; }
            public string[] ProviderOrder { get; }

            private static string[] ParseProviderOrder(string? value)
            {
                var result = new List<string>();
                foreach (var raw in (value ?? "").Split(','))
                {
                    var item = raw.Trim().ToUpperInvariant();
                    if ((item == "7TV" || item == "BTTV" || item == "FFZ") && !result.Contains(item))
                        result.Add(item);
                }
                if (result.Count == 0)
                {
                    result.Add("7TV");
                    result.Add("BTTV");
                    result.Add("FFZ");
                }
                return result.ToArray();
            }
        }
    }
}
