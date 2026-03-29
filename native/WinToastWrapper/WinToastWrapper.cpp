/**
 * WinToastWrapper.cpp
 *
 * Implementation of the flat C API declared in WinToastWrapper.h.
 * Links against WinToastLib (wintoastlib.h / wintoastlib.cpp from mohabouje/WinToast).
 *
 * Compilation requirements:
 *   - MSVC 2019 or later, C++17
 *   - /W3 /EHsc /MT (static CRT to avoid runtime dependency)
 *   - Link: Ole32.lib Shlwapi.lib Shell32.lib
 *   - _WIN32_WINNT >= 0x0602 (Windows 8)
 *   - WINTOASTWRAPPER_EXPORTS defined in the DLL project
 *
 * Threading model:
 *   WNT_Initialize and WNT_ShowToast MUST be called from an STA thread.
 *   Callbacks fire on a WinRT thread-pool thread — they must not call back into
 *   WNT_ShowToast or WNT_HideToast directly; the .NET side re-queues on the STA.
 */

#define WINTOASTWRAPPER_EXPORTS
#define NOMINMAX
#include <Windows.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include "WinToastWrapper.h"
#include "vendor/wintoastlib.h"

// WinToast is a class inside the WinToastLib namespace — not a nested namespace.
// "using namespace WinToastLib" brings WinToast, WinToastTemplate, IWinToastHandler,
// WinToastError, etc. into the global scope.
using namespace WinToastLib;

/* -------------------------------------------------------------------------
 * Handler implementation
 * -------------------------------------------------------------------------
 * WinToastLib wraps the IWinToastHandler* in a std::shared_ptr<IWinToastHandler>
 * at the start of showToast (wintoastlib.cpp line ~721), adopting ownership.
 * WinToastLib will delete the handler when the shared_ptr is destroyed.
 * We must NEVER delete a handler ourselves — doing so would cause a double-free.
 *
 * g_handlers is kept only for lookup (e.g. WNT_HideToast needs the handler for
 * the toast-ID-to-handler mapping that WinToastLib itself tracks). Entries are
 * removed from g_handlers when a toast ends, but the pointed-to object is never
 * freed here.
 * ------------------------------------------------------------------------- */

class WinToastHandlerImpl : public IWinToastHandler
{
public:
    INT64       m_toastId;
    WNT_Handler m_handler; // copied by value

    WinToastHandlerImpl(INT64 toastId, const WNT_Handler& handler)
        : m_toastId(toastId), m_handler(handler) {}

    // WinToastDismissalReason is a nested enum of IWinToastHandler.
    // It is accessible by unqualified name within this derived class.

    void toastActivated() const override
    {
        if (m_handler.onActivated)
            m_handler.onActivated(m_toastId);
    }

    void toastActivated(int actionIndex) const override
    {
        if (m_handler.onButtonActivated)
            m_handler.onButtonActivated(m_toastId, actionIndex);
    }

    // Input response (text box) — treat as a plain activation for our purposes.
    void toastActivated(std::wstring /*response*/) const override
    {
        if (m_handler.onActivated)
            m_handler.onActivated(m_toastId);
    }

    void toastDismissed(WinToastDismissalReason state) const override
    {
        if (m_handler.onDismissed)
        {
            int reason = 0;
            switch (state)
            {
                case WinToastDismissalReason::UserCanceled:      reason = 0; break;
                case WinToastDismissalReason::ApplicationHidden: reason = 1; break;
                case WinToastDismissalReason::TimedOut:          reason = 2; break;
            }
            m_handler.onDismissed(m_toastId, reason);
        }
        ScheduleDelete(m_toastId);
    }

    void toastFailed() const override
    {
        if (m_handler.onFailed)
            m_handler.onFailed(m_toastId);
        ScheduleDelete(m_toastId);
    }

private:
    // Removes this toast from the lookup table when its lifecycle ends.
    // Does NOT delete the handler — WinToastLib owns it via shared_ptr.
    static void ScheduleDelete(INT64 toastId);
};

/* -------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

static std::mutex                                       g_mutex;
static std::unordered_map<INT64, WinToastHandlerImpl*> g_handlers;  // live toasts (no ownership)

// Removes the entry for toastId from g_handlers.
// Does NOT delete the handler — WinToastLib owns it via shared_ptr.
void WinToastHandlerImpl::ScheduleDelete(INT64 toastId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_handlers.erase(toastId);
}

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static WinToastTemplate::WinToastTemplateType SelectTemplateType(
    bool hasImage, bool hasBody)
{
    if (hasImage)
        return hasBody
            ? WinToastTemplate::ImageAndText02   // image + title + body
            : WinToastTemplate::ImageAndText01;  // image + title only

    return hasBody
        ? WinToastTemplate::Text02               // title + body
        : WinToastTemplate::Text01;              // title only
}

// ---------------------------------------------------------------------------
// SEH-safe wrappers for WinToastLib calls that can trigger AVs from WinRT.
//
// MSVC rule: __try cannot appear in a function that has local objects with
// destructors (WinToastTemplate, std::lock_guard, std::wstring, etc.).
// These helpers contain ONLY plain scalars and raw pointers so __try is legal.
// Any structured exception (AV, invalid handle, COM fault) is caught here and
// converted to an error-code return, preventing it from ever reaching the CLR
// where a corrupted-state exception would terminate the process.
// ---------------------------------------------------------------------------

// Calls WinToast::showToast with full SEH protection.
// tmpl is passed as a const pointer to avoid any destructor in this frame.
static INT64 SafeShowToast(
    const WinToastTemplate*      tmpl,
    IWinToastHandler*            handler,
    WinToast::WinToastError*     error)
{
    INT64 result = static_cast<INT64>(WinToast::WinToastError::UnknownError);
    __try
    {
        result = WinToast::instance()->showToast(*tmpl, handler, error);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Structured exception (AV, etc.) from WinRT internals — return failure.
        result = static_cast<INT64>(WinToast::WinToastError::UnknownError);
    }
    return result;
}

// Calls WinToast::hideToast with full SEH protection.
static BOOL SafeHideToast(INT64 toastId)
{
    BOOL result = FALSE;
    __try
    {
        result = WinToast::instance()->hideToast(toastId) ? TRUE : FALSE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = FALSE;
    }
    return result;
}

/* -------------------------------------------------------------------------
 * Exported API
 * ------------------------------------------------------------------------- */

