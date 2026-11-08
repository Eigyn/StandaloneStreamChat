#include "Net/Curl.hpp"
#include "logging.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace StandaloneStreamChat::Net {
    namespace {
        // The static libcurl build has no usable default CA path on Android, so
        // feed it the headset's own trusted roots. Android keeps one PEM per CA;
        // Android 14+ moved the live set into the Conscrypt APEX.
        std::string const& SystemCaBundle() {
            // Network workers can still open connections during static teardown.
            static auto const* bundle = new std::string([] {
                for (auto dir : {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"}) {
                    // Only certificates that were actually read count, so a
                    // directory of unreadable files falls through to the next one.
                    std::string pem;
                    int certificates = 0;
                    std::error_code ec;
                    std::filesystem::directory_iterator it(dir, ec), end;
                    for (; !ec && it != end; it.increment(ec)) {
                        std::ifstream file(it->path(), std::ios::binary);
                        if (!file)
                            continue;
                        std::string cert((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                        if (cert.find("-----BEGIN CERTIFICATE-----") == std::string::npos)
                            continue;
                        pem += cert;
                        pem.push_back('\n');
                        certificates++;
                    }
                    if (certificates > 0) {
                        INFO("Loaded {} system CA certificates from {}", certificates, dir);
                        return pem;
                    }
                }
                ERROR("No system CA certificates readable; using libcurl's default trust store");
                return std::string();
            }());
            return *bundle;
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
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
        auto curl = handle.get();
        if (!curl)
            throw std::runtime_error("curl_easy_init failed");

        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &AbortWhenStopping);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stopping);
        Check(curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https"), "require HTTPS");
        Check(curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https"), "require HTTPS redirects");

        auto const& ca = SystemCaBundle();
        if (!ca.empty()) {
            curl_blob blob{const_cast<char*>(ca.data()), ca.size(), CURL_BLOB_NOCOPY};
            Check(curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob), "load system CA certificates");
        }
        return handle.release();
    }

    void Check(CURLcode rc, char const* what) {
        if (rc != CURLE_OK)
            throw std::runtime_error(std::string(what) + ": " + curl_easy_strerror(rc));
    }
}
