//Convert file extension to content type

#include "Mime.h"
#include "string_util.h"

struct mime_type {
    const char* suffix;
    const char* mime_type;
} mime_types[] = {
    { ".htm", "text/html" },
    { ".html", "text/html" },
    { ".css", "text/css" },
    { ".js", "application/javascript" },
    { ".png", "image/png" },
    { ".gif", "image/gif" },
    { ".jpeg", "image/jpeg" },
    { ".jpg", "image/jpeg" },
    { ".ico", "image/x-icon" },
    { ".xml", "text/xml" },
    { ".pdf", "application/x-pdf" },
    { ".zip", "application/x-zip" },
    { ".gz", "application/x-gzip" },
    { ".txt", "text/plain" },
    { "", "application/octet-stream" },
};

const char* getContentType(const std::string_view filename) {
    mime_type* m;
    for (m = mime_types; *(m->suffix) != '\0'; ++m) {
        if (string_util::ends_with_ignore_case(filename, m->suffix)) {
            return m->mime_type;
        }
    }
    return m->mime_type;
}
