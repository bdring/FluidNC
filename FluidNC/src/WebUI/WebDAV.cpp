#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <AsyncTCP.h>
#include "ESP32SHA1.h"

#include <chrono>
#include <ctime>
// #include <format>
#include "string_util.h"

#include "WebDAV.h"
#include "FileStream.h"
#include "HashFS.h"

#include "Mime.h"

using namespace asyncsrv;

WebDAV::WebDAV(const std::string_view url, const Volume& volume, bool reject_metadata) :
    _url(url), _volume(volume), _reject_metadata(reject_metadata) {}

bool WebDAV::canHandle(AsyncWebServerRequest* request) const {
    if (request->url().startsWith(_url.c_str())) {
        return true;
    }
    return false;
}

static const char* rootname = "/";
static const char* slashstr = "/";
static const char  slash    = '/';

// Mac command to prevent .DS_Store files:
//  defaults write com.apple.desktopservices DSDontWriteNetworkStores -bool TRUE
// Mac metadata files:
//  .metadata_never_index_unless_rootfs
//  .metadata_never_index
//  .Spotlight-V100
//  .DS_Store
//  ._* (metadata for the file *)
//  .hidden

bool WebDAV::isMacOS(AsyncWebServerRequest* request) {
    return request->hasHeader("User-Agent") && request->getHeader("User-Agent")->value().indexOf("Darwin") != -1;
}

// We can reject attempts to create or access MacOS metadata files, which
// waste precious space on FLASH filesystems and can waste time on SD filesystems,
// especially when MacOS tries to access a lot of them.
bool WebDAV::rejectMacMetadata(AsyncWebServerRequest* request, stdfs::path& path) {
    return _reject_metadata && isMacOS(request) &&
           (path.filename() == ".DS_Store" || string_util::starts_with_ignore_case(path.filename().string(), "._"));
}

void WebDAV::handleRequest(AsyncWebServerRequest* request) {
    // Check if handleBody already did the work
    auto state = static_cast<RequestState*>(request->_tempObject);
    if (state) {
        if (state->outFile) {
            // The file was already opened and written in handleBody so
            // we are done.  We will handle PUT without body data below.
            delete state->outFile;
            request->send(201);  // Created
        }
        // If state was non-null but state->outFile was null, handleBody
        // rejected the operation and already sent the response code.

        delete state;
        request->_tempObject = nullptr;
        return;
    }

    // parse the url to a proper path
    stdfs::path path { request->url().substring(_url.length()).c_str() };
    DavResource resource = DavResource::NONE;

    if (rejectMacMetadata(request, path)) {
        // Reject MacOS metadata filenames right away.  We do not want
        // to clutter the FLASH filesystem with them, nor do we want
        // to read the filesystem, lest we interfere with motion.
        return request->send((request->method() == HTTP_PROPFIND || request->method() == HTTP_GET) ? 404 : 403);
    }
    //    if (request->method() != HTTP_PROPFIND && request->method() != HTTP_LOCK && request->method() != HTTP_UNLOCK) {
    if (request->method() == HTTP_LOCK) {
        return handleLock(path, request);
    }
    if (request->method() == HTTP_UNLOCK) {
        return handleUnlock(path, request);
    }
    if (request->method() == HTTP_HEAD || request->method() == HTTP_OPTIONS) {
        return handleHead(resource, request);
    }

    std::error_code ec;

    log_verbose("fpath with method " << request->methodToString() << " on " << path);

    FluidPath fpath { path.string(), _volume, ec };
    if (ec) {
        resource = DavResource::NONE;
    } else if (stdfs::is_regular_file(fpath)) {
        resource = DavResource::FILE;
    } else if (stdfs::is_directory(fpath)) {
        resource = DavResource::DIR;
    }

    if (request->method() == HTTP_MKCOL) {
        return handleMkcol(fpath, resource, request);
    }
    if (request->method() == HTTP_PUT) {
        log_verbose("PUT empty " << fpath);

        try {
            // Create the file and close it immediately.
            // A later PUT request with a body might populate it.
            // MacOS tends to create an empty file first, then
            // lock it and write to it.
            FileStream file(fpath, "w", LocalFS);
        } catch (const Error err) {
            log_debug(fpath << " cannot be opened");
            return request->send(403);
        }
        return request->send(201);
    }

    if (request->method() == HTTP_GET) {
        return handleGet(fpath, resource, request);
    }

    // If we are not creating the resource it must already exist
    if (resource == DavResource::NONE) {
        return handleNotFound(request);
    }

    if (request->method() == HTTP_PROPFIND || request->method() == HTTP_PROPPATCH) {
        return handlePropfind(fpath, resource, request);
    }
    if (request->method() == HTTP_MOVE) {
        return handleMove(fpath, resource, request);
    }
    if (request->method() == HTTP_DELETE) {
        return handleDelete(fpath, resource, request);
    }

    return handleNotFound(request);
}

