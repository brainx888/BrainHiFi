#pragma once
/*
 * BhExport.h - export macro of the BrainHifiViewDll numerical core.
 *
 * BRAINHIFI_EXPORTS is defined only while building BrainHifiViewDll;
 * BRAINHIFI_STATIC when the core is compiled into another target (tests).
 * Keyed on project-specific defines, never on _AFXEXT, because every
 * sibling MFC extension DLL defines _AFXEXT as well.
 */
#if defined(BRAINHIFI_STATIC)
#  define BH_API
#elif defined(_WIN32)
#  if defined(BRAINHIFI_EXPORTS)
#    define BH_API __declspec(dllexport)
#  else
#    define BH_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define BH_API __attribute__((visibility("default")))
#else
#  define BH_API
#endif
