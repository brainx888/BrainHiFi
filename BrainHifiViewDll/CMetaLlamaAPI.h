#pragma once

#pragma once
#include <windows.h>

#ifdef METAVISION_EXPORTS
#define META_API __declspec(dllexport)
#else
#define META_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

	// imagePath : C:\mri\brain.png
	// prompt    : L"Describe this MRI image..."
	// apiKey    : Your Meta Llama API key
	// result    : [out] AI answer text
	// resultChars: size of result buffer
	BOOL  Meta_AnalyzeImage(LPCWSTR imagePath, LPCWSTR prompt, LPCWSTR apiKey, LPWSTR result, int resultChars);
	void  Meta_GetLastError(LPWSTR buffer, int bufferChars);

#ifdef __cplusplus
}
#endif

CString CallMetaApiFromCpp1();
CString CallMetaApiFromCpp(
	const CString& prompt = L"Reply with exactly one short sentence: say hello and confirm the API works.",
	const wchar_t* effort = L"low",
	int maxOutputTokens = 256);

class CMetaLlamaAPI  
{
public:
	CMetaLlamaAPI();
	virtual ~CMetaLlamaAPI();
};


