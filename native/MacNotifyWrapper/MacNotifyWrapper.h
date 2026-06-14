/**
 * MacNotifyWrapper.h
 *
 * Flat C API for macOS User Notifications (UNUserNotificationCenter).
 * Consumed by the Notify.NET managed library via P/Invoke.
 *
 * All strings are UTF-8, null-terminated.
 * Callbacks fire on a background GCD thread managed by UNUserNotificationCenter.
 * The caller must not free any memory passed to MNW_ShowNotification before it returns;
 * the implementation copies all fields before returning.
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MACNOTIFYWRAPPER_EXPORTS
#  define MACNOTIFYAPI __attribute__((visibility("default")))
#else
#  define MACNOTIFYAPI
#endif

/* -------------------------------------------------------------------------
 * Callback types — fired on a background thread from UNUserNotificationCenter.
 * None of the callbacks should call back into MNW_* synchronously.
 * ------------------------------------------------------------------------- */
typedef void (*MNW_ActivatedCallback)      (int64_t notifId);
typedef void (*MNW_ButtonActivatedCallback)(int64_t notifId, int buttonIndex);
typedef void (*MNW_DismissedCallback)      (int64_t notifId, int reason);
typedef void (*MNW_FailedCallback)         (int64_t notifId);

/* -------------------------------------------------------------------------
 * Dismiss reasons (passed to MNW_DismissedCallback)
 * ------------------------------------------------------------------------- */
#define MNW_DISMISS_EXPIRED      0  /* Notification auto-expired (note: macOS does not fire this) */
#define MNW_DISMISS_USER         1  /* User swiped or clicked "Close" */
#define MNW_DISMISS_APP_REMOVED  2  /* Removed programmatically via MNW_HideNotification */

/* -------------------------------------------------------------------------
 * Audio options
 * ------------------------------------------------------------------------- */
#define MNW_AUDIO_DEFAULT  0
#define MNW_AUDIO_SILENT   1

/* -------------------------------------------------------------------------
 * Interruption level (macOS 12+; silently ignored on earlier versions)
 * ------------------------------------------------------------------------- */
#define MNW_INTERRUPTION_ACTIVE          0
#define MNW_INTERRUPTION_PASSIVE         1
#define MNW_INTERRUPTION_TIME_SENSITIVE  2
#define MNW_INTERRUPTION_CRITICAL        3

/* -------------------------------------------------------------------------
 * Dock-tile progress states (passed to MNW_SetTaskbarProgress)
 * ------------------------------------------------------------------------- */
#define MNW_PROGRESS_NONE           0  /* Clear the progress bar */
#define MNW_PROGRESS_INDETERMINATE  1  /* Animated bar with no specific value */
#define MNW_PROGRESS_NORMAL         2  /* Determinate bar at `fraction` */
#define MNW_PROGRESS_PAUSED         3  /* Same visual as NORMAL (Dock cannot tint) */
#define MNW_PROGRESS_ERROR          4  /* Same visual as NORMAL (Dock cannot tint) */

/* -------------------------------------------------------------------------
 * Handler — bundle of four callback function pointers, copied by value.
 * Any pointer may be NULL to opt out of that event.
 * ------------------------------------------------------------------------- */
typedef struct {
    MNW_ActivatedCallback       onActivated;
    MNW_ButtonActivatedCallback onButtonActivated;
    MNW_DismissedCallback       onDismissed;
    MNW_FailedCallback          onFailed;
} MNW_Handler;

/* -------------------------------------------------------------------------
 * Notification descriptor.
 * All pointer fields may be NULL / empty string where documented.
 * The caller must keep all pointed-to memory valid until MNW_ShowNotification returns;
 * the implementation deep-copies every string before returning.
 * ------------------------------------------------------------------------- */
typedef struct {
    const char*  title;           /* Required, non-empty UTF-8 string */
    const char*  body;            /* Optional body text; NULL or "" → omitted */
    const char*  imagePath;       /* Optional absolute path to an image file */
    const char** buttonLabels;    /* Optional array of buttonCount UTF-8 strings */
    int          buttonCount;     /* 0–5 */
    int64_t      expirationMs;    /* Reserved — UNUserNotificationCenter has no per-notification timeout API */
    int          audioOption;     /* MNW_AUDIO_* */
    int          interruptionLevel; /* MNW_INTERRUPTION_* */
} MNW_NotificationDescriptor;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * Returns true if UNUserNotificationCenter is available (macOS 10.14+).
 * Safe to call before MNW_Initialize.
 */
