#include "Net/Curl.hpp"
#include "logging.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>

namespace SimpleBSStreamChat::Net {
    namespace {
        // The static libcurl build has no usable default CA path on Android, so
        // feed it the headset's own trusted roots. Android keeps one PEM per CA;
        // Android 14+ moved the live set into the Conscrypt APEX.
        std::string const& SystemCaBundle() {
            static std::string const bundle = [] {
                std::string pem;
                for (auto dir : {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"}) {
                    std::error_code ec;
                    std::filesystem::directory_iterator it(dir, ec), end;
                    for (; !ec && it != end; it.increment(ec)) {
                        std::ifstream file(it->path(), std::ios::binary);
                        pem.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                        pem.push_back('\n');
                    }
                    if (!pem.empty()) {
                        INFO("Loaded system CA certificates from {}", dir);
                        break;
                    }
                }
                if (pem.empty()) {
                    // Chat is public and read-only, so an unverified connection beats
                    // none, but make it loud in the log.
                    ERROR("No system CA certificates readable; connecting WITHOUT certificate verification");
                }
                return pem;
            }();
            return bundle;
        }

        void EnsureCurlGlobalInit() {
            static std::once_flag once;
            std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
        }

        // Aborts a blocking transfer as soon as the owner asks to stop.
        int AbortWhenStopping(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            return static_cast<std::atomic<bool> const*>(clientp)->load() ? 1 : 0;
        }
    }

    CURL* NewEasyHandle(std::atomic<bool> const& stopping) {
        EnsureCurlGlobalInit();
        CURL* curl = curl_easy_init();
        if (!curl)
            throw std::runtime_error("curl_easy_init failed");

        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &AbortWhenStopping);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stopping);

        auto const& ca = SystemCaBundle();
        if (!ca.empty()) {
            curl_blob blob{const_cast<char*>(ca.data()), ca.size(), CURL_BLOB_NOCOPY};
            curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
        } else {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        }
        return curl;
    }

    void Check(CURLcode rc, char const* what) {
        if (rc != CURLE_OK)
            throw std::runtime_error(std::string(what) + ": " + curl_easy_strerror(rc));
    }
}