void WebDAV::handleBody(AsyncWebServerRequest* request, unsigned char* data, size_t len, size_t index, size_t total) {
    // The other requests with a body are LOCK and PROPFIND, where the body data is the XML
    // schema for their replies.  For now, we just ignore that data and hardcode the reply
    // schema.  It might be useful to decode the schema to, for example, omit some reply fields,
    // but that doesn't appear to be necessary at the moment.
    if (request->method() == HTTP_PUT) {
        auto state = static_cast<RequestState*>(request->_tempObject);

        if (index == 0 && state == nullptr) {
            state                = new RequestState { nullptr };
            request->_tempObject = static_cast<void*>(state);
        }
        if (state->outFile == nullptr) {
            // parse the url to a proper path
            stdfs::path path { request->url().substring(_url.length()).c_str() };

            if (rejectMacMetadata(request, path)) {
                return request->send(403);
            }

            std::error_code ec;
            FluidPath       fpath(path.string(), _volume, ec);

            if (ec) {
                return request->send(403);
            }

            if (total) {
                auto avail = stdfs::space(fpath, ec).available;
                avail -= 4096;  // Reserve a block for overhead
                if (total > avail) {
                    log_debug("PUT " << total << " bytes will not fit in available space (" << avail << ")\n");
                    request->send(507);  // Insufficient storage
                    return;
                }
            }

            if (stdfs::is_directory(fpath)) {
                log_error("Cannot PUT to a directory");
                return request->send(403);
            }

            // If we ever handle LOCK properly, we might need
            // to open for appending instead of recreating the
            // file if it already exists.
            try {
                state->outFile = new FileStream(fpath, "w", LocalFS);
            } catch (const Error err) {
                log_debug(fpath << " cannot be opened");
                return request->send(500);
            }
        }
        if (state && state->outFile) {
            auto actual = state->outFile->write(data, len);
            if (actual != len) {
                log_debug("WebDAV write failed.  Deleting file.");
                delete state->outFile;  // Closes file
                state->outFile = nullptr;

                stdfs::path     path { request->url().substring(_url.length()).c_str() };
                std::error_code ec;
                FluidPath       fpath(path.string(), _volume, ec);

                if (!ec) {
                    stdfs::remove(fpath, ec);
                }

                return request->send(507);  // Insufficient storage
            }
        }
    }
}

void WebDAV::handleUpload(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
    handleBody(request, data, len, index, 0);
}

bool WebDAV::acceptsEncoding(AsyncWebServerRequest* request, const char* encoding) {
    if (request->hasHeader("Accept-Encoding")) {
        auto encodings = std::string(request->getHeader("Accept-Encoding")->value().c_str());
        return encodings.find(encoding) != std::string::npos;
    }
    return false;
}

bool WebDAV::acceptsType(AsyncWebServerRequest* request, const char* type) {
    if (request->hasHeader(T_ACCEPT)) {
        auto types = std::string(request->getHeader(T_ACCEPT)->value().c_str());
        return types.find(type) != std::string::npos;
    }
    return false;
}

void WebDAV::handlePropfind(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    // check depth header
    auto depth = DavDepth::NONE;

    bool noroot = false;

    const AsyncWebHeader* depthHeader = request->getHeader("Depth");
    if (depthHeader) {
        if (depthHeader->value().equals("1")) {
            depth = DavDepth::CHILD;
        } else if (depthHeader->value().equals("1,noroot")) {
            depth  = DavDepth::CHILD;
            noroot = true;
        } else if (depthHeader->value().equals("infinity")) {
            depth = DavDepth::ALL;
        } else if (depthHeader->value().equals("infinity,noroot")) {
            depth  = DavDepth::ALL;
            noroot = true;
        }
    }

    JSONencoder* j = nullptr;

    bool wantJSON = acceptsType(request, T_application_json);

    AsyncResponseStream* response = request->beginResponseStream(wantJSON ? T_application_json : "application/xml");
    response->setCode(207);

    if (wantJSON) {
        j = new JSONencoder([response](const char* s) { response->print(s); });
    }

    if (j) {
        j->begin();
    } else {
        response->print("<?xml version=\"1.0\"?>");
        response->print("<d:multistatus xmlns:d=\"DAV:\">");
    }
    //    if (!is_dir || !noroot) {
    sendPropResponse(response, (int)depth, fpath, j);
    //    }

    if (j) {
        j->end();
        delete j;
    } else {
        response->print("</d:multistatus>");
    }

    return request->send(response);
}

