#pragma once
/*
 * BhMsiViewerApi.h - plain entry point of BrainHifiViewDll.
 * Opens the modal MRI + MEG source viewer; either path may be empty.
 */
#include "core/BhExport.h"

extern "C" BH_API INT_PTR BhShowMsiViewer(HWND hParent, LPCTSTR szMriPath, LPCTSTR szSrcPath);
