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

WebDAV::WebDAV(const std::string_view url, const char* fsname) : _url(url), _fsname(fsname) {}

bool WebDAV::canHandle(AsyncWebServerRequest* request) const {
    if (request->url().startsWith(_url.c_str())) {
        return true;
    }
    return false;
}

void WebDAV::handleRequest(AsyncWebServerRequest* request) {
    ::printf("DAV handleRequest %s\n", request->url().c_str());

    bool acceptGz = false;
    if (request->hasHeader("Accept-Encoding")) {
        auto encodings = std::string(request->getHeader("Accept-Encoding")->value().c_str());
        if (encodings.find("gzip") != std::string::npos) {
            acceptGz = true;
        }
    }

    // parse the url to a proper path
    std::string path = std::string(request->url().substring(_url.length()).c_str());
    if (path.empty()) {
        path = "/";
    }
    if (path != "/" && path[path.length() - 1] == '/') {
        path.pop_back();  // Remove the trailing /
    }

    DavResource resource = DavResource::NONE;

    std::error_code ec;
    FluidPath       fluid_path(path, _fsname, ec);

    stdfs::path fpath = fluid_path;

    if (ec) {
        if (acceptGz) {
            stdfs::path gzpath(fpath);
            gzpath += ".gz";
        }
        resource = DavResource::NONE;
    }

    if (stdfs::is_regular_file(fpath)) {
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

    // If we are not creating the resource it must already exist
    if (resource == DavResource::NONE) {
        return handleNotFound(request);
    }

    if (request->method() == HTTP_PROPFIND || request->method() == HTTP_PROPPATCH) {
        return handlePropfind(fpath, resource, request);
    }
    if (request->method() == HTTP_GET) {
        return handleGet(fpath, resource, request);
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
        path = "/";
    }
    if (!path.equals("/") && path.endsWith("/")) {
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

    // prepare response

    //    std::string  s;  // Lifetime needs to extend to end of this function
    bool isJSON = false;
    if (request->hasHeader("Accept")) {
        auto encodings = std::string(request->getHeader("Accept")->value().c_str());
        if (encodings.find("application/json") != std::string::npos) {
            isJSON = true;
        }
    }

    JSONencoder* j = nullptr;

    AsyncResponseStream* response = request->beginResponseStream(isJSON ? "application/json" : "application/xml");
    response->setCode(207);
    JsonCallback cb = [response](const char* s) { response->print(s); };

    j = new JSONencoder(&cb);

    auto   ftime  = stdfs::last_write_time(fpath);
    bool   is_dir = resource == DavResource::DIR;
    size_t size   = is_dir ? -1 : stdfs::file_size(fpath);

    if (isJSON) {
        j->begin();
    } else {
        response->print("<?xml version=\"1.0\"?>");
        response->print("<d:multistatus xmlns:d=\"DAV:\">");
    }
    if (!is_dir || !noroot) {
        sendPropResponse(response, false, fpath.filename(), is_dir, size, ftime, j);
    }
    if (resource == DavResource::DIR && depth == DavDepth::CHILD) {
        std::error_code ec;
        auto            iter = stdfs::directory_iterator { fpath, ec };
        if (!ec) {
            if (isJSON) {
                j->begin_array("files");
            }
            for (auto const& dirent : iter) {
                bool   is_dir = dirent.is_directory();
                size_t size   = is_dir ? -1 : dirent.file_size();
                sendPropResponse(response, true, dirent.path().filename().string(), is_dir, size, dirent.last_write_time(), j);
            }
            if (isJSON) {
                j->end_array();
            }
        }
    }
    if (isJSON) {
        j->end();
        //        response->print(s.c_str());
        delete j;
    } else {
        response->print("</d:multistatus>");
    }

    return request->send(response);
}

struct mime_type {
    const char* suffix;
    const char* mime_type;
} mime_types[] = {
    { ".htm", "text/html" },         { ".html", "text/html" },        { ".css", "text/css" },   { ".js", "application/javascript" },
    { ".htm", "text/html" },         { ".png", "image/png" },         { ".gif", "image/gif" },  { ".jpeg", "image/jpeg" },
    { ".jpg", "image/jpeg" },        { ".ico", "image/x-icon" },      { ".xml", "text/xml" },   { ".pdf", "application/x-pdf" },
    { ".zip", "application/x-zip" }, { ".gz", "application/x-gzip" }, { ".txt", "text/plain" }, { "", "application/octet-stream" }
};
static bool endsWithCI(const char* suffix, const char* test) {
    size_t slen = strlen(suffix);
    size_t tlen = strlen(test);
    if (slen > tlen || slen == 0) {
        return false;
    }
    const char* s = suffix + slen;
    const char* t = test + tlen;
    while (--s != suffix) {
        if (tolower(*s) != tolower(*--t)) {
            return false;
        }
    }
    return true;
}
static const char* getContentType(const char* filename) {
    mime_type* m;
    for (m = mime_types; *(m->suffix) != '\0'; ++m) {
        if (endsWithCI(m->suffix, filename)) {
            return m->mime_type;
        }
    }
    return m->mime_type;
}

void WebDAV::handleGet(const FluidPath& fpath, DavResource resource, AsyncWebServerRequest* request) {
    FileStream* file = nullptr;
    try {
        file = new FileStream(fpath, "r", "");
    } catch (const Error err) { log_debug(fpath << " not found"); }

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
    ::printf("url %s\n", url.c_str());
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

void WebDAV::sendPropResponse(AsyncResponseStream*         response,
                              bool                         recursing,
                              std::string                  fullPath,
                              bool                         is_dir,
                              size_t                       size,
                              const stdfs::file_time_type& ftime,
                              JSONencoder*                 j) {
    if (fullPath.substr(0, 1) != "/") {
        fullPath.insert(0, "/");
    }
    if (is_dir && fullPath.length() > 1 && fullPath[fullPath.length() - 1] != '/') {
        fullPath += "/";
    }
    fullPath.insert(0, _url);

    size_t pos = 0;

    while ((pos = fullPath.find(" ", pos)) != std::string::npos) {
        fullPath.replace(pos, fullPath.length(), "%20");
        pos += strlen("%20");  // Move past the replacement to avoid infinite loops
    }

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
    }
}