void WebDAV::handleGet(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    FileStream* file = nullptr;

    bool isGzip = false;
    if (resource == DavResource::NONE) {
        if (acceptsEncoding(request, T_gzip)) {
            stdfs::path gzpath(fpath);
            gzpath += ".gz";
            try {
                file   = new FileStream(gzpath, "r", LocalFS);
                isGzip = true;
            } catch (const Error err) {}
        }
    } else {
        try {
            file = new FileStream(fpath, "r", LocalFS);
        } catch (const Error err) {}
    }

    if (!file) {
        AsyncWebServerResponse* response = request->beginResponse(404);
        request->send(response);
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(
        getContentType(fpath.c_str()), file->size(), [file, request](uint8_t* buffer, size_t maxLen, size_t filled) mutable -> size_t {
            if (!file) {
                request->client()->close();
                return 0;  //RESPONSE_TRY_AGAIN; // This only works for ChunkedResponse
            }
            int actual = 0;
            if (maxLen) {
                actual = file->read(buffer, maxLen);  // return 0 even when no bytes were loaded
            }
            if (actual == 0) {
                file = nullptr;
            }
            return actual;
        });

    if (isGzip) {
        response->addHeader(T_Content_Encoding, T_gzip, false);
    }

    request->onDisconnect([request, file]() { delete file; });

    //  response->addHeader("Content-Disposition", "attachment");

    request->send(response);
}

void WebDAV::handlePut(
    const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request, unsigned char* data, size_t len, size_t index, size_t total) {
    if (resource == DavResource::DIR) {
        return;
    }

    try {
        FileStream file(fpath, index ? "a" : "w", LocalFS);
        file.write(data, len);
        file.flush();
    } catch (const Error err) { log_debug(fpath << " cannot be opened"); }
}

void WebDAV::handleLock(const stdfs::path& path, AsyncWebServerRequest* request) {
    std::string lockroot("http://");
    lockroot += request->host().c_str();
    lockroot += replace_fs_name(path);

    AsyncResponseStream* response = request->beginResponseStream("application/xml; charset=utf-8");
    response->setCode(200);
    response->addHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");

    response->print("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    response->print("<D:prop xmlns:D=\"DAV:\">");
    response->print("<D:lockdiscovery>");
    response->print("<D:activelock>");
    response->print("<D:locktype><write/></D:locktype>");
    response->print("<D:lockscope><exclusive/></D:lockscope>");
    response->print("<D:locktoken><D:href>urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9</D:href></D:locktoken>");
    response->printf("<D:lockroot><D:href>%s</D:href></D:lockroot>", lockroot.c_str());
    response->print("<D:depth>infinity</D:depth>");
    response->printf("<D:owner><a:href xmlns:a=\"DAV:\">%s</a:href></D:owner>", "todo");
    response->print("<D:timeout>Second-3600</D:timeout>");
    response->print("</D:activelock>");
    response->print("</D:lockdiscovery>");
    response->print("</D:prop>");

    request->send(response);
}

void WebDAV::handleUnlock(const stdfs::path& path, AsyncWebServerRequest* request) {
    request->send(204);  // No Content
}

void WebDAV::handleMkcol(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    // does the file/dir already exist?
    int status;
    if (resource != DavResource::NONE) {
        // Already exists
        // I think there is an "Overwrite: {T,F}" header; we should handle it
        status = 405;
    } else {
        // create dir and send response
        std::error_code ec;
        if (stdfs::create_directory(fpath, ec)) {
            status = 201;
        } else {
            status = 405;
        }
    }
    request->send(status);
}

void WebDAV::handleMove(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    const AsyncWebHeader* destinationHeader = request->getHeader("Destination");
    if (!destinationHeader || destinationHeader->value().isEmpty()) {
        return handleNotFound(request);
    }

    std::string newname = std::string(urlToUri(destinationHeader->value().c_str()));

    AsyncWebServerResponse* response;

    // Should handle "Overwrite: {T,F}" header
    std::error_code ec;
    FluidPath       newpath(newname, _volume, ec);
    if (ec) {
        response = request->beginResponse(500, "text/plain", "Unable to move");
    } else {
        std::filesystem::rename(fpath, newpath, ec);
        if (ec) {
            response = request->beginResponse(500, "text/plain", "Unable to move");
        } else {
            //        HashFS::rename_file(fpath, newname);
            response = request->beginResponse(201);
            // XXX webdav go server adds text/plain "Created" response
        }
    }
    request->send(response);
}

void WebDAV::handleDelete(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    // delete file or dir
    bool            okay;
    std::error_code ec;
    if (resource == DavResource::FILE) {
        okay = stdfs::remove(fpath, ec);
    } else {
        // remove_all returns the number of items that were deleted
        okay = stdfs::remove_all(fpath, ec) != 0;
    }

    return request->send(okay ? 204 : 413);
}

void WebDAV::handleHead(DavResource resource, AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200);
    response->addHeader("Dav", "1,2");
    response->addHeader("Ms-Author-Via", "DAV");
    response->addHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
    request->send(response);
}

