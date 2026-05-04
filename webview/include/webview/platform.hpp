#pragma once

#ifndef WEBVIEW_PLATFORM
#  if defined(__APPLE__)
#    define WEBVIEW_PLATFORM "macos"
#  elif defined(_WIN32)
#    define WEBVIEW_PLATFORM "windows"
#  else
#    define WEBVIEW_PLATFORM "linux"
#  endif
#endif

// Per-platform feature flags (also set by CMake; guard against double-define)
#if defined(__APPLE__) && !defined(WEBVIEW_FEATURE_REQUEST_INTERCEPT)
#  define WEBVIEW_FEATURE_REQUEST_INTERCEPT
#elif defined(_WIN32) && !defined(WEBVIEW_FEATURE_REQUEST_INTERCEPT)
#  define WEBVIEW_FEATURE_REQUEST_INTERCEPT
#endif