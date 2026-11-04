// MesiStringUtil.h - CString <-> UTF-8 std::string helpers for the MFC layer.
// The MESI core stores every path and text as UTF-8; these are the only
// conversion points so the rest of the UI never mixes encodings.
#pragma once

#include <afxstr.h>
#include <string>
#include <string_view>

namespace mesiui {

inline std::string ToUtf8(const CStringW& s)
{
	if (s.IsEmpty()) return {};
	const int needed = ::WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<std::size_t>((needed > 0) ? needed : 0), '\0');
	if (needed > 0)
		::WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), out.data(), needed, nullptr, nullptr);
	return out;
}

inline std::string ToUtf8(const CStringA& s)
{
	// ANSI build: go through the wide form so non-ASCII code pages survive.
	return ToUtf8(CStringW(s));
}

inline CStringW Utf8ToWide(std::string_view s)
{
	if (s.empty()) return {};
	const int len = static_cast<int>(s.size());
	const int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), len, nullptr, 0);
	CStringW out;
	if (needed > 0) {
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), len, out.GetBuffer(needed), needed);
		out.ReleaseBuffer(needed);
	}
	return out;
}

inline CString FromUtf8(std::string_view s)
{
	return CString(Utf8ToWide(s));
}

inline CString FormatDouble(double v, int decimals = 2)
{
	CString s;
	s.Format(_T("%.*f"), decimals, v);
	return s;
}

// Reads a double from an edit control string; returns false when the text is
// not a plain number (DDV_MinMaxDouble is then used for the range).
inline bool ParseDouble(const CString& text, double& out)
{
	CString t(text);
	t.Trim();
	if (t.IsEmpty()) return false;
	LPTSTR end = nullptr;
	out = _tcstod(t, &end);
	return end != nullptr && *end == _T('\0');
}

} // namespace mesiui