void WebDAV::handleNotFound(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(404);
    request->send(response);
}

std::string WebDAV::urlToUri(std::string url) {
    if (string_util::starts_with_ignore_case(url, "http://")) {
        url.erase(0, 7);
    } else if (string_util::starts_with_ignore_case(url, "https://")) {
        url.erase(0, 8);
    }
    // Now remove the hostname.

    auto pos = url.find('/');
    if (pos != std::string::npos) {
        url.erase(0, pos);
    }
    return url.substr(_url.length());
}

bool WebDAV::is_fs_root(const stdfs::path& p) {
    if (p.empty()) {
        return false;
    }
    auto it = std::next(p.begin(), 2);
    if (it == p.end()) {
        return true;
    }

    return (*it).empty();
}

// Construct a new path with the first component of the incoming URI,
// followed by the remainder of the argument path.  This is necessary
// because the filesystem component of the URI path has been mapped
// to a FluidNC filesystem name and needs to be mapped back.
stdfs::path WebDAV::replace_fs_name(const stdfs::path& p) {
    if (p.empty()) {
        return p;
    }

    stdfs::path new_path;

    stdfs::path uri_path(_url);
    auto        uit = uri_path.begin();
    new_path /= *uit;
    ++uit;
    new_path /= *uit;

    // Add the rest of the components
    for (auto it = std::next(p.begin(), 2); it != p.end(); ++it) {
        new_path /= *it;
    }

    return new_path;
}

void WebDAV::sendXMLResponse(
    AsyncResponseStream* response, bool is_dir, const std::string& name, const std::string& tag, const std::string& time, size_t size) {
    response->printf("<d:response>");
    response->printf("<d:href>%s</d:href>", name.c_str());
    response->printf("<d:propstat>");
    response->printf("<d:prop>");
    if (is_dir) {
        response->printf("<d:resourcetype><d:collection/></d:resourcetype>");
    } else {
        // response->printf("<d:getetag>%s</d:getetag>", tag.c_str());
        response->printf("<d:getlastmodified>%s</d:getlastmodified>", time.c_str());
        response->printf("<d:resourcetype/>");
        response->printf("<d:getcontentlength>%d</d:getcontentlength>", size);
        response->printf("<d:getcontenttype>%s</d:getcontenttype>", getContentType(name));
    }
    response->printf("</d:prop>");
    response->printf("<d:status>HTTP/1.1 200 OK</d:status>");
    response->printf("</d:propstat>");
    response->printf("</d:response>");
}
void WebDAV::sendPropResponse(AsyncResponseStream* response, int level, const stdfs::path& fpath, JSONencoder* j) {
    auto   ftime  = stdfs::last_write_time(fpath);
    bool   is_dir = stdfs::is_directory(fpath);
    size_t size   = is_dir ? -1 : stdfs::file_size(fpath);

    // last modified
#if __cpp_lib_format
    std::string timestr = std::format("{:%c}", ftime);
#else
#    if 0
    std::time_t cftime  = std::chrono::system_clock::to_time_t(std::chrono::file_clock::to_sys(ftime));
    std::string timestr = std::asctime(std::localtime(&cftime));
    timestr.pop_back();  // rm the trailing '\n' put by `asctime`
#    else
    std::string timestr = "Fri, 05 Sep 2014 19:00:00 GMT";
#    endif
#endif

    if (j) {
        j->begin_object();
        j->member("name", replace_fs_name(fpath));
        // j->member("shortname", fullPath);
        j->member("size", is_dir ? -1 : size);
        j->member("datetime", timestr);
        if (is_dir && level--) {
            j->begin_array("files");
            std::error_code ec;
            auto            iter = stdfs::directory_iterator { fpath, ec };
            for (auto const& dirent : iter) {
                sendPropResponse(response, level, dirent.path(), j);
            }
            j->end_array();
        }
        j->end_object();
    } else {
        // send response
        std::string tag(fpath.string() + timestr);

        sendXMLResponse(response, is_dir, replace_fs_name(fpath).string(), tag, timestr, size);

        if (is_dir && level--) {
            if (_isMacOS && is_fs_root(fpath)) {
                auto mni = fpath / ".metadata_never_index";
                sendXMLResponse(response, false, replace_fs_name(mni).string(), "", timestr, 0);
            }
            std::error_code ec;
            auto            iter = stdfs::directory_iterator { fpath, ec };
            for (auto const& dirent : iter) {
                sendPropResponse(response, level, dirent.path(), j);
            }
        }
    }
}
