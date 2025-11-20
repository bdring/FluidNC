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

WebDAV::WebDAV(const std::string_view url, const char* fsname) : _url(url), _fsname(fsname) {}

bool WebDAV::canHandle(AsyncWebServerRequest* request) const {
    if (request->url().startsWith(_url.c_str())) {
        return true;
    }
    return false;
}

static const char* rootname = "/";
static const char* slashstr = "/";
static const char  slash    = '/';

void WebDAV::handleRequest(AsyncWebServerRequest* request) {
    // parse the url to a proper path
    std::string path = std::string(request->url().substring(_url.length()).c_str());
    if (path.empty()) {
        path = rootname;
    }
    if (path != rootname && path[path.length() - 1] == '/') {
        path.pop_back();  // Remove the trailing /
    }

    DavResource resource = DavResource::NONE;

    std::error_code ec;

    FluidPath fpath = { path, _fsname, ec };

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
        if (resource == DavResource::FILE) {
            return request->send(200);
        }

        // Open the file for appending to see if it is possible, then
        // close it (automatically when file goes out of scope.
        // handleBody will add stuff to the file later.
        FileStream* file = nullptr;
        try {
            file = new FileStream(fpath, "a", "");
        } catch (const Error err) {
            log_debug(fpath << " cannot be opened");
            return request->send(500);
            ;
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
    if (request->method() == HTTP_HEAD || request->method() == HTTP_OPTIONS) {
        return handleHead(resource, request);
    }
    if (request->method() == HTTP_LOCK) {
        return handleLock(fpath, resource, request);
    }
    if (request->method() == HTTP_UNLOCK) {
        return handleUnlock(fpath, resource, request);
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
    // parse the url to a proper path
    String path = request->url().substring(_url.length());
    if (path.isEmpty()) {
        path = rootname;
    }
    if (!path.equals(rootname) && path.endsWith(slashstr)) {
        path = path.substring(0, path.length() - 1);
    }

    // check resource type on local storage
    DavResource resource = DavResource::NONE;

    std::error_code ec;

    FluidPath fpath(path.c_str(), _fsname, ec);

    if (ec) {
        resource = DavResource::NONE;
    } else if (stdfs::is_regular_file(fpath)) {
        resource = DavResource::FILE;
    } else if (stdfs::is_directory(fpath)) {
        resource = DavResource::DIR;
    }

    // route the request
    if (request->method() == HTTP_PUT) {
        return handlePut(fpath, resource, request, data, len, index, total);
    }
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

    std::string fullPath = fpath;
    size_t      pos      = 0;

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
    sendPropResponse(response, (int)depth, fullPath, j);
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
                file   = new FileStream(gzpath, "r", "");
                isGzip = true;
            } catch (const Error err) {}
        }
    } else {
        try {
            file = new FileStream(fpath, "r", "");
        } catch (const Error err) {}
    }

    if (!file) {
        log_debug(fpath << " not found");
        AsyncWebServerResponse* response = request->beginResponse(404);
        response->addHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
        request->send(response);
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(
        getContentType(fpath.c_str()), file->size(), [file, request](uint8_t* buffer, size_t maxLen, size_t total) mutable -> size_t {
            if (!file) {
                request->client()->close();
                return 0;  //RESPONSE_TRY_AGAIN; // This only works for ChunkedResponse
            }
            if (total >= file->size() || request->method() != HTTP_GET) {
                file = nullptr;
                return 0;
            }
            int bytes  = min(file->size(), maxLen);
            int actual = file->read(buffer, bytes);  // return 0 even when no bytes were loaded
            if (bytes == 0 || (bytes + total) >= file->size()) {
                file = nullptr;
            }
            return bytes;
        });

    response->addHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
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
        FileStream file(fpath, index ? "a" : "w", "");
        file.write(data, len);
        file.flush();
    } catch (const Error err) { log_debug(fpath << " cannot be opened"); }
}

