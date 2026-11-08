#include "Compat.hpp"

#include "Config.hpp"
#include "logging.hpp"

#include "scotland2/shared/modloader.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace SimpleBSStreamChat {
    namespace {
        Config config;

        std::filesystem::path ConfigPath() { return ConfigDirectory() / (MOD_ID ".json"); }

        void Read(rapidjson::Value const& obj, char const* key, std::string& out) {
            auto it = obj.FindMember(key);
            if (it != obj.MemberEnd() && it->value.IsString())
                out = it->value.GetString();
        }

        void Read(rapidjson::Value const& obj, char const* key, bool& out) {
            auto it = obj.FindMember(key);
            if (it != obj.MemberEnd() && it->value.IsBool())
                out = it->value.GetBool();
        }

        void Read(rapidjson::Value const& obj, char const* key, float& out) {
            auto it = obj.FindMember(key);
            if (it != obj.MemberEnd() && it->value.IsNumber())
                out = it->value.GetFloat();
        }

        void Read(rapidjson::Value const& obj, char const* key, int& out) {
            auto it = obj.FindMember(key);
            if (it != obj.MemberEnd() && it->value.IsInt())
                out = it->value.GetInt();
        }

        template <typename T>
        void Read(rapidjson::Value const& obj, char const* key, Shared<T>& out) {
            T value = out;
            Read(obj, key, value);
            out = value;
        }
    }

    Config& GetConfig() { return config; }

    std::filesystem::path ConfigDirectory() { return modloader::get_external_dir() / "Configs"; }

    bool WriteFileReplacing(std::filesystem::path const& path, std::string_view contents) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        auto temp = path;
        temp += ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            file.close();
            if (!file) {
                std::filesystem::remove(temp, ec);
                return false;
            }
        }
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return false;
        }
        return true;
    }

    void LoadConfig() {
        auto path = ConfigPath();
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            INFO("No config at {}, writing defaults", path.string());
            SaveConfig();
            return;
        }

        std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        rapidjson::Document doc;
        doc.Parse(json.c_str(), json.size());
        if (doc.HasParseError() || !doc.IsObject()) {
            // Move the broken file aside so the next save can't overwrite it;
            // the user can still fix it and rename it back. Run on defaults.
            auto aside = path;
            aside += ".unreadable";
            std::error_code ec;
            std::filesystem::rename(path, aside, ec);
            if (ec)
                ERROR("Config {} is not valid JSON and could not be moved aside ({}), using defaults", path.string(), ec.message());
            else
                ERROR("Config {} is not valid JSON; moved it to {} and using defaults", path.string(), aside.string());
            return;
        }

        Read(doc, "TwitchChannel", config.twitchChannel);
        Read(doc, "EnableTwitch", config.enableTwitch);
        Read(doc, "YouTubeChannel", config.youTubeChannel);
        Read(doc, "EnableYouTube", config.enableYouTube);
        Read(doc, "KickChannel", config.kickChannel);
        Read(doc, "EnableKick", config.enableKick);
        Read(doc, "ShowPanel", config.showPanel);
        Read(doc, "ShowSystemMessages", config.showSystemMessages);
        Read(doc, "ShowPlatformIcons", config.showPlatformIcons);
        Read(doc, "EmojiImages", config.emojiImages);
        Read(doc, "AnimatedEmotes", config.animatedEmotes);
        Read(doc, "HighlightMessages", config.highlightMessages);
        Read(doc, "HighlightOpacity", config.highlightOpacity);
        Read(doc, "ShowViewerCounts", config.showViewerCounts);
        Read(doc, "FontSize", config.fontSize);
        Read(doc, "ViewerCountSize", config.viewerCountSize);
        Read(doc, "PanelWidth", config.panelWidth);
        Read(doc, "PanelHeight", config.panelHeight);
        Read(doc, "CurveAmount", config.curveAmount);
        Read(doc, "CornerRadius", config.cornerRadius);
        Read(doc, "BackgroundColor", config.backgroundColor);
        Read(doc, "BackgroundOpacity", config.backgroundOpacity);
        Read(doc, "PanelUnlocked", config.panelUnlocked);
        Read(doc, "PanelMoveMode", config.panelMoveMode);
        if (config.panelMoveMode < 0 || config.panelMoveMode > 3)
            config.panelMoveMode = 0;
        Read(doc, "PositionX", config.positionX);
        Read(doc, "PositionY", config.positionY);
        Read(doc, "PositionZ", config.positionZ);
        Read(doc, "RotationX", config.rotationX);
        Read(doc, "RotationY", config.rotationY);
        Read(doc, "EnableSongRequests", config.enableSongRequests);
        Read(doc, "WhoCanRequest", config.whoCanRequest);
        Read(doc, "PerUserCooldownSeconds", config.perUserCooldownSeconds);
        Read(doc, "AllowDuplicates", config.allowDuplicates);
        Read(doc, "MaxRequestsPerUser", config.maxRequestsPerUser);
        Read(doc, "PersistRequestQueue", config.persistRequestQueue);
    }

    void SaveConfig() {
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("TwitchChannel"); writer.String(config.twitchChannel.c_str(), config.twitchChannel.size());
        writer.Key("EnableTwitch"); writer.Bool(config.enableTwitch);
        writer.Key("YouTubeChannel"); writer.String(config.youTubeChannel.c_str(), config.youTubeChannel.size());
        writer.Key("EnableYouTube"); writer.Bool(config.enableYouTube);
        writer.Key("KickChannel"); writer.String(config.kickChannel.c_str(), config.kickChannel.size());
        writer.Key("EnableKick"); writer.Bool(config.enableKick);
        writer.Key("ShowPanel"); writer.Bool(config.showPanel);
        writer.Key("ShowSystemMessages"); writer.Bool(config.showSystemMessages);
        writer.Key("ShowPlatformIcons"); writer.Bool(config.showPlatformIcons);
        writer.Key("EmojiImages"); writer.Bool(config.emojiImages);
        writer.Key("AnimatedEmotes"); writer.Bool(config.animatedEmotes);
        writer.Key("HighlightMessages"); writer.Bool(config.highlightMessages);
        writer.Key("HighlightOpacity"); writer.Double(config.highlightOpacity);
        writer.Key("ShowViewerCounts"); writer.Bool(config.showViewerCounts);
        writer.Key("FontSize"); writer.Double(config.fontSize);
        writer.Key("ViewerCountSize"); writer.Double(config.viewerCountSize);
        writer.Key("PanelWidth"); writer.Double(config.panelWidth);
        writer.Key("PanelHeight"); writer.Double(config.panelHeight);
        writer.Key("CurveAmount"); writer.Double(config.curveAmount);
        writer.Key("CornerRadius"); writer.Double(config.cornerRadius);
        writer.Key("BackgroundColor"); writer.String(config.backgroundColor.c_str(), config.backgroundColor.size());
        writer.Key("BackgroundOpacity"); writer.Double(config.backgroundOpacity);
        writer.Key("PanelUnlocked"); writer.Bool(config.panelUnlocked);
        writer.Key("PanelMoveMode"); writer.Int(config.panelMoveMode);
        writer.Key("PositionX"); writer.Double(config.positionX);
        writer.Key("PositionY"); writer.Double(config.positionY);
        writer.Key("PositionZ"); writer.Double(config.positionZ);
        writer.Key("RotationX"); writer.Double(config.rotationX);
        writer.Key("RotationY"); writer.Double(config.rotationY);
        writer.Key("EnableSongRequests"); writer.Bool(config.enableSongRequests);
        writer.Key("WhoCanRequest"); writer.Int(config.whoCanRequest);
        writer.Key("PerUserCooldownSeconds"); writer.Int(config.perUserCooldownSeconds);
        writer.Key("AllowDuplicates"); writer.Bool(config.allowDuplicates);
        writer.Key("MaxRequestsPerUser"); writer.Int(config.maxRequestsPerUser);
        writer.Key("PersistRequestQueue"); writer.Bool(config.persistRequestQueue);
        writer.EndObject();

        auto path = ConfigPath();
        if (!WriteFileReplacing(path, {buffer.GetString(), buffer.GetSize()}))
            ERROR("Could not write config {}", path.string());
    }
}
