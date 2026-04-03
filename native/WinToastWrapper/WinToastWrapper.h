/**
 * WinToastWrapper.h
 *
 * Flat C API that wraps WinToastLib (https://github.com/mohabouje/WinToast).
 * This header is the sole interface between the managed P/Invoke layer and
 * the C++ WinToastLib implementation.
 *
 * ABI contract (must stay in sync with WinToastNative.cs):
 *   - All strings are wchar_t* (UTF-16LE, null-terminated).
 *   - Callbacks are called on a WinRT thread-pool thread, NOT the calling STA thread.
 *   - WNT_ShowToast must be called from the STA thread that called WNT_Initialize.
 *   - WNT_HideToast must be called from the same STA thread.
 */

#pragma once

#include <Windows.h>

#ifdef WINTOASTWRAPPER_EXPORTS
  #define NOTIFYAPI __declspec(dllexport)
#else
  #define NOTIFYAPI __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Callback function pointer types
 * ------------------------------------------------------------------------- */

/** Called when the user clicks the notification body (no button). */
typedef void (CALLBACK* WNT_ActivatedCallback)(INT64 toastId);

/** Called when the user clicks one of the action buttons. */
typedef void (CALLBACK* WNT_ButtonActivatedCallback)(INT64 toastId, int buttonIndex);

/**
 * Called when the notification is dismissed.
 * reason: 0 = UserCancelled, 1 = ApplicationHidden, 2 = TimedOut
 */
typedef void (CALLBACK* WNT_DismissedCallback)(INT64 toastId, int reason);

/** Called when the notification fails to display. */
typedef void (CALLBACK* WNT_FailedCallback)(INT64 toastId);

/* -------------------------------------------------------------------------
 * Structs (must match StructLayout in WinToastNative.cs exactly)
 * ------------------------------------------------------------------------- */

typedef enum _WNT_Scenario {
    WNT_SCENARIO_DEFAULT       = 0,
    WNT_SCENARIO_ALARM         = 1,
    WNT_SCENARIO_REMINDER      = 2,
    WNT_SCENARIO_INCOMING_CALL = 3
} WNT_Scenario;

typedef enum _WNT_AudioOption {
    WNT_AUDIO_DEFAULT = 0,
    WNT_AUDIO_SILENT  = 1,
    WNT_AUDIO_LOOP    = 2
} WNT_AudioOption;

/**
 * Describes the notification to display.
 * All pointer fields may be NULL where noted.
 * Callers must keep pointed-to memory valid for the duration of WNT_ShowToast.
 */
typedef struct _WNT_ToastDescriptor {
    const wchar_t*   title;          /* required */
    const wchar_t*   body;           /* nullable */
    const wchar_t*   imagePath;      /* nullable — absolute path; displayed as a square thumbnail */
    const wchar_t*   heroImagePath;  /* nullable — absolute path; displayed full-width, aspect ratio preserved */
    const wchar_t**  buttonLabels;   /* nullable — array of buttonCount wchar_t* */
    int              buttonCount;
    long long        expirationMs;   /* 0 = platform default */
    WNT_Scenario     scenario;
    WNT_AudioOption  audioOption;
} WNT_ToastDescriptor;

/**
 * Bundle of callback function pointers for one notification.
 * The struct is copied by value inside WNT_ShowToast; callers need not keep it alive.
 * Individual function pointers may be NULL to skip that event.
 */
typedef struct _WNT_Handler {
    WNT_ActivatedCallback       onActivated;
    WNT_ButtonActivatedCallback onButtonActivated;
    WNT_DismissedCallback       onDismissed;
    WNT_FailedCallback          onFailed;
} WNT_Handler;

/* -------------------------------------------------------------------------
 * API functions
 * ------------------------------------------------------------------------- */

/**
 * Initialises WinToastLib. Must be called once from an STA thread.
 *
 * @param appName         Human-readable name shown in the Action Centre.
 * @param appUserModelId  AppUserModelId (AUMI). The wrapper creates a Start-Menu
 *                        shortcut carrying this AUMI automatically if one does not
 *                        already exist.
 * @return TRUE on success.
 */
NOTIFYAPI BOOL WNT_Initialize(const wchar_t* appName, const wchar_t* appUserModelId);

/**
 * Releases all WinToastLib resources. Call from the same STA thread as WNT_Initialize.
 */
NOTIFYAPI void WNT_Uninitialize(void);

/**
 * Returns TRUE if WinToast is supported on the current Windows version (requires Win 8+).
 * Check this before calling WNT_Initialize.
 */
NOTIFYAPI BOOL WNT_IsCompatible(void);

/**
 * Displays a toast notification.
 *
 * Must be called from the STA thread that called WNT_Initialize.
 *
 * @param descriptor  Pointer to a WNT_ToastDescriptor (read-only during the call).
 * @param handler     Pointer to a WNT_Handler with callback function pointers.
 * @return A positive INT64 toast ID on success, or a negative WinToastError code on failure.
 */
NOTIFYAPI INT64 WNT_ShowToast(const WNT_ToastDescriptor* descriptor, const WNT_Handler* handler);

/**
 * Programmatically dismisses a previously shown toast.
 *
 * Must be called from the same STA thread as WNT_ShowToast.
 *
 * @param toastId  The ID returned by WNT_ShowToast.
 * @return TRUE if the toast was successfully hidden.
 */
NOTIFYAPI BOOL WNT_HideToast(INT64 toastId);

#ifdef __cplusplus
} /* extern "C" */
#endif
