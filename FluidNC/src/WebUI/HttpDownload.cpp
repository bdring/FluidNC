// Copyright (c) 2026 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// File download for FluidNC
// Fetches a file over HTTP(S) and stores it on the SD card (or local FLASH).
//
// Commands:
//   $File/Download=<url> [dest]
//   $GitHub/Get=<owner>/<repo>/<ref>/<path> [dest]
//
// $GitHub/Get builds
//   https://raw.githubusercontent.com/<owner>/<repo>/<ref>/<path>
// and is otherwise identical to $File/Download.  <ref> is a branch, tag, or
// commit SHA.  The target repository must be public.
//
// dest:
//   Optional.  A leading /sd/ or /localfs/ selects the volume; otherwise the
//   file lands on the SD card.  When omitted, the destination is
//   sd/<basename> taken from the last path segment of the URL.
//
// Behavior:
//   - The body is streamed to <dest>.part and renamed to <dest> only after a
//     complete transfer, so an interrupted download never leaves a truncated
//     file in place.
//   - HTTP redirects are followed (GitHub's raw host redirects to its CDN).
//   - HTTPS is used when the URL says so; server certificates are NOT
//     validated (there is no certificate store on the device).
//   - No authentication headers are sent - public URLs only.
//   - Refused while a job is running or the machine is in motion.  Blocks
//     GCode/serial command processing (not stepper motion) for the duration
//     of the transfer.
//
// GCode parameters set after the request:
//   _HTTP_STATUS       - HTTP status code (0 if the connection failed)
//   _HTTP_RESPONSE_LEN - number of bytes written to the file

#include "HttpDownload.h"

#include "../System.h"
#include "../Report.h"
#include "../Module.h"
#include "Driver/heap.h"  // platform_max_free_block()
#include "../Job.h"
#include "../HashFS.h"
#include "../FileStream.h"
#include "../FluidPath.h"
#include "../Parameters.h"

#include "NetSettings.h"  // networkConnected()

#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>

#include <string>
#include <string_view>

namespace WebUI {

    static const char* RAW_GITHUB_PREFIX = "https://raw.githubusercontent.com/";

    static void store_response_params(int status_code, uint32_t bytes_received) {
        set_named_param("_HTTP_STATUS", static_cast<float>(status_code));
        set_named_param("_HTTP_RESPONSE_LEN", static_cast<float>(bytes_received));
    }

