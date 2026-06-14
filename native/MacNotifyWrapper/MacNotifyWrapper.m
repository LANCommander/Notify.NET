/**
 * MacNotifyWrapper.m
 *
 * Objective-C implementation of the flat C API declared in MacNotifyWrapper.h.
 * Wraps UNUserNotificationCenter (macOS 10.14+).
 *
 * Compilation requirements:
 *   clang -fobjc-arc -fvisibility=hidden
 *   Frameworks: Foundation, UserNotifications
 *   Minimum deployment target: macOS 10.14 (for x86_64), macOS 11.0 (for arm64)
 *
 * Threading model:
 *   MNW_Initialize: blocks on a semaphore waiting for the authorisation dialog.
 *   MNW_ShowNotification / MNW_HideNotification: thread-safe; UNUserNotificationCenter
 *     internally serialises requests.
 *   Callbacks (onActivated, onButtonActivated, onDismissed, onFailed): invoked on a
 *     background GCD thread managed by UNUserNotificationCenter. Callers must not
 *     call back into MNW_* synchronously from these callbacks.
 */

#define MACNOTIFYWRAPPER_EXPORTS
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "MacNotifyWrapper.h"

/* -------------------------------------------------------------------------
 * Per-notification heap state
 * ------------------------------------------------------------------------- */

typedef struct {
    int64_t     notifId;
    MNW_Handler handler;
} NotifState;

/* -------------------------------------------------------------------------
 * Delegate
 * ------------------------------------------------------------------------- */

@interface MNWDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

/* -------------------------------------------------------------------------
 * Process-lifetime globals
 * ------------------------------------------------------------------------- */

static MNWDelegate*                              g_delegate       = nil;
static NSLock*                                   g_lock           = nil;
/* strId → NSValue wrapping NotifState* (heap-allocated) */
static NSMutableDictionary<NSString*, NSValue*>* g_entries        = nil;
/* int64 NSNumber → strId NSString */
static NSMutableDictionary<NSNumber*, NSString*>* g_idMap         = nil;
/* category identifiers we have registered */
static NSMutableSet<NSString*>*                  g_categoryIds    = nil;
/* UNNotificationCategory objects corresponding to the above */
static NSMutableSet<UNNotificationCategory*>*    g_categoryObjs   = nil;
static _Atomic(int64_t)                          g_counter        = 1;
static bool                                      g_initialized    = false;

/* -------------------------------------------------------------------------
 * Private helpers
 * ------------------------------------------------------------------------- */

static NSString* StringIdFor(int64_t notifId)
{
    return [NSString stringWithFormat:@"mnw_%lld", (long long)notifId];
}

/* Stores a newly allocated NotifState in both lookup tables. */
static void StoreEntry(NSString* strId, int64_t notifId, const MNW_Handler* handler)
{
    NotifState* s = (NotifState*)malloc(sizeof(NotifState));
    s->notifId  = notifId;
    s->handler  = *handler;
    NSValue* boxed = [NSValue valueWithPointer:s];
    [g_lock lock];
    g_entries[strId]     = boxed;
    g_idMap[@(notifId)]  = strId;
    [g_lock unlock];
}

/*
 * Atomically removes the entry and returns a copy of its state.
 * Returns NO if no entry exists (already removed by a prior event).
 */
static BOOL TakeEntry(NSString* strId, NotifState* outState)
{
    [g_lock lock];
    NSValue* val = g_entries[strId];
    if (!val) { [g_lock unlock]; return NO; }

    NotifState* s = (NotifState*)[val pointerValue];
    *outState = *s;
    free(s);
    [g_entries removeObjectForKey:strId];
    [g_idMap removeObjectForKey:@(outState->notifId)];
    [g_lock unlock];
    return YES;
}

/*
 * Builds a stable, reproducible category identifier from a set of button labels.
 * The identifier must survive app restarts so that previously delivered notifications
 * whose category was registered in an earlier session are still actionable.
 */
static NSString* BuildCategoryId(const char** labels, int count)
{
    if (count == 0) return @"mnw_default";

    NSMutableString* s = [NSMutableString stringWithString:@"mnw"];
    for (int i = 0; i < count; i++)
        [s appendFormat:@"_%s", labels[i]];

    /* Hash long identifiers to keep the string short. */
    if (s.length > 80)
        return [NSString stringWithFormat:@"mnw_%lu", (unsigned long)s.hash];

    return [s copy];
}

