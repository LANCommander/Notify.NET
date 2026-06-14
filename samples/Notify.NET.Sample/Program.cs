using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.DependencyInjection;
using Notify.NET.Abstractions;
using Notify.NET.Builder;
using Notify.NET.Extensions;

// ============================================================================
// Notify.NET Sample — demonstrates fluent builder + DI registration
// ============================================================================

// --- Option A: use the factory helper directly (no DI container) ---
using var service = ServiceCollectionExtensions.CreateNotificationService(opts =>
{
    opts.AppName        = "Notify.NET Sample";
    opts.AppUserModelId = "NotifyNET.Sample";
});

Console.WriteLine($"Notification service supported: {service.IsSupported}");

if (!service.IsSupported)
{
    Console.WriteLine("Native notifications are not available on this platform. Exiting.");
    return;
}

var done = new ManualResetEventSlim(false);

// ------ 1. Simple notification (title + body) --------------------------------
Console.WriteLine("\n[1] Showing a simple notification...");

long id1 = await service.ShowAsync(
    NotificationBuilder.Create("Hello from Notify.NET")
        .WithBody("This is a simple cross-platform OS notification.")
        .OnActivated(id => Console.WriteLine($"  [1] Notification {id} activated"))
        .OnDismissed((id, reason) => Console.WriteLine($"  [1] Notification {id} dismissed: {reason}"))
        .Build());

Console.WriteLine($"  Shown with id={id1}");
await Task.Delay(3000);

// ------ 2. Notification with buttons ----------------------------------------
Console.WriteLine("\n[2] Showing a notification with action buttons...");

var buttonDone = new ManualResetEventSlim(false);

long id2 = await service.ShowAsync(
    NotificationBuilder.Create("Update Available")
        .WithBody("Version 2.0 is ready to install.")
        .AddButton("Install Now",   id => { Console.WriteLine($"  [2] Install Now clicked (id={id})"); buttonDone.Set(); })
        .AddButton("Remind Me",     id => { Console.WriteLine($"  [2] Remind Me clicked (id={id})");   buttonDone.Set(); })
        .AddButton("Skip Version",  id => { Console.WriteLine($"  [2] Skip Version clicked (id={id})"); buttonDone.Set(); })
        .OnDismissed((id, reason) => { Console.WriteLine($"  [2] Dismissed: {reason}"); buttonDone.Set(); })
        .OnFailed(id => { Console.WriteLine($"  [2] Failed for id={id}"); buttonDone.Set(); })
        .Build());

Console.WriteLine($"  Shown with id={id2}. Waiting up to 15 s for interaction...");
buttonDone.Wait(TimeSpan.FromSeconds(15));

// ------ 3. Notification with an image ----------------------------------------
Console.WriteLine("\n[3] Showing a notification with an image...");

// Provide any PNG/JPG path; the sample gracefully handles a missing file
// because the native service falls back to no image when gdk_pixbuf_new_from_file fails.
string imagePath = "image.jpg";

long id3 = -1;
try
{
    id3 = await service.ShowAsync(
        NotificationBuilder.Create("Picture Notification")
            .WithBody("This notification includes an image.")
            .WithImage(imagePath)
            .WithUrgency(NotificationUrgency.Low)
            .OnActivated(id => Console.WriteLine($"  [3] Activated id={id}"))
            .Build());
    Console.WriteLine($"  Shown with id={id3}");
}
catch (Exception ex)
{
    Console.WriteLine($"  [3] Failed: {ex.GetType().Name}: {ex.Message}");
}
await Task.Delay(3000);

// ------ 4. Programmatic dismiss ----------------------------------------------
Console.WriteLine("\n[4] Showing a notification and dismissing it programmatically after 2 s...");

long id4 = await service.ShowAsync(
    NotificationBuilder.Create("I will disappear in 2 seconds")
        .WithBody("Programmatically dismissed.")
        .OnDismissed((id, reason) => Console.WriteLine($"  [4] Dismissed: {reason}"))
        .Build());

Console.WriteLine($"  Shown with id={id4}. Hiding in 2 s...");
await Task.Delay(2000);
await service.HideAsync(id4);
Console.WriteLine("  Hidden.");

// ------ 5. Interface-based handler -------------------------------------------
Console.WriteLine("\n[5] Showing a notification using INotificationHandler...");

long id5 = await service.ShowAsync(
    NotificationBuilder.Create("Handler-based Notification")
        .WithBody("Uses a custom INotificationHandler implementation.")
        .AddButton("Acknowledge", null)
        .WithHandler(new SampleHandler())
        .Build());

