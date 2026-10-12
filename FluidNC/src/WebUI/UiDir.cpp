// Copyright (c) 2026 Mitch Bradley. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Serves several WebUIs at once, each from a subdirectory of HTTP/UIDir at
// /ui/<name>/.  See UiDir.h.

#include "Platform.h"  // HOSTED
#include "UiDir.h"
#include "WebUIServer.h"
#include "WSChannel.h"
#include "FluidPath.h"
#include "FileStream.h"
#include "HashFS.h"
#include "System.h"  // inMotionState()
#include "Logging.h"

#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <set>
#include <vector>

namespace WebUI {
    static StringSetting* http_ui_dir;
    static StringSetting* http_default_ui;

    void UiDir::init() {
        // A directory, on any filesystem, whose subdirectories each hold a
        // WebUI, served at /ui/<subdirectory>/ - e.g. builds too big for FLASH
        http_ui_dir = new StringSetting("HTTP WebUI directory", WEBSET, WA, NULL, "HTTP/UIDir", "/sd/ui", 0, 0);
        // The subdirectory of HTTP/UIDir that / redirects to, if it exists
        http_default_ui = new StringSetting("HTTP default WebUI", WEBSET, WA, NULL, "HTTP/DefaultUI", "", 0, 0);
    }

    // Canonical form of HTTP/UIDir, or empty if it is not set.  Looked up
    // per request so that a change takes effect without a restart.
    static std::string uiDir() {
        std::string_view value = http_ui_dir->get();
        std::string      path;
        if (!value.empty()) {
            path = FluidPath::canonPath(value, LocalFS);
        }
        HashFS::set_ui_dir(path);
        return path;
    }