/*
 * Ensures a UNNotificationCategory with the given identifier is registered.
 * No-op if the category was already registered this session.
 * Thread-safe — uses g_lock.
 */
static NSString* EnsureCategory(const char** labels, int count)
{
    NSString* catId = BuildCategoryId(labels, count);

    [g_lock lock];
    BOOL known = [g_categoryIds containsObject:catId];
    [g_lock unlock];

    if (known) return catId;

    /* Build the UNNotificationAction array. */
    NSMutableArray<UNNotificationAction*>* actions = [NSMutableArray array];
    for (int i = 0; i < count; i++) {
        NSString* actionId = [NSString stringWithFormat:@"btn_%d", i];
        NSString* title    = [NSString stringWithUTF8String:labels[i]];
        [actions addObject:[UNNotificationAction
            actionWithIdentifier:actionId
            title:title
            options:UNNotificationActionOptionNone]];
    }

    /* CustomDismissAction makes the delegate receive dismiss events. */
    UNNotificationCategory* category = [UNNotificationCategory
        categoryWithIdentifier:catId
        actions:actions
        intentIdentifiers:@[]
        options:UNNotificationCategoryOptionCustomDismissAction];

    [g_lock lock];
    if (![g_categoryIds containsObject:catId]) {
        [g_categoryIds addObject:catId];
        [g_categoryObjs addObject:category];
        /* setNotificationCategories replaces the full set; pass our accumulated set. */
        NSSet* snapshot = [g_categoryObjs copy];
        [[UNUserNotificationCenter currentNotificationCenter]
            setNotificationCategories:snapshot];
    }
    [g_lock unlock];

    return catId;
}

/* -------------------------------------------------------------------------
 * Delegate implementation
 * ------------------------------------------------------------------------- */

@implementation MNWDelegate

/*
 * Called when a notification arrives while the app is in the foreground.
 * Show the banner and play the sound so that console/background processes
 * still see the notification visually.
 */
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void(^)(UNNotificationPresentationOptions))completionHandler
{
    if (@available(macOS 12.0, *)) {
        completionHandler(UNNotificationPresentationOptionBanner
                        | UNNotificationPresentationOptionSound
                        | UNNotificationPresentationOptionList);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        completionHandler(UNNotificationPresentationOptionAlert
                        | UNNotificationPresentationOptionSound);
#pragma clang diagnostic pop
    }
}

/*
 * Called when the user interacts with (or dismisses) a notification.
 * This is the single delivery point for all notification responses;
 * we route to the appropriate managed callback and then clean up.
 *
 * Note: on macOS, a body-tap or button-tap IS the terminal event.
 * UNUserNotificationCenter does NOT subsequently fire a dismiss event
 * after an action response. We therefore release after every interaction.
 */
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
   didReceiveNotificationResponse:(UNNotificationResponse*)response
            withCompletionHandler:(void(^)(void))completionHandler
{
    NSString* strId = response.notification.request.identifier;

    NotifState state = {0};
    if (!TakeEntry(strId, &state)) {
        /* Already handled (e.g. MNW_HideNotification was called first). */
        completionHandler();
        return;
    }

    NSString* actionId = response.actionIdentifier;

    if ([actionId isEqualToString:UNNotificationDefaultActionIdentifier]) {
        /* User tapped the notification body. */
        if (state.handler.onActivated)
            state.handler.onActivated(state.notifId);

    } else if ([actionId isEqualToString:UNNotificationDismissActionIdentifier]) {
        /* User dismissed (swipe/close). Requires CustomDismissAction on category. */
        if (state.handler.onDismissed)
            state.handler.onDismissed(state.notifId, MNW_DISMISS_USER);

    } else if ([actionId hasPrefix:@"btn_"]) {
        /* User tapped an action button. */
        int idx = (int)[[actionId substringFromIndex:4] integerValue];
        if (state.handler.onButtonActivated)
            state.handler.onButtonActivated(state.notifId, idx);

    } else {
        /* Unknown action — treat as activation. */
        if (state.handler.onActivated)
            state.handler.onActivated(state.notifId);
    }

    completionHandler();
}

@end

/* -------------------------------------------------------------------------
 * API implementation
 * ------------------------------------------------------------------------- */

