#pragma once
/*
 * BhHttps - minimal blocking WinHTTP request (no MFC).  Call from a worker thread.
 */
#include <windows.h>
#include <winhttp.h>
#include <string>

#pragma comment(lib, "winhttp.lib")

struct SBhHttpsCall
{
	std::wstring sHost;
	INTERNET_PORT nPort = INTERNET_DEFAULT_HTTPS_PORT;
	std::wstring sVerb = L"POST";
	std::wstring sPath = L"/";
	std::wstring sHeaders;          // "Name: value\r\n" lines (may hold the key: wiped by the caller)
	std::string sBody;              // UTF-8
	int nResolveMs = 10000, nConnectMs = 15000, nSendMs = 60000;
	int nReceiveMs = 300000;        // reasoning models can think for minutes; WinHTTP default is 30 s
};

struct SBhHttpsResult
{
	DWORD nStatus = 0;              // HTTP status, 0 = no HTTP response
	DWORD nWinError = 0;            // GetLastError() of the failing WinHTTP call
	std::string sBody;
};

bool BhHttpsSend(const SBhHttpsCall& call, SBhHttpsResult& res);
/// "12002 timeout" style text for common WinHTTP errors.
std::wstring BhWinHttpErrorText(DWORD nError);