    // Trim leading and trailing ASCII whitespace from a string_view.
    static std::string_view trim(std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
            v.remove_prefix(1);
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
            v.remove_suffix(1);
        }
        return v;
    }

    // Split "<first-token> [rest]" - rest is trimmed and may be empty.
    static void split_first(const char* value, std::string& first, std::string& rest) {
        std::string_view v = trim(value ? value : "");
        size_t           sp = v.find_first_of(" \t");
        if (sp == std::string_view::npos) {
            first.assign(v);
            rest.clear();
        } else {
            first.assign(v.substr(0, sp));
            rest.assign(trim(v.substr(sp + 1)));
        }
    }

    // Last path segment of a URL, minus any ?query or #fragment.
    static std::string basename_from_url(const std::string& url) {
        size_t      cut = url.find_first_of("?#");
        std::string u   = (cut == std::string::npos) ? url : url.substr(0, cut);
        // Drop a trailing slash so ".../foo/" still yields "foo".
        while (u.size() > 1 && u.back() == '/') {
            u.pop_back();
        }
        size_t slash = u.find_last_of('/');
        return (slash == std::string::npos) ? u : u.substr(slash + 1);
    }

    static void remove_quietly(const std::string& path) {
        std::error_code ec;
        stdfs::remove(FluidPath { path, SD, ec }, ec);
    }

    Error HttpDownload::download(const std::string& url, std::string dest, Channel& out) {
        store_response_params(0, 0);

        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
            log_error_to(out, "Download: URL must begin with http:// or https://");
            return Error::InvalidValue;
        }
        bool secure_url = url.rfind("https://", 0) == 0;

        if (dest.empty()) {
            std::string base = basename_from_url(url);
            if (base.empty()) {
                log_error_to(out, "Download: cannot derive a filename from the URL; give an explicit destination");
                return Error::InvalidValue;
            }
            dest = "sd/" + base;
        }

        if (!networkConnected()) {
            log_error_to(out, "Download: network not connected");
            return Error::MessageFailed;
        }
        if (Job::active() || inMotionState()) {
            log_error_to(out, "Download: not allowed while a job is running");
            return Error::IdleError;
        }

        std::string partpath = dest + ".part";

        // Open the output file first so a filesystem problem fails before we
        // bother the network.
        FileStream* file = nullptr;
        try {
            file = new FileStream(partpath, "w", SD);
        } catch (const ErrorException& ex) {
            log_error_to(out, "Download: cannot create " << partpath << ": " << errorString(ex.error()));
            return Error::FsFailedCreateFile;
        }

        WiFiClient       plain_client;
        WiFiClientSecure secure_client;
        secure_client.setInsecure();  // no certificate store on the device
        // Note: the mbedtls RX/TX content buffers (~16 KB each) are fixed by
        // the build's sdkconfig on this core - NetworkClientSecure has no
        // runtime setBufferSizes().  A fragmented heap without a ~40 KB
        // contiguous block is what makes connect() fail here; the failure
        // message below reports the largest free block so that is visible.

        HTTPClient http;
        http.setUserAgent("FluidNC");
        http.setConnectTimeout(8000);
        http.setTimeout(15000);  // per-read stall timeout
        http.setReuse(false);
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

        bool begun = secure_url ? http.begin(secure_client, url.c_str()) : http.begin(plain_client, url.c_str());
        if (!begun) {
            delete file;
            remove_quietly(partpath);
            log_error_to(out, "Download: malformed URL");
            return Error::InvalidValue;
        }

        log_info_to(out, "Download: GET " << url.c_str());

        int http_code = http.GET();
        if (http_code != HTTP_CODE_OK) {
            http.end();
            delete file;
            remove_quietly(partpath);
            store_response_params(http_code < 0 ? 0 : http_code, 0);
            if (http_code < 0) {
                log_error_to(out,
                             "Download: connection failed: " << HTTPClient::errorToString(http_code).c_str() << " (free heap "
                                                             << xPortGetFreeHeapSize() << ", largest block "
                                                             << (unsigned)platform_max_free_block() << ")");
            } else {
                log_error_to(out, "Download: server returned HTTP " << http_code);
            }
            return Error::MessageFailed;
        }

        int expected = http.getSize();  // -1 when the response is chunked

        // writeToStream() blocks for the whole transfer with no chance to feed
        // the task watchdog, and a single mbedtls record read can block up to
        // the socket timeout.  Detach the running task from the TWDT for the
        // duration; http.setTimeout() still bounds a stalled connection so this
        // cannot hang indefinitely.
        bool wdt_here = esp_task_wdt_status(nullptr) == ESP_OK;
        if (wdt_here) {
            esp_task_wdt_delete(nullptr);
        }
        int written = http.writeToStream(file);
        if (wdt_here) {
            esp_task_wdt_add(nullptr);
            esp_task_wdt_reset();
        }

        http.end();
        file->flush();
        delete file;  // closes the fd and releases the SD mount reference

        if (written < 0) {
            remove_quietly(partpath);
            store_response_params(http_code, 0);
            log_error_to(out, "Download: transfer failed: " << HTTPClient::errorToString(written).c_str());
            return Error::MessageFailed;
        }
        if (expected >= 0 && written != expected) {
            remove_quietly(partpath);
            store_response_params(http_code, static_cast<uint32_t>(written));
            log_error_to(out, "Download: short transfer (" << written << " of " << expected << " bytes)");
            return Error::MessageFailed;
        }

        // Promote the completed .part file to its final name.
        remove_quietly(dest);
        try {
            FluidPath inPath { partpath, SD };
            FluidPath outPath { dest, SD };
            stdfs::rename(inPath, outPath);
            HashFS::rename_file(inPath, outPath, true);
        } catch (const ErrorException& ex) {
            remove_quietly(partpath);
            store_response_params(http_code, static_cast<uint32_t>(written));
            log_error_to(out, "Download: could not rename to " << dest << ": " << errorString(ex.error()));
            return Error::FsFailedCreateFile;
        } catch (const std::exception& ex) {
            remove_quietly(partpath);
            store_response_params(http_code, static_cast<uint32_t>(written));
            log_error_to(out, "Download: could not rename to " << dest << ": " << ex.what());
            return Error::FsFailedCreateFile;
        }

        store_response_params(http_code, static_cast<uint32_t>(written));
        log_info_to(out, "Download: wrote " << written << " bytes to " << dest);
        return Error::Ok;
    }

    Error HttpDownload::file_download(const char* value, AuthenticationLevel auth_level, Channel& out) {
        std::string url;
        std::string dest;
        split_first(value, url, dest);
        if (url.empty()) {
            log_error_to(out, "Usage: $File/Download=<url> [dest]");
            return Error::InvalidValue;
        }
        return download(url, dest, out);
    }

    Error HttpDownload::github_get(const char* value, AuthenticationLevel auth_level, Channel& out) {
        std::string spec;
        std::string dest;
        split_first(value, spec, dest);

        // Accept both "owner/repo/ref/path..." and a full raw URL.
        if (spec.rfind("http://", 0) == 0 || spec.rfind("https://", 0) == 0) {
            return download(spec, dest, out);
        }

        // Require at least owner/repo/ref/one-path-segment.
        int slashes = 0;
        for (char c : spec) {
            if (c == '/') {
                ++slashes;
            }
        }
        if (slashes < 3) {
            log_error_to(out, "Usage: $GitHub/Get=<owner>/<repo>/<ref>/<path> [dest]");
            return Error::InvalidValue;
        }

        std::string url = std::string(RAW_GITHUB_PREFIX) + spec;
        return download(url, dest, out);
    }

    // Allow only from states where a multi-second blocking transfer is safe.
    static bool download_state_check() {
        if (state_is(State::Cycle) || state_is(State::Homing) || state_is(State::Jog) || state_is(State::Hold) ||
            state_is(State::SafetyDoor) || state_is(State::Sleep)) {
            return true;  // block
        }
        return false;  // allow
    }

    static Error file_download_handler(const char* value, AuthenticationLevel auth_level, Channel& out) {
        return HttpDownload::file_download(value, auth_level, out);
    }
    static Error github_get_handler(const char* value, AuthenticationLevel auth_level, Channel& out) {
        return HttpDownload::github_get(value, auth_level, out);
    }

    class HttpDownloadModule : public Module {
    public:
        explicit HttpDownloadModule(const char* name) : Module(name) {}

        void init() override {
            // needs_protocol_context = false, drains_buffer = false: this runs on
            // the polling task and touches no planner or motion state.
            new UserCommand("FDL", "File/Download", file_download_handler, download_state_check, WG, false, false);
            new UserCommand("GHG", "GitHub/Get", github_get_handler, download_state_check, WG, false, false);
            log_info("File download command registered");
        }
    };

    ModuleFactory::InstanceBuilder<HttpDownloadModule> http_download_module __attribute__((init_priority(110)))
    ("http_download", true);

}  // namespace WebUI
