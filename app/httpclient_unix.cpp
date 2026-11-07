#include "app/httpclient.h"
#include <QCoreApplication>
#include <QFile>
#include <curl/curl.h>
#include <mutex>

const char* const kTmcUserAgent = "OSMC/1.0 (+https://osmc.pro; half.shine@gmail.com)";

namespace
{
std::once_flag gCurlInit;

size_t appendBody(char* data, size_t size, size_t nmemb, void* user)
{
    static_cast<QByteArray*>(user)->append(data, size * nmemb);
    return size * nmemb;
}

int cancelTransfer(void* client, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    // nonzero return aborts the transfer with CURLE_ABORTED_BY_CALLBACK
    return static_cast<const std::atomic_bool*>(client)->load() ? 1 : 0;
}
}

HttpResult httpRequest(const QString& url, const QByteArray& body, const QByteArray& contentType, int timeoutMs, const std::atomic_bool* cancel)
{
    HttpResult res;
    std::call_once(gCurlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* easy = curl_easy_init();
    if (!easy) { res.error = QStringLiteral("curl_easy_init: out of memory"); return res; }

    const QByteArray urlUtf8 = url.toUtf8();
    // a CA bundle shipped next to the executable (the AppImage) wins over the system store — same rule as GDAL data in main.cpp
    static const QByteArray caPath = [] {
        const QString file = QCoreApplication::applicationDirPath() + QStringLiteral("/ca-certificates.crt");
        return QFile::exists(file) ? file.toUtf8() : QByteArray();
    }();
    struct curl_slist* headers = nullptr;
    if (!body.isEmpty()) headers = curl_slist_append(headers, (QByteArray("Content-Type: ") + contentType).constData());
    char errbuf[CURL_ERROR_SIZE] = {};

    curl_easy_setopt(easy, CURLOPT_URL, urlUtf8.constData());
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);                                  // no alarm() signals: the call runs in a worker thread
    curl_easy_setopt(easy, CURLOPT_USERAGENT, kTmcUserAgent);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);                            // WinHTTP follows redirects by default too
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");              // Overpass extracts are several times smaller on the wire
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 10000L);                     // the same resolve/connect limit as the WinHTTP client
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);                           // the WinHTTP timeouts are per operation, not total: a slow but live download may run longer
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, static_cast<long>((timeoutMs + 999) / 1000));   // long: setopt is variadic, curl reads this option as a long
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errbuf);
    if (!caPath.isEmpty()) curl_easy_setopt(easy, CURLOPT_CAINFO, caPath.constData());
    if (headers) curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    if (cancel)
    {
        curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, cancelTransfer);
        curl_easy_setopt(easy, CURLOPT_XFERINFODATA, const_cast<std::atomic_bool*>(cancel));
    }
    if (!body.isEmpty())
    {
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.constData());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    const CURLcode code = (cancel && cancel->load()) ? CURLE_ABORTED_BY_CALLBACK : curl_easy_perform(easy);
    long status = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    res.status = code == CURLE_ABORTED_BY_CALLBACK ? 0 : int(status);
    if (code == CURLE_ABORTED_BY_CALLBACK) res.error = QStringLiteral("cancelled");
    else if (code != CURLE_OK)
    {
        res.error = QStringLiteral("request: %1").arg(QString::fromUtf8(curl_easy_strerror(code)));
        if (errbuf[0]) res.error += QStringLiteral(": %1").arg(QString::fromUtf8(errbuf));
    }
    if (!res.error.isEmpty()) res.body.clear();
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(easy);
    return res;
}