MACNOTIFYAPI bool MNW_IsSupported(void);

/**
 * Initialises the notification centre and requests authorisation (alert + sound + badge).
 * Blocks until the user grants or denies the authorisation prompt (up to 30 s).
 * Returns true if authorisation was granted; false if denied or unavailable.
 * Safe to call multiple times; subsequent calls only re-check authorisation status.
 */
MACNOTIFYAPI bool MNW_Initialize(const char* appName);

/**
 * Removes all pending and delivered notifications posted by this process and frees
 * all internal state. Call once before the process exits.
 */
MACNOTIFYAPI void MNW_Uninitialize(void);

/**
 * Posts a notification. Returns a positive opaque int64 identifier on success,
 * or a negative value if the descriptor is invalid or the library is not initialised.
 * The MNW_Handler callbacks will fire asynchronously from a background thread.
 */
MACNOTIFYAPI int64_t MNW_ShowNotification(
    const MNW_NotificationDescriptor* descriptor,
    const MNW_Handler*                handler);

/**
 * Removes a pending or delivered notification by its ID.
 * Fires onDismissed(MNW_DISMISS_APP_REMOVED) synchronously before returning.
 * Returns true if the notification was found and removed.
 */
MACNOTIFYAPI bool MNW_HideNotification(int64_t notifId);

/**
 * Sets the Dock-tile progress indicator.
 *
 * @param state     One of MNW_PROGRESS_*.
 * @param fraction  Progress in the range 0.0–1.0 (used only when state is
 *                  MNW_PROGRESS_NORMAL / PAUSED / ERROR; ignored otherwise).
 *
 * The work is dispatched asynchronously onto the main thread because AppKit/Dock
 * APIs are main-thread-only. It is therefore only effective for a regular GUI
 * application whose main run loop is running and which owns a Dock tile; a bare
 * console process has no Dock tile and the call is a harmless no-op.
 * Safe to call before MNW_Initialize (it does not depend on notification state).
 */
MACNOTIFYAPI void MNW_SetTaskbarProgress(int state, double fraction);

/* -------------------------------------------------------------------------
 * Dock menu (jump-list equivalent)
 *
 * Adds custom items to the application's Dock menu (shown on right-click / click-and-hold of
 * the Dock icon). Unlike Windows jump lists / Linux .desktop actions, Dock-menu items fire a
 * live in-process callback — there is no relaunch.
 *
 * Like the Dock-tile progress API these are only effective for a regular (bundled) GUI
 * application with a running main loop; a bare console process has no Dock menu and the calls
 * are harmless no-ops. The wrapper provides the menu via the application delegate's
 * -applicationDockMenu:, installing its own delegate if the app has none, or adding the method
 * to the existing delegate's class if it does not already implement it.
 * ------------------------------------------------------------------------- */

/** Fired on the main thread when the user clicks a Dock-menu item. taskId is UTF-8. */
typedef void (*MNW_DockMenuCallback)(const char* taskId);

/** Registers the callback invoked when a Dock-menu item is clicked. Pass NULL to clear it. */
MACNOTIFYAPI void MNW_SetDockMenuHandler(MNW_DockMenuCallback callback);

/**
 * Replaces the custom Dock-menu items.
 *
 * @param ids     Array of `count` UTF-8 task ids (passed back to the callback when clicked).
 * @param titles  Array of `count` UTF-8 item labels, parallel to `ids`.
 * @param count   Number of items (0 clears the menu).
 *
 * The arrays are copied before this function returns; the caller may free them afterwards.
 * Work is dispatched onto the main thread because AppKit menus are main-thread-only.
 */
MACNOTIFYAPI void MNW_SetDockMenu(const char** ids, const char** titles, int count);

/** Removes all custom Dock-menu items. Equivalent to MNW_SetDockMenu(NULL, NULL, 0). */
MACNOTIFYAPI void MNW_ClearDockMenu(void);

#ifdef __cplusplus
}
#endif
