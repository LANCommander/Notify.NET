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
 * Selects which Windows system notification sound to play.
 * WNT_AUDIO_FILE_NONE (-1) means no specific sound file override (use AudioOption behaviour).
 * Values 0-25 map directly to WinToastTemplate::AudioSystemFile.
 */
typedef enum _WNT_AudioFile {
    WNT_AUDIO_FILE_NONE     = -1,
    WNT_AUDIO_FILE_DEFAULT  =  0,
    WNT_AUDIO_FILE_IM       =  1,
    WNT_AUDIO_FILE_MAIL     =  2,
    WNT_AUDIO_FILE_REMINDER =  3,
    WNT_AUDIO_FILE_SMS      =  4,
    WNT_AUDIO_FILE_ALARM    =  5,
    WNT_AUDIO_FILE_ALARM2   =  6,
    WNT_AUDIO_FILE_ALARM3   =  7,
    WNT_AUDIO_FILE_ALARM4   =  8,
    WNT_AUDIO_FILE_ALARM5   =  9,
    WNT_AUDIO_FILE_ALARM6   = 10,
    WNT_AUDIO_FILE_ALARM7   = 11,
    WNT_AUDIO_FILE_ALARM8   = 12,
    WNT_AUDIO_FILE_ALARM9   = 13,
    WNT_AUDIO_FILE_ALARM10  = 14,
    WNT_AUDIO_FILE_CALL     = 15,
    WNT_AUDIO_FILE_CALL1    = 16,
    WNT_AUDIO_FILE_CALL2    = 17,
    WNT_AUDIO_FILE_CALL3    = 18,
    WNT_AUDIO_FILE_CALL4    = 19,
    WNT_AUDIO_FILE_CALL5    = 20,
    WNT_AUDIO_FILE_CALL6    = 21,
    WNT_AUDIO_FILE_CALL7    = 22,
    WNT_AUDIO_FILE_CALL8    = 23,
    WNT_AUDIO_FILE_CALL9    = 24,
    WNT_AUDIO_FILE_CALL10   = 25
} WNT_AudioFile;

/** Controls how the app-logo image is cropped. */
typedef enum _WNT_CropHint {
    WNT_CROP_HINT_SQUARE = 0,
    WNT_CROP_HINT_CIRCLE = 1
} WNT_CropHint;

/**
 * Describes the notification to display.
 * All pointer fields may be NULL where noted.
 * Callers must keep pointed-to memory valid for the duration of WNT_ShowToast.
 *
 * Field layout (x64, no explicit packing):
 *   offsets 0..39   — five pointers (title, body, imagePath, heroImagePath, buttonLabels)
 *   offset  40      — buttonCount (int, 4 bytes) + 4 bytes natural padding
 *   offset  48      — expirationMs (long long, 8 bytes)
 *   offset  56      — scenario (int, 4 bytes)
 *   offset  60      — audioOption (int, 4 bytes)
 *   offsets 64..79  — three new pointers (inlineImagePath, attributionText, customAudioPath)
 *   offset  88      — cropHint (int, 4 bytes)
 *   offset  92      — audioFile (int, 4 bytes)
 * Total: 96 bytes
 */
typedef struct _WNT_ToastDescriptor {
    const wchar_t*   title;             /* required */
    const wchar_t*   body;              /* nullable */
    const wchar_t*   imagePath;         /* nullable — absolute path; app logo override in generic templates */
    const wchar_t*   heroImagePath;     /* nullable — absolute path; full-width banner above the notification */
    const wchar_t**  buttonLabels;      /* nullable — array of buttonCount wchar_t* */
    int              buttonCount;
    long long        expirationMs;      /* 0 = platform default */
    WNT_Scenario     scenario;
    WNT_AudioOption  audioOption;
    /* Extended fields (added in v2): */
    const wchar_t*   inlineImagePath;   /* nullable — image displayed inline inside the notification body */
    const wchar_t*   attributionText;   /* nullable — small text shown at the bottom of the notification */
    const wchar_t*   customAudioPath;   /* nullable — ms-winsoundevent: URI or ms-appx:/// path; overrides audioFile */
    int              cropHint;          /* WNT_CropHint — how imagePath is cropped (Square or Circle) */
    int              audioFile;         /* WNT_AudioFile — system sound to play; -1 = not overridden */
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
 * @param appIconPath     Optional absolute path to an .ico (or .exe/.dll) file whose
 *                        first icon is stamped onto the Start-Menu shortcut. This is
 *                        the small icon shown in the top-left corner of every toast
 *                        notification from this app. Pass NULL to use the default
 *                        (the host executable's icon).
 * @return TRUE on success.
 */
NOTIFYAPI BOOL WNT_Initialize(const wchar_t* appName, const wchar_t* appUserModelId, const wchar_t* appIconPath);

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
