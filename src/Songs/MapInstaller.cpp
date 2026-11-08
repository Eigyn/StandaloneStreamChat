#include "Songs/MapInstaller.hpp"
#include "Songs/ZipExtractor.hpp"
#include "Net/Curl.hpp"
#include "Requests/BeatSaver.hpp"
#include "logging.hpp"

#include "scotland2/shared/modloader.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace StandaloneStreamChat::Songs {
    namespace {
        // BeatSaver's own upload cap is far below these; they only stop a bad
        // link from filling memory or storage.
        constexpr size_t MaxZipBytes = 100u << 20;
        constexpr uint64_t MaxUnpackedBytes = 400ull << 20;

        // Same storage as CustomLevels, so finished maps move with a rename.
        std::filesystem::path ScratchRoot() { return modloader::get_external_dir() / "Mods" / MOD_ID / "Downloads"; }

        void RemoveQuietly(std::filesystem::path const& path) {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }

        bool IsInfoFile(std::filesystem::path const& path) {
            auto name = path.filename().string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            return name == "info.dat";
        }

        bool HasInfoFile(std::filesystem::path const& folder) {
            std::error_code ec;
            for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
                if (it->is_regular_file() && IsInfoFile(it->path()))
                    return true;
            }
            return false;
        }

        // The folder holding the map's Info.dat: the zip's top level, or its
        // only subfolder for zips packed one level deep.
        std::filesystem::path MapRoot(std::filesystem::path const& unpacked) {
            if (HasInfoFile(unpacked))
                return unpacked;
            std::error_code ec;
            std::filesystem::path only;
            int count = 0;
            for (std::filesystem::directory_iterator it(unpacked, ec), end; !ec && it != end; it.increment(ec)) {
                count++;
                only = it->path();
            }
            if (count == 1 && std::filesystem::is_directory(only, ec) && HasInfoFile(only))
                return only;
            throw std::runtime_error("no Info.dat in the zip");
        }

        // Never remove a destination SongCore may still have cached.
        void MoveIntoPlace(std::filesystem::path const& from, std::filesystem::path const& to) {
            std::error_code ec;
            std::filesystem::create_directories(to.parent_path());
            if (std::filesystem::exists(to))
                throw std::runtime_error("map destination already exists; retry the download");
            std::filesystem::rename(from, to, ec);
            if (!ec)
                return;
            // Different storage after all: copy instead.
            if (!std::filesystem::create_directory(to))
                throw std::runtime_error("map destination was created by another installer");
            std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, ec);
            if (ec) {
                RemoveQuietly(to);
                throw std::runtime_error("could not move the map into CustomLevels: " + ec.message());
            }
        }

        struct Transfer {
            std::string* body;
            std::atomic<bool> const* stopping;
            MapInstaller::Progress const* progress;
            int64_t id;
            float reported = -1.0f;
            bool tooLarge = false;
        };

        size_t WriteBody(char* data, size_t size, size_t count, void* userdata) {
            auto& transfer = *static_cast<Transfer*>(userdata);
            if (transfer.body->size() + size * count > MaxZipBytes) {
                transfer.tooLarge = true;
                return 0;  // aborts with CURLE_WRITE_ERROR
            }
            transfer.body->append(data, size * count);
            return size * count;
        }

        // Reports whole percents and aborts when the installer shuts down.
        int OnProgress(void* clientp, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
            auto& transfer = *static_cast<Transfer*>(clientp);
            if (transfer.stopping->load())
                return 1;
            if (total > 0) {
                float fraction = std::clamp(static_cast<float>(now) / static_cast<float>(total), 0.0f, 1.0f);
                if (fraction - transfer.reported >= 0.01f) {
                    transfer.reported = fraction;
                    (*transfer.progress)(transfer.id, fraction);
                }
            }
            return 0;
        }
    }

    MapInstaller::MapInstaller(Progress progress) : _progress(std::move(progress)), _thread(&MapInstaller::Run, this) {}

    bool MapInstaller::HasMapFiles(std::filesystem::path const& folder) {
        return HasInfoFile(folder);
    }

    MapInstaller::~MapInstaller() {
        {
            std::lock_guard lock(_lock);
            _stopping = true;
        }
        _wake.notify_one();
        _thread.join();
    }

    void MapInstaller::Install(Job job) {
        {
            std::lock_guard lock(_lock);
            _job = std::move(job);
            _result.reset();
        }
        _wake.notify_one();
    }

    std::optional<MapInstaller::Result> MapInstaller::Take() {
        std::lock_guard lock(_lock);
        auto result = std::move(_result);
        _result.reset();
        return result;
    }

    void MapInstaller::Run() {
        RemoveQuietly(ScratchRoot());  // anything left by a crash mid-install
        while (true) {
            Job job;
            {
                std::unique_lock lock(_lock);
                _wake.wait(lock, [this] { return _stopping || _job; });
                if (_stopping)
                    return;
                job = std::move(*_job);
                _job.reset();
            }

            Result result{job.id};
            try {
                std::string body;
                Download(job.url, job.id, body);
                Unpack(job, body);
                result.ok = true;
                INFO("Installed requested map into {}", job.target.string());
            } catch (std::exception const& ex) {
                if (_stopping)
                    return;
                result.error = ex.what();
                WARNING("Requested map {} failed to install: {}", job.url, ex.what());
            }

            std::lock_guard lock(_lock);
            _result = std::move(result);
        }
    }

    void MapInstaller::Download(std::string const& url, int64_t id, std::string& body) {
        // A map is written to disk, so never accept an unverified server.
        CURL* curl = Net::NewEasyHandle(_stopping, Net::TlsTrust::Required);
        curl_slist* headers = curl_slist_append(nullptr, Requests::UserAgentHeader().c_str());
        struct Cleanup {
            CURL* curl;
            curl_slist* headers;
            ~Cleanup() {
                curl_easy_cleanup(curl);
                curl_slist_free_all(headers);
            }
        } cleanup{curl, headers};

        Transfer transfer{&body, &_stopping, &_progress, id};
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        // Give up on a stalled transfer (under 1 KB/s for 30 s) or a slow one
        // after 10 minutes, rather than leaving the request spinning.
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
        curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(MaxZipBytes));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteBody);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &OnProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);

        auto rc = curl_easy_perform(curl);
        if (transfer.tooLarge || rc == CURLE_FILESIZE_EXCEEDED)
            throw std::runtime_error("map is too large");
        if (rc == CURLE_OPERATION_TIMEDOUT)
            throw std::runtime_error("download timed out");
        Net::Check(rc, "download");
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (status < 200 || status >= 300)
            throw std::runtime_error("download HTTP " + std::to_string(status));
        if (body.empty())
            throw std::runtime_error("download returned no data");
    }

    void MapInstaller::Unpack(Job const& job, std::string const& body) {
        auto scratch = ScratchRoot() / std::to_string(job.id);
        RemoveQuietly(scratch);
        try {
            ExtractZip({reinterpret_cast<uint8_t const*>(body.data()), body.size()}, scratch, MaxUnpackedBytes, _stopping);
            MoveIntoPlace(MapRoot(scratch), job.target);
        } catch (...) {
            RemoveQuietly(scratch);
            throw;
        }
        RemoveQuietly(scratch);
    }
}
