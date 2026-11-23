#include "Requests/BeatSaver.hpp"
#include "Chat/Json.hpp"
#include "Net/HttpClient.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>

namespace StandaloneStreamChat::Requests {
    namespace {
        namespace Json = Chat::Json;

        bool IsHash(std::string const& value) {
            return value.size() == 40 && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c); });
        }

        // The version BeatSaverSharp calls LatestVersion: the newest upload.
        Json::Value const* NewestVersion(Json::Value const* versions) {
            if (!Json::IsArray(versions))
                return nullptr;
            Json::Value const* newest = nullptr;
            std::string newestCreated;
            for (auto const& version : versions->GetArray()) {
                // ISO 8601 UTC timestamps, so text order is time order.
                auto created = Json::Scalar(Json::Get(&version, "createdAt"));
                if (!newest || created > newestCreated) {
                    newest = &version;
                    newestCreated = created;
                }
            }
            return newest;
        }

        double Number(Json::Value const* value, double fallback) { return value && value->IsNumber() ? value->GetDouble() : fallback; }

        float LowestNps(Json::Value const* version) {
            auto diffs = Json::Get(version, "diffs");
            if (!Json::IsArray(diffs))
                return -1.0f;
            double lowest = -1.0;
            for (auto const& diff : diffs->GetArray()) {
                double nps = Number(Json::Get(&diff, "nps"), -1.0);
                if (nps >= 0.0 && (lowest < 0.0 || nps < lowest))
                    lowest = nps;
            }
            return static_cast<float>(lowest);
        }
    }

    std::string MapFilterReason(MapInfo const& map, int minRatingPercent, int maxLengthMinutes, float maxNps) {
        int rating = static_cast<int>(std::lround(map.rating * 100.0f));
        if (minRatingPercent > 0 && map.rating >= 0.0f && rating < minRatingPercent)
            return "rated " + std::to_string(rating) + "%, below " + std::to_string(minRatingPercent) + "%";
        if (maxLengthMinutes > 0 && map.durationSeconds > maxLengthMinutes * 60) {
            int seconds = map.durationSeconds % 60;
            return std::to_string(map.durationSeconds / 60) + (seconds < 10 ? ":0" : ":") + std::to_string(seconds) + " long, over " +
                   std::to_string(maxLengthMinutes) + " min";
        }
        if (maxNps > 0.0f && map.lowestNps > maxNps) {
            char text[64];
            std::snprintf(text, sizeof(text), "easiest difficulty %.1f NPS, over %.1f", map.lowestNps, maxNps);
            return text;
        }
        return {};
    }

    std::string UserAgentHeader() { return "User-Agent: StandaloneStreamChat/" VERSION; }

    MapLookup LookUpMap(Net::HttpClient& http, std::string const& key) {
        MapLookup result;
        try {
            auto response = http.Get("https://api.beatsaver.com/maps/id/" + key, {UserAgentHeader()});
            if (response.status == 404) {
                result.outcome = LookupOutcome::NotFound;
                return result;
            }
            if (!response.Ok()) {
                result.error = "HTTP " + std::to_string(response.status);
                return result;
            }

            rapidjson::Document doc;
            doc.Parse(response.body.c_str(), response.body.size());
            if (doc.HasParseError() || !doc.IsObject()) {
                result.error = "unreadable reply";
                return result;
            }

            auto version = NewestVersion(Json::Get(&doc, "versions"));
            auto hash = Json::Scalar(Json::Get(version, "hash"));
            auto url = Json::Scalar(Json::Get(version, "downloadURL"));
            std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) { return std::tolower(c); });
            if (!IsHash(hash) || !url.starts_with("https://")) {
                // Nothing installable.
                result.outcome = LookupOutcome::NotFound;
                return result;
            }

            auto metadata = Json::Get(&doc, "metadata");
            result.info.songName = Json::Scalar(Json::Get(metadata, "songName"));
            if (result.info.songName.empty())
                result.info.songName = Json::Scalar(Json::Get(&doc, "name"));
            result.info.songAuthor = Json::Scalar(Json::Get(metadata, "songAuthorName"));
            result.info.mapper = Json::Scalar(Json::Get(metadata, "levelAuthorName"));
            result.info.hash = std::move(hash);
            result.info.downloadUrl = std::move(url);
            result.info.rating = static_cast<float>(Number(Json::Path(&doc, {"stats", "score"}), -1.0));
            result.info.durationSeconds = static_cast<int>(Number(Json::Get(metadata, "duration"), -1.0));
            result.info.lowestNps = LowestNps(version);
            result.outcome = LookupOutcome::Found;
        } catch (std::exception const& ex) {
            result.error = ex.what();
        }
        return result;
    }
}