bool MNW_IsSupported(void)
{
    if (@available(macOS 10.14, *)) return true;
    return false;
}

bool MNW_Initialize(const char* appName)
{
    (void)appName; /* appName is informational; the bundle identifier governs delivery. */

    if (!MNW_IsSupported()) return false;

    /* One-time setup of global state. */
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        g_lock         = [[NSLock alloc] init];
        g_entries      = [NSMutableDictionary dictionary];
        g_idMap        = [NSMutableDictionary dictionary];
        g_categoryIds  = [NSMutableSet set];
        g_categoryObjs = [NSMutableSet set];
        g_delegate     = [[MNWDelegate alloc] init];
        [[UNUserNotificationCenter currentNotificationCenter] setDelegate:g_delegate];

        /* Pre-register the default (no-button) category. */
        EnsureCategory(NULL, 0);
    });

    if (g_initialized) {
        /* On re-initialisation just re-check authorisation status. */
        __block bool ok = false;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [[UNUserNotificationCenter currentNotificationCenter]
            getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* s) {
                ok = (s.authorizationStatus == UNAuthorizationStatusAuthorized
                   || s.authorizationStatus == UNAuthorizationStatusProvisional);
                dispatch_semaphore_signal(sem);
            }];
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
        return ok;
    }

    /* Request authorisation. Blocks until the user responds (or times out). */
    __block bool granted = false;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [[UNUserNotificationCenter currentNotificationCenter]
        requestAuthorizationWithOptions:(UNAuthorizationOptionAlert
                                       | UNAuthorizationOptionSound
                                       | UNAuthorizationOptionBadge)
                      completionHandler:^(BOOL g, NSError* __unused err) {
            granted = (bool)g;
            dispatch_semaphore_signal(sem);
        }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));

    g_initialized = granted;
    return granted;
}

void MNW_Uninitialize(void)
{
    if (!g_lock) return;

    /* Remove all notifications posted by this process. */
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    [center removeAllPendingNotificationRequests];
    [center removeAllDeliveredNotifications];

    /* Free all live state without firing callbacks. */
    [g_lock lock];
    for (NSValue* val in g_entries.allValues) {
        NotifState* s = (NotifState*)[val pointerValue];
        free(s);
    }
    [g_entries removeAllObjects];
    [g_idMap removeAllObjects];
    [g_lock unlock];

    g_initialized = false;
}

int64_t MNW_ShowNotification(
    const MNW_NotificationDescriptor* descriptor,
    const MNW_Handler*                handler)
{
    if (!descriptor || !handler)                          return -1;
    if (!descriptor->title || descriptor->title[0]=='\0') return -2;
    if (!g_lock)                                          return -3; /* Not initialised */

    int64_t notifId = atomic_fetch_add_explicit(&g_counter, 1, memory_order_relaxed);
    NSString* strId = StringIdFor(notifId);

    /* --- Content --------------------------------------------------------- */
    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
    content.title = [NSString stringWithUTF8String:descriptor->title];

    if (descriptor->body && descriptor->body[0] != '\0')
        content.body = [NSString stringWithUTF8String:descriptor->body];

    /* --- Sound ----------------------------------------------------------- */
    content.sound = (descriptor->audioOption == MNW_AUDIO_SILENT)
        ? nil
        : [UNNotificationSound defaultSound];

    /* --- Interruption level (macOS 12+) ---------------------------------- */
    if (@available(macOS 12.0, *)) {
        switch (descriptor->interruptionLevel) {
            case MNW_INTERRUPTION_PASSIVE:
                content.interruptionLevel = UNNotificationInterruptionLevelPassive;
                break;
            case MNW_INTERRUPTION_TIME_SENSITIVE:
                content.interruptionLevel = UNNotificationInterruptionLevelTimeSensitive;
                break;
            case MNW_INTERRUPTION_CRITICAL:
                content.interruptionLevel = UNNotificationInterruptionLevelCritical;
                break;
            default:
                content.interruptionLevel = UNNotificationInterruptionLevelActive;
                break;
        }
    }

    /* --- Image attachment ------------------------------------------------ */
    if (descriptor->imagePath && descriptor->imagePath[0] != '\0') {
        NSString* path = [NSString stringWithUTF8String:descriptor->imagePath];
        NSURL*    url  = [NSURL fileURLWithPath:path];
        NSError*  err  = nil;
        UNNotificationAttachment* att = [UNNotificationAttachment
            attachmentWithIdentifier:@"image" URL:url options:nil error:&err];
        if (att)
            content.attachments = @[att];
        /* If attachment fails (file missing, unsupported format), continue without it. */
    }

    /* --- Category / buttons --------------------------------------------- */
    content.categoryIdentifier = EnsureCategory(descriptor->buttonLabels,
                                                descriptor->buttonCount);

    /* --- Trigger --------------------------------------------------------- */
    /*
     * UNTimeIntervalNotificationTrigger requires timeInterval > 0.
     * Use 0.1 s for "fire immediately" — imperceptible to the user.
     */
    UNTimeIntervalNotificationTrigger* trigger =
        [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:0.1 repeats:NO];

    /* --- Register state BEFORE scheduling so no callback is missed ------- */
    StoreEntry(strId, notifId, handler);

    /* --- Schedule -------------------------------------------------------- */
    UNNotificationRequest* req = [UNNotificationRequest
        requestWithIdentifier:strId
        content:content
        trigger:trigger];

    [[UNUserNotificationCenter currentNotificationCenter]
        addNotificationRequest:req
        withCompletionHandler:^(NSError* error) {
            if (!error) return;
            /* Delivery failed — fire onFailed and clean up. */
            NotifState state = {0};
            if (TakeEntry(strId, &state)) {
                if (state.handler.onFailed)
                    state.handler.onFailed(state.notifId);
            }
        }];

    return notifId;
}

