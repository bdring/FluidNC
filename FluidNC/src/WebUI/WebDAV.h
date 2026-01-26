#include <Arduino.h>

#include <string_view>
#include "FluidPath.h"
#include "JSONEncoder.h"
#include "FileStream.h"

struct RequestState {
    FileStream* outFile;
};

class WebDAV : public AsyncWebHandler {
    enum class DavResource { NONE, FILE, DIR };
    enum class DavDepth { NONE = 0, CHILD = 1, ALL = 999 };

public:
    WebDAV(const std::string_view url, const Volume& volume, bool allow_metadata);

    bool canHandle(AsyncWebServerRequest* request) const override final;
    void handleRequest(AsyncWebServerRequest* request) override final;
    void handleBody(AsyncWebServerRequest* request, unsigned char* data, size_t len, size_t index, size_t total) override final;

    virtual void handleUpload(
        AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) override final;

    const char* url() const { return _url.c_str(); }

private:
    std::string   _url;
    const Volume& _volume;
    bool          _isMacOS = false;
    bool          _reject_metadata = false;

    void handlePropfind(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleGet(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handlePut(const FluidPath&       path,
                   DavResource            resource,
                   AsyncWebServerRequest* request,
                   unsigned char*         data,
                   size_t                 len,
                   size_t                 index,
                   size_t                 total);
    void handleLock(const stdfs::path& path, AsyncWebServerRequest* request);
    void handleUnlock(const stdfs::path& path, AsyncWebServerRequest* request);
    void handleMkcol(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleMove(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleDelete(const FluidPath& path, DavResource resource, AsyncWebServerRequest* request);
    void handleHead(DavResource resource, AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);
    void sendPropResponse(AsyncResponseStream* response, int level, const stdfs::path& fpath, JSONencoder* j);

    void sendXMLResponse(
        AsyncResponseStream* response, bool is_dir, const std::string& name, const std::string& tag, const std::string& time, uint32_t size);

    std::string urlToUri(std::string url);

    bool acceptsType(AsyncWebServerRequest* request, const char*);
    bool acceptsEncoding(AsyncWebServerRequest* request, const char*);

    bool isMacOS(AsyncWebServerRequest* request);
    bool rejectMacMetadata(AsyncWebServerRequest* request, stdfs::path& path);

    stdfs::path replace_fs_name(const stdfs::path& p);
    bool        is_fs_root(const stdfs::path& p);
};
