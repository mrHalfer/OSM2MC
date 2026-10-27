#pragma once
// Minimal HTTP(S) client on top of WinHTTP: system certificates and proxy, no OpenSSL and no Qt Network.
// The calls are blocking — run them from a worker thread.
#include <QByteArray>
#include <QString>
#include <atomic>

struct HttpResult
{
    int status = 0;                 //!< HTTP response code; 0 — no response was received (see error)
    QByteArray body;
    QString error;                  //!< network/cancellation error text; empty for 4xx/5xx codes — look at status
    bool ok() const { return status >= 200 && status < 300; }
};

//! GET (empty body) or POST (body set, type — contentType). cancel is the cancellation flag (also checked while waiting for the response).
//! timeoutMs — limit for waiting for the server response and reading the data.
HttpResult httpRequest(const QString& url, const QByteArray& body = QByteArray(),
                       const QByteArray& contentType = QByteArray("application/x-www-form-urlencoded"),
                       int timeoutMs = 30000, const std::atomic_bool* cancel = nullptr);

//! The program identifier sent to the OSM servers (their rules require it). For a public build set a contact here.
extern const char* const kTmcUserAgent;
