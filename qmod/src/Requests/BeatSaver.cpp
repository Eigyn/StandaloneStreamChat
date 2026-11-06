#include "Requests/BeatSaver.hpp"
#include "Chat/Json.hpp"
#include "Net/HttpClient.hpp"

#include <algorithm>
#include <cctype>
#include <exception>

namespace SimpleBSStreamChat::Requests {
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
    }

    std::string UserAgentHeader() { return "User-Agent: SimpleBSStreamChat/" VERSION; }

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
                // Nothing installable (as the PC treats a map without a LatestVersion).
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
            result.outcome = LookupOutcome::Found;
        } catch (std::exception const& ex) {
            result.error = ex.what();
        }
        return result;
    }
}