extern "C" {

NOTIFYAPI BOOL WNT_IsCompatible(void)
{
    return WinToast::isCompatible() ? TRUE : FALSE;
}

NOTIFYAPI BOOL WNT_Initialize(const wchar_t* appName, const wchar_t* appUserModelId)
{
    if (!WinToast::isCompatible())
        return FALSE;

    WinToast* instance = WinToast::instance();
    instance->setAppName(appName);
    instance->setAppUserModelId(appUserModelId);

    // SHORTCUT_POLICY_REQUIRE_CREATE: create a Start-Menu shortcut with this AUMI
    // automatically if one does not already exist. Required for unpackaged (Win32)
    // apps so that toasts persist in the Action Centre between sessions.
    instance->setShortcutPolicy(WinToast::SHORTCUT_POLICY_REQUIRE_CREATE);

    WinToast::WinToastError error = WinToast::WinToastError::NoError;
    if (!instance->initialize(&error))
        return FALSE;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // Clear lookup table from a previous Initialize/Uninitialize cycle.
        // Do NOT delete handlers — WinToastLib owns them via shared_ptr.
        g_handlers.clear();
    }

    return TRUE;
}

NOTIFYAPI void WNT_Uninitialize(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    // Do NOT delete handlers — WinToastLib owns them via shared_ptr.
    g_handlers.clear();
}

NOTIFYAPI INT64 WNT_ShowToast(
    const WNT_ToastDescriptor* descriptor,
    const WNT_Handler*         handler)
{
    if (!descriptor || !handler)
        return static_cast<INT64>(WinToast::WinToastError::InvalidParameters);

    bool hasBody  = descriptor->body      != nullptr && descriptor->body[0]      != L'\0';
    bool hasImage = descriptor->imagePath != nullptr && descriptor->imagePath[0]  != L'\0';

    WinToastTemplate tmpl(SelectTemplateType(hasImage, hasBody));

    tmpl.setTextField(descriptor->title, WinToastTemplate::FirstLine);
    if (hasBody)
        tmpl.setTextField(descriptor->body, WinToastTemplate::SecondLine);

    if (hasImage)
        tmpl.setImagePath(descriptor->imagePath);

    for (int i = 0; i < descriptor->buttonCount; ++i)
    {
        if (descriptor->buttonLabels && descriptor->buttonLabels[i])
            tmpl.addAction(descriptor->buttonLabels[i]);
    }

    if (descriptor->expirationMs > 0)
        tmpl.setExpiration(descriptor->expirationMs);

    switch (descriptor->audioOption)
    {
        case WNT_AUDIO_SILENT:
            tmpl.setAudioOption(WinToastTemplate::AudioOption::Silent);
            break;
        case WNT_AUDIO_LOOP:
            tmpl.setAudioOption(WinToastTemplate::AudioOption::Loop);
            break;
        default:
            tmpl.setAudioOption(WinToastTemplate::AudioOption::Default);
            break;
    }

    switch (descriptor->scenario)
    {
        case WNT_SCENARIO_ALARM:
            tmpl.setScenario(WinToastTemplate::Scenario::Alarm);
            break;
        case WNT_SCENARIO_REMINDER:
            tmpl.setScenario(WinToastTemplate::Scenario::Reminder);
            break;
        case WNT_SCENARIO_INCOMING_CALL:
            tmpl.setScenario(WinToastTemplate::Scenario::IncomingCall);
            break;
        default:
            tmpl.setScenario(WinToastTemplate::Scenario::Default);
            break;
    }

    // Allocate the handler. Use nothrow so a failed allocation returns an error
    // code rather than throwing std::bad_alloc through the extern "C" boundary.
    auto* handlerImpl = new (std::nothrow) WinToastHandlerImpl(0 /* filled in below */, *handler);
    if (!handlerImpl)
        return static_cast<INT64>(WinToast::WinToastError::UnknownError);

    WinToast::WinToastError error = WinToast::WinToastError::NoError;
    INT64 toastId = SafeShowToast(&tmpl, handlerImpl, &error);

    if (toastId < 0)
    {
        // showToast failed — WinToastLib created a shared_ptr internally which
        // will delete handlerImpl when it destructs. Do NOT delete it here.
        return toastId; // already the error code cast from WinToastError
    }

    handlerImpl->m_toastId = toastId;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_handlers[toastId] = handlerImpl;
    }

    return toastId;
}

NOTIFYAPI BOOL WNT_HideToast(INT64 toastId)
{
    BOOL ok = SafeHideToast(toastId);

    if (ok)
    {
        // hideToast may fire the ApplicationHidden dismissal callback synchronously,
        // which calls ScheduleDelete and removes from g_handlers. Erase here too in
        // case the callback was skipped. Do NOT delete — WinToastLib owns the handler.
        std::lock_guard<std::mutex> lock(g_mutex);
        g_handlers.erase(toastId);
    }

    return ok ? TRUE : FALSE;
}

} /* extern "C" */
