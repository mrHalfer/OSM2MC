#include "app/httpclient.h"
#include <QThread>
#include <string>
#include <thread>
#include <windows.h>
#include <winhttp.h>

const char* const kTmcUserAgent = "tMC/1.0 (+https://github.com/mrHalfer/tMC; half.shine@gmail.com)";

namespace
{
QString lastErrorText(const char* what)
{
    const DWORD e = GetLastError();
    return QStringLiteral("%1: network error %2").arg(QString::fromUtf8(what)).arg(e);
}
}

HttpResult httpRequest(const QString& url, const QByteArray& body, const QByteArray& contentType, int timeoutMs, const std::atomic_bool* cancel)
{
    HttpResult res;
    const std::wstring wurl = url.toStdWString();
    wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { res.error = QStringLiteral("invalid address: %1").arg(url); return res; }
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    const std::wstring target = std::wstring(path) + extra;

    const std::wstring ua = QString::fromLatin1(kTmcUserAgent).toStdWString();
    HINTERNET session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { res.error = lastErrorText("WinHttpOpen"); return res; }
    WinHttpSetTimeouts(session, 10000, 10000, timeoutMs, timeoutMs);
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, body.isEmpty() ? L"GET" : L"POST", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    auto cleanup = [&]() { if (req) WinHttpCloseHandle(req); if (conn) WinHttpCloseHandle(conn); WinHttpCloseHandle(session); };
    if (!req) { res.error = lastErrorText("WinHttpOpenRequest"); cleanup(); return res; }

    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;                            // gzip/deflate: Overpass extracts are several times smaller on the wire
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));
    std::wstring headers = L"Accept-Encoding: gzip, deflate\r\n";
    if (!body.isEmpty()) headers += L"Content-Type: " + QString::fromLatin1(contentType).toStdWString() + L"\r\n";

    // cancellation while waiting for the response: a separate thread closes the request, the blocking call then fails
    std::atomic_bool done{ false };
    std::atomic_bool aborted{ false };
    std::thread watcher;
    if (cancel)
        watcher = std::thread([&]()
        {
            while (!done.load())
            {
                if (cancel->load()) { aborted.store(true); HINTERNET h = req; if (h) WinHttpCloseHandle(h); return; }
                Sleep(80);
            }
        });
    auto finish = [&]()
    {
        done.store(true);
        if (watcher.joinable()) watcher.join();
    };

    bool ok = WinHttpSendRequest(req, headers.c_str(), DWORD(-1), body.isEmpty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.constData(),
                                 DWORD(body.size()), DWORD(body.size()), 0)
              && WinHttpReceiveResponse(req, nullptr);
    if (ok)
    {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        res.status = int(status);
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; break; }
            if (avail == 0) break;
            const int old = res.body.size();
            res.body.resize(old + int(avail));
            DWORD got = 0;
            if (!WinHttpReadData(req, res.body.data() + old, avail, &got)) { ok = false; break; }
            res.body.resize(old + int(got));
        }
    }
    const QString err = ok ? QString() : lastErrorText("request");
    finish();
    if (aborted.load()) { res.error = QStringLiteral("cancelled"); res.status = 0; res.body.clear(); conn ? WinHttpCloseHandle(conn) : (void)0; WinHttpCloseHandle(session); return res; }
    if (!ok) { res.error = err; res.body.clear(); }
    cleanup();
    return res;
}