void WebDAV::handleLock(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    std::string lockroot("http://");
    lockroot += request->host().c_str();
    lockroot += _url;
    lockroot += fpath;

    AsyncResponseStream* response = request->beginResponseStream("application/xml; charset=utf-8");
    response->setCode(200);
    response->addHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
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

void WebDAV::handleUnlock(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200);
    response->addHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
    response->addHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");
    request->send(response);
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
    const AsyncWebHeader* destinationHeader = request->getHeader("destination");
    if (!destinationHeader || destinationHeader->value().isEmpty()) {
        return handleNotFound(request);
    }

    std::string newname = std::string(urlToUri(destinationHeader->value().c_str()));

    AsyncWebServerResponse* response;

    // Should handle "Overwrite: {T,F}" header
    std::error_code ec;
    FluidPath       newpath(newname, _fsname, ec);
    if (ec) {
        response = request->beginResponse(500, "text/plain", "Unable to move");
    } else {
        std::filesystem::rename(fpath, newpath, ec);
        if (ec) {
            response = request->beginResponse(500, "text/plain", "Unable to move");
        } else {
            //        HashFS::rename_file(fpath, newname);
            response = request->beginResponse(201);
            response->addHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
        }
    }
    request->send(response);
}

void WebDAV::handleDelete(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    // delete file or dir
    bool            result;
    std::error_code ec;
    if (resource == DavResource::FILE) {
        result = stdfs::remove(fpath, ec);
    } else {
        // remove_all returns the number of items that were deleted
        result = stdfs::remove_all(fpath, ec) == 0;
    }

    // check for error
    AsyncWebServerResponse* response;
    if (result) {
        response = request->beginResponse(200);
        response->addHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
    } else {
        response = request->beginResponse(201);
    }

    request->send(response);
}

void WebDAV::handleHead(DavResource resource, AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(200);
    if (resource == DavResource::FILE) {
        response->addHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
    }
    if (resource == DavResource::DIR) {
        response->addHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");
    }
    request->send(response);
}

void WebDAV::handleNotFound(AsyncWebServerRequest* request) {
    AsyncWebServerResponse* response = request->beginResponse(404);
    response->addHeader("Allow", "OPTIONS,MKCOL,POST,PUT");
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

void WebDAV::sendPropResponse(AsyncResponseStream* response, int level, std::string fullPath, JSONencoder* j) {
    auto   ftime  = stdfs::last_write_time(fullPath);
    bool   is_dir = stdfs::is_directory(fullPath);
    size_t size   = is_dir ? -1 : stdfs::file_size(fullPath);

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
        j->member("name", fullPath);
        // j->member("shortname", fullPath);
        j->member("size", is_dir ? -1 : size);
        j->member("datetime", timestr);
        if (is_dir && level--) {
            j->begin_array("files");
            std::error_code ec;
            auto            iter = stdfs::directory_iterator { fullPath, ec };
            for (auto const& dirent : iter) {
                sendPropResponse(response, level, dirent.path().string(), j);
            }
            j->end_array();
        }
        j->end_object();
    } else {
        // send response
        response->print("<d:response>");
        response->printf("<d:href>%s</d:href>", fullPath.c_str());
        response->print("<d:propstat>");
        response->print("<d:prop>");

        response->printf("<d:getlastmodified>%s</d:getlastmodified>", timestr.c_str());

        if (is_dir) {
            // resource type
            response->print("<d:resourcetype><d:collection/></d:resourcetype>");
        } else {
            std::string shadata(fullPath);
            shadata += timestr;
            // etag
            response->printf("<d:getetag>%s</d:getetag>", sha1(shadata.c_str()).c_str());

            // resource type
            response->print("<d:resourcetype/>");

            // content length
            response->printf("<d:getcontentlength>%d</d:getcontentlength>", size);

            // content type
            response->print("<d:getcontenttype>text/plain</d:getcontenttype>");
        }
        response->print("</d:prop>");
        response->print("<d:status>HTTP/1.1 200 OK</d:status>");
        response->print("</d:propstat>");

        response->print("</d:response>");
        if (is_dir && level--) {
            std::error_code ec;
            auto            iter = stdfs::directory_iterator { fullPath, ec };
            for (auto const& dirent : iter) {
                sendPropResponse(response, level, dirent.path().string(), j);
            }
        }
    }
}