    // WebUI names become URL path components and cookie paths, so they are
    // restricted to characters that need no escaping in either.
    static bool validUiName(std::string_view name) {
        if (name.empty() || name == "." || name == "..") {
            return false;
        }
        for (char c : name) {
            if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') {
                return false;
            }
        }
        return true;
    }

    // The WebUI in the LocalFS root - the one / has always served - is also
    // /ui/localfs/, so it stays reachable when HTTP/DefaultUI sends /
    // elsewhere.  It hides any subdirectory of HTTP/UIDir with that name.
    static const std::string localfsUi = "localfs";

    // The directory of the named WebUI, or empty if there is none
    static std::string uiPath(const std::string& name) {
        if (name == localfsUi) {
            return FluidPath::canonPath("", LocalFS);
        }
        auto dir = uiDir();
        return dir.empty() ? "" : dir + "/" + name;
    }

    // A request URL under /ui, split into the WebUI name and the path
    // beneath that WebUI's directory.
    struct UiUrl {
        std::string name;   // Empty for /ui or /ui/
        std::string tail;   // Empty for /ui/<name> or /ui/<name>/
        bool        slash;  // /ui/<name>/ rather than /ui/<name>
    };

    // False if the URL is malformed.  Empty, "." and ".." components are
    // rejected, so the path cannot escape HTTP/UIDir.
    static bool splitUiUrl(std::string_view url, UiUrl& u) {
        u = {};
        url.remove_prefix(3);  // "/ui"
        if (url.empty() || url == "/") {
            return true;
        }
        url.remove_prefix(1);
        bool first = true;
        while (!url.empty()) {
            auto             pos  = url.find('/');
            std::string_view comp = url.substr(0, pos);
            if (comp.empty() || comp == "." || comp == "..") {
                return false;
            }
            if (first) {
                if (!validUiName(comp)) {
                    return false;
                }
                u.name = comp;
                first  = false;
            } else {
                if (!u.tail.empty()) {
                    u.tail += '/';
                }
                u.tail += comp;
            }
            if (pos == std::string_view::npos) {
                break;
            }
            url.remove_prefix(pos + 1);
            if (url.empty() && u.tail.empty()) {
                u.slash = true;
            }
        }
        return true;
    }

    // True for /ui/<name>/, the URL of a WebUI page, which is also the
    // URL of its WebSocket - just as / is for a WebUI served at the root.
    static bool isUiPageUrl(std::string_view url) {
        UiUrl u;
        return url.rfind("/ui/", 0) == 0 && splitUiUrl(url, u) && !u.name.empty() && u.slash;
    }

    // Accepts WebSocket connections at /ui/<name>/ and hands them to the
    // main WebSocket server.  The browser sends that WebUI's uiSession
    // cookie with them, which isolates its channels from other WebUIs.
    class UiWebSocketHandler : public AsyncWebHandler {
        AsyncWebSocket* _ws;

    public:
        explicit UiWebSocketHandler(AsyncWebSocket* ws) : _ws(ws) {}
        bool canHandle(AsyncWebServerRequest* request) const override {
            return request->isWebSocketUpgrade() && isUiPageUrl(request->url().c_str());
        }
        void handleRequest(AsyncWebServerRequest* request) override { _ws->handleRequest(request); }
    };

    void UiDir::addHandlers(AsyncWebServer* server, AsyncWebSocket* ws) {
        server->addHandler(new UiWebSocketHandler(ws));
        server->on("/ui", HTTP_ANY, handle, nullptr, handleBody);
    }

    void UiDir::announce() {
        uiDir();
    }

    bool UiDir::redirectToDefault(AsyncWebServerRequest* request) {
        std::string name(http_default_ui->get());
        if (!indexExists(name)) {
            return false;
        }
        request->redirect(("/ui/" + name + "/").c_str());
        return true;
    }

    static bool blockedInMotion() {
        return http_block_during_motion->get() && inMotionState();
    }

    // True if the named WebUI has an index.html[.gz].  During
    // motion only the hash cache is consulted, so the files are not read.
    bool UiDir::indexExists(const std::string& name) {
        if (!validUiName(name)) {
            return false;
        }
        auto base = uiPath(name);
        if (base.empty()) {
            return false;
        }
        std::string index = base + "/index.html";
        if (blockedInMotion()) {
            return !HashFS::hash(index, true).empty() || !HashFS::hash(index + ".gz", true).empty();
        }
        std::error_code ec;
        FluidPath       fpath { index, LocalFS, ec };  // Keeps an SD volume mounted
        if (ec) {
            return false;
        }
        return !HashFS::hash(fpath).empty() || !HashFS::hash(index + ".gz").empty();
    }

    static std::string html_escape(std::string_view s) {
        std::string out;
        for (char c : s) {
            switch (c) {
                case '&':
                    out += "&amp;";
                    break;
                case '<':
                    out += "&lt;";
                    break;
                case '>':
                    out += "&gt;";
                    break;
                case '"':
                    out += "&quot;";
                    break;
                default:
                    out += c;
            }
        }
        return out;
    }

    // A page listing the WebUIs in HTTP/UIDir
    void UiDir::handleList(AsyncWebServerRequest* request, const std::string& dir) {
        if (blockedInMotion()) {
            WebUI_Server::handleReloadBlocked(request);
            return;
        }
        std::string defaultUi(http_default_ui->get());
        std::string page = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                           "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                           "<title>WebUIs</title></head><body><h3>WebUIs" +
                           (dir.empty() ? std::string() : " in " + html_escape(dir)) + "</h3><ul>";
        int count = 0;
        if (indexExists(localfsUi)) {
            page += "<li><a href='/ui/" + localfsUi + "/'>" + localfsUi + "</a> (the local file system)";
            if (localfsUi == defaultUi) {
                page += " (default)";
            }
            page += "</li>";
            ++count;
        }
        std::error_code ec;
        FluidPath       fpath { dir, LocalFS, ec };
        if (!dir.empty() && !ec) {
            std::vector<std::string> names;
            // The error_code forms throughout: a card pulled mid-listing
            // must not throw out of this async_tcp callback.
            for (stdfs::directory_iterator it { fpath, ec }, end; !ec && it != end; it.increment(ec)) {
                std::error_code fec;
                auto            name = it->path().filename().string();
                if (it->is_directory(fec) && validUiName(name) && name != localfsUi &&
                    (stdfs::exists(it->path() / "index.html", fec) || stdfs::exists(it->path() / "index.html.gz", fec))) {
                    names.push_back(name);
                }
            }
            std::sort(names.begin(), names.end());
            for (auto const& name : names) {
                page += "<li><a href='/ui/" + name + "/'>" + name + "</a>";
                if (name == defaultUi) {
                    page += " (default)";
                }
                page += "</li>";
                ++count;
            }
        }
        if (!count) {
            page += "<li>None found</li>";
        }
        page += "</ul><a href='/?forcefallback=yes'>WebUI Uploader</a></body></html>";
        request->send(200, "text/html", page.c_str());
    }

    // Requests under /ui:
    //   /ui/                      a list of the WebUIs in HTTP/UIDir
    //   /ui/<name>/               that WebUI's index.html, and its WebSocket
    //   /ui/<name>/command[_silent]  commands in that WebUI's session
    //   /ui/<name>/<path>         GET or PUT a file in that WebUI's directory
    // A WebUI that uses relative URLs thus gets its own session and keeps
    // its state files in its own directory.  One that uses absolute URLs
    // still works, sharing the browser-wide session and LocalFS state files.
    void UiDir::handle(AsyncWebServerRequest* request) {
        std::string url(request->url().c_str());
        UiUrl       u;
        if (!splitUiUrl(url, u)) {
            WebUI_Server::send404Page(request);
            return;
        }
        if (u.name.empty()) {
            handleList(request, uiDir());
            return;
        }
        auto base = uiPath(u.name);
        if (base.empty()) {
            WebUI_Server::send404Page(request);
            return;
        }
        if (u.tail.empty() && !u.slash) {
            // Relative URLs in the page must resolve beneath its directory
            request->redirect((url + "/").c_str());
            return;
        }
        if (u.tail == "command") {
            WebUI_Server::handle_web_command(request);
            return;
        }
        if (u.tail == "command_silent") {
            WebUI_Server::handle_web_command_silent(request);
            return;
        }

        std::string path = base + "/" + (u.tail.empty() ? "index.html" : u.tail);

        if (request->method() == HTTP_PUT && u.tail.empty()) {
            request->send(405);  // PUT needs a file name; /ui/<name>/ is the page
            return;
        }
        if (request->method() == HTTP_PUT) {
            handlePut(request, path);
            return;
        }
        if (request->method() != HTTP_GET && request->method() != HTTP_HEAD) {
            request->send(405);
            return;
        }

        // One file at a time - see serveWaiting().  Until this request's
        // connection closes, later ones wait their turn.
        if (serving) {
            waiting.push_back(request->pause());
            return;
        }
        serving   = true;
        auto done = [] {
            serving = false;
            serveWaiting();
        };

        if (u.tail.empty()) {
            // A page load.  Close the channels of the previous instance of
            // this page: its uiSession, which is scoped to this WebUI's
            // directory.  Only on the very first load, before it has a
            // uiSession, fall back to the browser-wide sessionId (which
            // does close a root WebUI's channels in another tab, once).
            // Closing sessionId on every load would disconnect that tab each
            // time.  A WebUI that uses absolute URLs keeps using sessionId;
            // its stale channel is closed when its new WebSocket connects.
            auto session = getCookie(request, "uiSession");
            if (session.empty()) {
                session = getCookie(request, "sessionId");
            }
            if (!session.empty()) {
                WSChannels::closeSessionChannels(session);
            }
            if (WebUI_Server::myStreamFile(request, path.c_str(), false, true, u.name.c_str(), done)) {
                return;
            }
        } else if (WebUI_Server::myStreamFile(request, path.c_str(), false, false, nullptr, done)) {
            return;
        }
        WebUI_Server::send404Page(request);
    }

    // SD is mounted with room for only two open files, and a job or a PUT may
    // be using one.  A browser loading a WebUI asks for several files at once,
    // so they are served one at a time: while one is being sent, later
    // requests are paused, and resumed in order when its connection closes.
    // The number waiting is bounded by the TCP connections.  Every handler and
    // disconnect callback runs on the async_tcp task, so this needs no lock.
    bool                                 UiDir::serving = false;
    std::deque<AsyncWebServerRequestPtr> UiDir::waiting;

    void UiDir::serveWaiting() {
        while (!serving && !waiting.empty()) {
            auto next = waiting.front().lock();  // Empty if that client has gone
            waiting.pop_front();
            if (next) {
                handle(next.get());
            }
        }
    }

    // State for a PUT under /ui, which is written to a temporary file that
    // replaces the target only when complete.  The request frees it.
    // Plain data only: the request releases it with free().
    struct UiPut {
        FileStream* file;
        uint16_t    status;   // Nonzero if the PUT failed
        uint32_t    seq;      // Makes this PUT's temporary file name unique
        size_t      written;  // Bytes accepted by FileStream::write()
    };

    static std::string uiPutPath(AsyncWebServerRequest* request) {
        std::string url(request->url().c_str());
        UiUrl       u;
        if (!splitUiUrl(url, u) || u.name.empty() || u.tail.empty() || u.tail == "command" || u.tail == "command_silent") {
            return "";
        }
        auto base = uiPath(u.name);
        return base.empty() ? "" : base + "/" + u.tail;
    }

    // Each PUT writes its own temporary file, so overlapping PUTs to the
    // same target (e.g. a client retrying after a timeout) don't share one.
    static std::atomic<uint32_t> uiPutSeq { 0 };

    static std::string partPath(const std::string& path, uint32_t seq) {
        return path + ".part" + std::to_string(seq);
    }

    // Temporary files of PUTs in progress.  All PUT handling runs on the
    // async_tcp task, so this needs no lock.
    static std::set<std::string> activeParts;

    // A reboot, power loss or failed rename mid-PUT can leave a target's
    // "<name>.part<n>" files behind.  Remove the ones no PUT is using when a
    // new PUT of that target starts (never during motion: callers have
    // already checked).
    static void removeStaleParts(const FluidPath& target) {
        std::error_code ec;
        std::string     stem = target.filename().string() + ".part";
        for (stdfs::directory_iterator it { target.parent_path(), ec }, end; !ec && it != end; it.increment(ec)) {
            std::string name = it->path().filename().string();
            if (name.rfind(stem, 0) == 0 && name.find_first_not_of("0123456789", stem.length()) == std::string::npos &&
                name.length() > stem.length() && !activeParts.count(it->path().string())) {
                std::error_code rec;
                stdfs::remove(it->path(), rec);
            }
        }
    }

    // Closes and deletes a PUT's temporary file
    static void discardPart(UiPut* put) {
        FluidPath fpath = put->file->fpath();
        delete put->file;
        put->file = nullptr;
        activeParts.erase(fpath.string());
        std::error_code ec;
        stdfs::remove(fpath, ec);
    }

    // A PUT under /ui/<name>/ creates the WebUI's directory, and any
    // subdirectories in the path, so that a new WebUI can be installed with
    // a single request.  uiPutPath() has already confined the path to
    // HTTP/UIDir, so only directories there can be created.
    static bool makeParentDirs(const FluidPath& fpath) {
        std::error_code ec;
        stdfs::create_directories(fpath.parent_path(), ec);
        if (ec) {
            log_debug("Cannot create " << fpath.parent_path() << ": " << ec.message());
            return false;
        }
        return true;
    }

    void UiDir::handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (request->method() != HTTP_PUT) {
            return;
        }
        auto put = static_cast<UiPut*>(request->_tempObject);
        if (!put) {
            put = static_cast<UiPut*>(calloc(1, sizeof(UiPut)));
            if (!put) {
                return;  // handlePut() reports the failure
            }
            request->_tempObject = put;

            auto path = uiPutPath(request);
            if (path.empty()) {
                put->status = 404;
                return;
            }
            // Writing to SD while a job reads from it could starve the job
            if (blockedInMotion()) {
                put->status = 503;
                return;
            }
            put->seq = ++uiPutSeq;
            try {
                FluidPath fpath { partPath(path, put->seq), LocalFS };
                if (total) {
                    std::error_code ec;
                    auto            avail = stdfs::space(fpath, ec).available;
                    if (!ec && total + 4096 > avail) {
                        put->status = 507;  // Insufficient storage
                        return;
                    }
                }
                if (!makeParentDirs(fpath)) {
                    put->status = 500;
                    return;
                }
                removeStaleParts(FluidPath { path, LocalFS });
                put->file = new FileStream(fpath, "w");
                activeParts.insert(put->file->fpath().string());
            } catch (...) {
                put->status = 500;
                return;
            }
            // Clean up if the client disconnects before the PUT completes
            request->onDisconnect([put]() {
                if (put->file) {
                    discardPart(put);
                }
            });
        }
        if (!put->file) {
            return;
        }
        // Motion may start during a long upload; stop writing then
        uint16_t failure = blockedInMotion() ? 503 : put->file->write(data, len) != len ? 507 : 0;
        if (failure) {
            discardPart(put);
            put->status = failure;
            return;
        }
        put->written += len;
    }

    void UiDir::handlePut(AsyncWebServerRequest* request, const std::string& path) {
        auto put = static_cast<UiPut*>(request->_tempObject);
        if (put && put->status) {
            request->send(put->status);
            return;
        }
        if (blockedInMotion()) {
            request->send(503);
            return;
        }
        size_t length = request->contentLength();
        // A chunked body has no Content-Length but is still a body
        bool hasBody = length || request->hasHeader("Transfer-Encoding");
        if (hasBody && !(put && put->file)) {
            // A body arrived but never reached handleBody(): the server
            // parsed it as form data (application/x-www-form-urlencoded, or
            // key=value text/plain), or the PUT state could not be
            // allocated.  Never treat that as an empty file.
            const String& type = request->contentType();
            bool form = type.equalsIgnoreCase("application/x-www-form-urlencoded") || type.startsWith("text/plain");
            request->send(form ? 415 : 500);
            return;
        }
        try {
            FluidPath   fpath { path, LocalFS };
            std::string partName = partPath(path, put ? put->seq : ++uiPutSeq);
            FluidPath   part { partName, LocalFS };
            if (put && put->file) {
                activeParts.erase(put->file->fpath().string());
                delete put->file;  // Closes the file, flushing its buffer
                put->file = nullptr;
            } else {
                // A PUT with no body creates or empties the file
                if (!makeParentDirs(part)) {
                    request->send(500);
                    return;
                }
                removeStaleParts(fpath);
                FileStream(part, "w");
            }
            std::error_code ec;
            // The final flush happens on close, where a failure goes
            // unreported -- so check what actually reached the file.
            size_t expected = put ? put->written : 0;
            size_t actual   = stdfs::file_size(part, ec);
            if (ec || actual != expected || (length && expected != length)) {
                log_debug("PUT " << path << ": wrote " << actual << " of " << length << " bytes");
                stdfs::remove(part, ec);
                request->send(507);
                return;
            }
            if (stdfs::is_directory(fpath, ec)) {
                stdfs::remove(part, ec);
                request->send(409);
                return;
            }
            bool existed = stdfs::exists(fpath, ec);
            // LittleFS renames onto an existing file in one step.  FAT
            // refuses to, so only then remove the target first.  Once it is
            // gone, the temporary file is the only copy: keep it on failure.
            stdfs::rename(part, fpath, ec);
            if (ec && existed) {
                stdfs::remove(fpath, ec);
                if (!ec) {
                    stdfs::rename(part, fpath, ec);
                    if (ec) {
                        log_error("PUT " << path << ": cannot rename " << partName << " into place: " << ec.message());
                        request->send(500);
                        return;
                    }
                }
            }
            if (ec) {
                log_debug("Cannot rename " << part << " to " << fpath << ": " << ec.message());
                stdfs::remove(part, ec);
                request->send(500);
                return;
            }
            HashFS::rehash_file(fpath, false);
            request->send(existed ? 204 : 201);
        } catch (...) {
            request->send(500);
        }
    }
}