bool MNW_HideNotification(int64_t notifId)
{
    if (!g_lock) return false;

    [g_lock lock];
    NSString* strId = g_idMap[@(notifId)];
    [g_lock unlock];

    if (!strId) return false;

    NSArray<NSString*>* ids = @[strId];
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    [center removePendingNotificationRequestsWithIdentifiers:ids];
    [center removeDeliveredNotificationsWithIdentifiers:ids];

    /* Fire dismissed callback synchronously before returning. */
    NotifState state = {0};
    if (TakeEntry(strId, &state)) {
        if (state.handler.onDismissed)
            state.handler.onDismissed(state.notifId, MNW_DISMISS_APP_REMOVED);
    }

    return true;
}

/* -------------------------------------------------------------------------
 * Dock-tile progress
 *
 * AppKit Dock APIs are main-thread-only, so all work is dispatched onto the
 * main queue. The custom content view draws the application icon with an
 * NSProgressIndicator overlaid along the bottom edge.
 * ------------------------------------------------------------------------- */

/* Accessed only on the main thread (inside the dispatched block). */
static NSImageView*        g_dockImageView = nil;
static NSProgressIndicator* g_dockProgress = nil;

static void EnsureDockViews(NSDockTile* tile)
{
    if (g_dockImageView) return;

    NSImageView* iconView = [[NSImageView alloc]
        initWithFrame:NSMakeRect(0, 0, tile.size.width, tile.size.height)];
    iconView.image = [NSApp applicationIconImage];

    NSProgressIndicator* bar = [[NSProgressIndicator alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, tile.size.width, 12.0)];
    bar.style          = NSProgressIndicatorStyleBar;
    bar.indeterminate  = NO;
    bar.minValue       = 0.0;
    bar.maxValue       = 1.0;
    [iconView addSubview:bar];

    tile.contentView = iconView;
    g_dockImageView  = iconView;
    g_dockProgress   = bar;
}

void MNW_SetTaskbarProgress(int state, double fraction)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NSApplication* app  = [NSApplication sharedApplication];
        NSDockTile*    tile = [app dockTile];

        if (state == MNW_PROGRESS_NONE) {
            if (g_dockProgress) [g_dockProgress stopAnimation:nil];
            tile.contentView = nil;
            g_dockImageView  = nil;
            g_dockProgress   = nil;
            [tile display];
            return;
        }

        EnsureDockViews(tile);

        if (state == MNW_PROGRESS_INDETERMINATE) {
            g_dockProgress.indeterminate = YES;
            [g_dockProgress startAnimation:nil];
        } else {
            [g_dockProgress stopAnimation:nil];
            g_dockProgress.indeterminate = NO;
            double clamped = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
            g_dockProgress.doubleValue = clamped;
        }

        g_dockProgress.hidden = NO;
        [tile display];
    });
}
