// CBrainHifiLinkMetaModal.cpp : implementation file
//

#include "pch.h"
#include "CBrainHifiLinkMetaModal.h"


#include <windows.h>
#include <winhttp.h>
#include <string>
#include <iostream>
#include <future>
#include <format> // C++20 formatting

#pragma comment(lib, "winhttp.lib")

struct ModalResponse {
    int statusCode = 0;
    std::string body;
    bool success = false;
};

// Asynchronous worker to prevent UI freezing on Windows desktop apps
std::future<ModalResponse> CallModalEndpointAsync(
    const std::wstring& serverName,
    const std::wstring& endpointPath,
    const std::string& jsonPayload)
{
    return std::async(std::launch::async, [=]() {
        ModalResponse result;

        // 1. Initialize WinHTTP session
        HINTERNET hSession = WinHttpOpen(
            L"BrainHifi-DesktopClient/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0
        );
        if (!hSession) return result;

        // 2. Connect to Modal HTTPS domain
        HINTERNET hConnect = WinHttpConnect(hSession, serverName.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return result;
        }

        // 3. Open HTTP POST request (WINHTTP_FLAG_SECURE enforces HTTPS/TLS)
        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect, L"POST", endpointPath.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        // 4. Set custom headers (Content-Type & optional Auth Token)
        std::wstring headers = L"Content-Type: application/json\r\n";
        // headers += L"Authorization: Bearer YOUR_MODAL_TOKEN\r\n"; 

        // 5. Send request with payload
        BOOL bResults = WinHttpSendRequest(
            hRequest,
            headers.c_str(), (DWORD)-1L,
            (LPVOID)jsonPayload.data(), (DWORD)jsonPayload.size(),
            (DWORD)jsonPayload.size(), 0
        );

        // 6. Receive response
        if (bResults) {
            bResults = WinHttpReceiveResponse(hRequest, NULL);
        }

        if (bResults) {
            DWORD dwStatusCode = 0;
            DWORD dwSize = sizeof(dwStatusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

            result.statusCode = static_cast<int>(dwStatusCode);

            // Read response data
            DWORD dwDownloaded = 0;
            std::string responseBody;
            do {
                DWORD dwSizeAvailable = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSizeAvailable)) break;
                if (dwSizeAvailable == 0) break;

                auto buffer = std::make_unique<char[]>(dwSizeAvailable + 1);
                ZeroMemory(buffer.get(), dwSizeAvailable + 1);

                if (WinHttpReadData(hRequest, buffer.get(), dwSizeAvailable, &dwDownloaded)) {
                    responseBody.append(buffer.get(), dwDownloaded);
                }
            } while (dwDownloaded > 0);

            result.body = responseBody;
            result.success = (result.statusCode == 200);
        }

        // Cleanup handles
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        return result;
        });
}

// call Meta Modal endpoint asynchronously and handle response
int test() {
    // Configure Visual Studio project for C++20 (/std:c++20)
    std::wcout << L"Sending request to Modal via Windows WinHTTP...\n";

    // Modal URL breakdown: https://username--app-name-endpoint.modal.run
    std::wstring server = L"username--app-name-endpoint.modal.run";
    std::wstring path = L"/"; // or specific route
    std::string payload = "{\"value\": 42}";

    // Fire off async request
    auto futureResponse = CallModalEndpointAsync(server, path, payload);

    // Do other UI or background work here while waiting...

    // Block and get result when ready (e.g., with a 30-second timeout check)
    if (futureResponse.wait_for(std::chrono::seconds(30)) == std::future_status::ready) {
        ModalResponse res = futureResponse.get();
        if (res.success) {
            std::cout << "Modal Success! Response:\n" << res.body << std::endl;
        }
        else {
            std::cerr << "Modal Error. Status: " << res.statusCode << "\n" << res.body << std::endl;
        }
    }
    else {
        std::cerr << "Request timed out.\n";
    }

    return 0;
}
CBrainHifiLinkMetaModal::CBrainHifiLinkMetaModal()
{
}

CBrainHifiLinkMetaModal::~CBrainHifiLinkMetaModal()
{
}


// CBrainHifiLinkMetaModal member functions
