/* Copyright 2014 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "BaseUtil.h"
#include <Wininet.h>
#include "HttpUtil.h"

#include "FileUtil.h"
#include "WinUtil.h"

// per RFC 1945 10.15 and 3.7, a user agent product token shouldn't contain whitespace
#define USER_AGENT L"BaseHTTP"

// returns ERROR_SUCCESS or an error code
DWORD HttpGet(const WCHAR *url, str::Str<char> *dataOut)
{
    DWORD error = ERROR_SUCCESS;
    DWORD dwRead = 0;
    DWORD flags = 0;

    HINTERNET hFile = NULL;
    HINTERNET hInet = InternetOpen(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet)
        goto Error;

    flags = INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD;
    hFile = InternetOpenUrl(hInet, url, NULL, 0, flags, 0);
    if (!hFile)
        goto Error;

    do {
        char buf[1024];
        if (!InternetReadFile(hFile, buf, sizeof(buf), &dwRead))
            goto Error;
        bool ok = dataOut->AppendChecked(buf, dwRead);
        if (!ok)
            goto Error;
    } while (dwRead > 0);

Exit:
    if (hFile)
        InternetCloseHandle(hFile);
    if (hInet)
        InternetCloseHandle(hInet);
    return error;

Error:
    error = GetLastError();
    if (!error)
        error = ERROR_GEN_FAILURE;
    goto Exit;
}

// Download content of a url to a file
bool HttpGetToFile(const WCHAR *url, const WCHAR *destFilePath)
{
    bool ok = false;
    HINTERNET hFile = NULL, hInet = NULL;
    DWORD dwRead = 0;
    char buf[1024];

    HANDLE hf = CreateFile(destFilePath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,  NULL);
    if (INVALID_HANDLE_VALUE == hf)
        goto Exit;

    hInet = InternetOpen(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet)
        goto Exit;

    hFile = InternetOpenUrl(hInet, url, NULL, 0, 0, 0);
    if (!hFile)
        goto Exit;

    for (;;) {
        if (!InternetReadFile(hFile, buf, sizeof(buf), &dwRead))
            goto Exit;
        if (dwRead == 0)
            break;

        DWORD size;
        BOOL wroteOk = WriteFile(hf, buf, (DWORD)dwRead, &size, NULL);
        if (!wroteOk)
            goto Exit;

        if (size != dwRead)
            goto Exit;
    }

    ok = true;
Exit:
    CloseHandle(hf);
    if (hFile)
        InternetCloseHandle(hFile);
    if (hInet)
        InternetCloseHandle(hInet);
    if (!ok)
        file::Delete(destFilePath);
    return ok;
}

bool HttpPost(const WCHAR *server, const WCHAR *url, str::Str<char> *headers, str::Str<char> *data)
{
    str::Str<char> resp(2048);
    bool ok = false;
    DWORD flags = 0;
    char *hdr = NULL;
    DWORD hdrLen = 0;
    HINTERNET hConn = NULL, hReq = NULL;
    void *d = NULL;
    DWORD dLen = 0;
    unsigned int timeoutMs = 15 * 1000;
    // Get the response status.
    DWORD respHttpCode = 0;
    DWORD respHttpCodeSize = sizeof(respHttpCode);
    DWORD dwRead = 0;

    HINTERNET hInet = InternetOpen(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet)
        goto Exit;
    hConn = InternetConnect(hInet, server, INTERNET_DEFAULT_HTTP_PORT,
                            NULL, NULL, INTERNET_SERVICE_HTTP, 0, 1);
    if (!hConn)
        goto Exit;

    hReq = HttpOpenRequest(hConn, L"POST", url, NULL, NULL, NULL, flags, NULL);
    if (!hReq)
        goto Exit;
    if (headers && headers->Count() > 0) {
        hdr = headers->Get();
        hdrLen = (DWORD)headers->Count();
    }
    if (data && data->Count() > 0) {
        d = data->Get();
        dLen = (DWORD)data->Count();
    }

    InternetSetOption(hReq, INTERNET_OPTION_SEND_TIMEOUT,
                      &timeoutMs, sizeof(timeoutMs));

    InternetSetOption(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT,
                      &timeoutMs, sizeof(timeoutMs));

    if (!HttpSendRequestA(hReq, hdr, hdrLen, d, dLen))
        goto Exit;

    HttpQueryInfo(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &respHttpCode, &respHttpCodeSize, 0);

    do {
        char buf[1024];
        if (!InternetReadFile(hReq, buf, sizeof(buf), &dwRead))
            goto Exit;
        ok = resp.AppendChecked(buf, dwRead);
        if (!ok)
            goto Exit;
    } while (dwRead > 0);

#if 0
    // it looks like I should be calling HttpEndRequest(), but it always claims
    // a timeout even though the data has been sent, received and we get HTTP 200
    if (!HttpEndRequest(hReq, NULL, 0, 0)) {
        LogLastError();
        goto Exit;
    }
#endif
    ok = (200 == respHttpCode);
Exit:
    if (hReq)
        InternetCloseHandle(hReq);
    if (hConn)
        InternetCloseHandle(hConn);
    if (hInet)
        InternetCloseHandle(hInet);
    return ok;
}

DWORD WINAPI HttpReq::DownloadThread(LPVOID data)
{
    HttpReq *req = (HttpReq *)data;
    req->error = HttpGet(req->url, req->data);
    req->callback->Callback(req);
    return 0;
}

HttpReq::HttpReq(const WCHAR *url, HttpReqCallback *callback) :
    thread(NULL), callback(callback), error(0),
    url(str::Dup(url)), data(new str::Str<char>(256))
{
    assert(url);
    if (callback)
        thread = CreateThread(NULL, 0, DownloadThread, this, 0, 0);
    else
        error = HttpGet(url, data);
}

HttpReq::~HttpReq()
{
    CloseHandle(thread);
    free(url);
    delete data;
}