Console.WriteLine($"  Shown with id={id5}. Waiting 10 s...");
await Task.Delay(10_000);

// ------ 6. DI container usage ------------------------------------------------
Console.WriteLine("\n[6] Demonstrating DI container registration...");

var services = new ServiceCollection();
services.AddNotifications(opts =>
{
    opts.AppName        = "Notify.NET Sample (DI)";
    opts.AppUserModelId = "NotifyNET.Sample.DI";
});
services.AddTaskbarProgress(opts =>
{
    opts.AppName       = "Notify.NET Sample (DI)";
    opts.DesktopFileId = "NotifyNET.Sample.DI"; // Linux: matches NotifyNET.Sample.DI.desktop
});

await using var provider = services.BuildServiceProvider();
var diService     = provider.GetRequiredService<INotificationService>();
var diTaskbar     = provider.GetRequiredService<ITaskbarProgressService>();

long id6 = await diService.ShowAsync(
    NotificationBuilder.Create("DI-registered Service")
        .WithBody("This notification was shown via an IServiceProvider-resolved service.")
        .Build());

Console.WriteLine($"  Shown with id={id6}");
await Task.Delay(3000);

// A realistic combined flow: drive the taskbar progress bar while a long-running
// job runs, then fire a completion notification when it finishes.
Console.WriteLine($"  Taskbar progress supported: {diTaskbar.IsSupported}");

if (diTaskbar.IsSupported)
{
    Console.WriteLine("  Simulating a download with live taskbar progress...");
    const int totalBytes = 100;
    for (int sent = 0; sent <= totalBytes; sent += 10)
    {
        diTaskbar.SetProgress((ulong)sent, (ulong)totalBytes);
        await Task.Delay(300);
    }

    await diService.ShowAsync(
        NotificationBuilder.Create("Download complete")
            .WithBody("All 100 bytes transferred.")
            .Build());

    diTaskbar.SetState(TaskbarProgressState.None); // clear the bar
}

// ------ 7. Taskbar progress via the factory (no DI) --------------------------
Console.WriteLine("\n[7] Demonstrating taskbar progress states (factory helper)...");

using var taskbar = ServiceCollectionExtensions.CreateTaskbarProgressService(opts =>
{
    opts.AppName       = "Notify.NET Sample";
    opts.DesktopFileId = "NotifyNET.Sample"; // Linux: matches NotifyNET.Sample.desktop
});

Console.WriteLine($"  Taskbar progress supported: {taskbar.IsSupported}");

if (taskbar.IsSupported)
{
    // On Windows this drives the terminal's taskbar button: ITaskbarList3 for the classic
    // console host, and the OSC 9;4 escape sequence for Windows Terminal (the Win11 default,
    // where the app runs under a ConPTY and ITaskbarList3 has no visible button). For a
    // WPF/WinForms app, call taskbar.SetWindow(mainWindowHandle) first to target its window.
    Console.WriteLine("  Normal bar at 60%...");
    taskbar.SetProgress(0.60);
    await Task.Delay(1500);

    Console.WriteLine("  Paused state...");
    taskbar.SetState(TaskbarProgressState.Paused);
    await Task.Delay(1500);

    Console.WriteLine("  Error state...");
    taskbar.SetState(TaskbarProgressState.Error);
    await Task.Delay(1500);

    Console.WriteLine("  Indeterminate state...");
    taskbar.SetState(TaskbarProgressState.Indeterminate);
    await Task.Delay(1500);

    Console.WriteLine("  Clearing progress.");
    taskbar.SetState(TaskbarProgressState.None);
}

Console.WriteLine("\nAll done.");

// ============================================================================
// Sample INotificationHandler implementation
// ============================================================================

sealed class SampleHandler : INotificationHandler
{
    public void OnActivated(long id)
        => Console.WriteLine($"  [5] SampleHandler.OnActivated(id={id})");

    public void OnButtonActivated(long id, int buttonIndex)
        => Console.WriteLine($"  [5] SampleHandler.OnButtonActivated(id={id}, button={buttonIndex})");

    public void OnDismissed(long id, DismissReason reason)
        => Console.WriteLine($"  [5] SampleHandler.OnDismissed(id={id}, reason={reason})");

    public void OnFailed(long id)
        => Console.WriteLine($"  [5] SampleHandler.OnFailed(id={id})");
}
