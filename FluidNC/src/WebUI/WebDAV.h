#include <Arduino.h>

#include <string_view>
#include "FluidPath.h"
#include "JSONEncoder.h"

class WebDAV : public AsyncWebHandler {
    enum class DavResource { NONE, FILE, DIR };
    enum class DavDepth { NONE = 0, CHILD = 1, ALL = 999 };

public:
    WebDAV(const std::string_view url, const Volume& volume);

    bool canHandle(AsyncWebServerRequest* request) const override final;
    void handleRequest(AsyncWebServerRequest* request) override final;
    void handleBody(AsyncWebServerRequest* request, unsigned char* data, size_t len, size_t index, size_t total) override final;

    const char* url() const { return _url.c_str(); }

private:
    std::string _url;
    const Volume& _volume;

    void handlePropfind(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleGet(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handlePut(const FluidPath&       path,
                   DavResource            resource,
                   AsyncWebServerRequest* request,
                   unsigned char*         data,
                   size_t                 len,
                   size_t                 index,
                   size_t                 total);
    void handleLock(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleUnlock(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleMkcol(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleMove(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleDelete(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleHead(DavResource resource, AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);
    void sendPropResponse(AsyncResponseStream* response, int level, const stdfs::path& fpath, JSONencoder* j);

    std::string urlToUri(std::string url);

    bool acceptsType(AsyncWebServerRequest* request, const char*);
    bool acceptsEncoding(AsyncWebServerRequest* request, const char*);

    stdfs::path replace_fs_name(const stdfs::path& p);
};
