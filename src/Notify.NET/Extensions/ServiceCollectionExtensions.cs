using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.DependencyInjection;
using Notify.NET.Abstractions;
using Notify.NET.Platform.Windows;
using Notify.NET.Platform.Linux;
using Notify.NET.Platform.MacOS;

namespace Notify.NET.Extensions
{
    /// <summary>
    /// Extension methods for registering <see cref="INotificationService"/> with an
    /// <see cref="IServiceCollection"/>. The correct platform implementation is selected
    /// automatically at runtime.
    /// </summary>
    public static class ServiceCollectionExtensions
    {
        /// <summary>
        /// Registers <see cref="INotificationService"/> as a singleton, using the
        /// platform-appropriate backend:
        /// <list type="bullet">
        ///   <item><description>Windows → <see cref="WindowsNotificationService"/> (WinToastLib)</description></item>
        ///   <item><description>Linux → <see cref="LinuxNotificationService"/> (libnotify)</description></item>
        ///   <item><description>macOS → <see cref="MacOSNotificationService"/> (UNUserNotificationCenter)</description></item>
        ///   <item><description>Other → <see cref="NullNotificationService"/> (<see cref="INotificationService.IsSupported"/> = false)</description></item>
        /// </list>
        /// </summary>
        /// <param name="services">The service collection to add to.</param>
        /// <param name="configure">Optional delegate to configure <see cref="NotificationOptions"/>.</param>
        public static IServiceCollection AddNotifications(
            this IServiceCollection services,
            Action<NotificationOptions>? configure = null)
        {
            var options = new NotificationOptions();
            configure?.Invoke(options);

            services.AddSingleton(options);

            services.AddSingleton<INotificationService>(sp =>
            {
                var opts = sp.GetRequiredService<NotificationOptions>();
                return CreateService(opts);
            });

            return services;
        }

        /// <summary>
        /// Creates the platform-appropriate <see cref="INotificationService"/> directly
        /// (without a DI container), for use in simple console applications.
        /// </summary>
        public static INotificationService CreateNotificationService(
            Action<NotificationOptions>? configure = null)
        {
            var opts = new NotificationOptions();
            configure?.Invoke(opts);
            return CreateService(opts);
        }

        private static INotificationService CreateService(NotificationOptions opts)
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                return new WindowsNotificationService(opts.AppName, opts.AppUserModelId, opts.AppIconPath);

            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                return new LinuxNotificationService(opts.AppName);

            if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                return new MacOSNotificationService(opts.AppName);

            return new NullNotificationService();
        }

        /// <summary>
        /// Registers <see cref="ITaskbarProgressService"/> as a singleton, using the
        /// platform-appropriate backend:
        /// <list type="bullet">
        ///   <item><description>Windows → <see cref="WindowsTaskbarProgressService"/> (ITaskbarList3)</description></item>
        ///   <item><description>Linux → <see cref="LinuxTaskbarProgressService"/> (Unity LauncherEntry D-Bus)</description></item>
        ///   <item><description>macOS → <see cref="MacOSTaskbarProgressService"/> (Dock tile)</description></item>
        ///   <item><description>Other → <see cref="NullTaskbarProgressService"/> (<see cref="ITaskbarProgressService.IsSupported"/> = false)</description></item>
        /// </list>
        /// </summary>
        /// <param name="services">The service collection to add to.</param>
        /// <param name="configure">Optional delegate to configure <see cref="NotificationOptions"/>.</param>
        public static IServiceCollection AddTaskbarProgress(
            this IServiceCollection services,
            Action<NotificationOptions>? configure = null)
        {
            var options = new NotificationOptions();
            configure?.Invoke(options);

            services.AddSingleton<ITaskbarProgressService>(_ => CreateTaskbarService(options));
            return services;
        }

        /// <summary>
        /// Creates the platform-appropriate <see cref="ITaskbarProgressService"/> directly
        /// (without a DI container), for use in simple console applications.
        /// </summary>
        public static ITaskbarProgressService CreateTaskbarProgressService(
            Action<NotificationOptions>? configure = null)
        {
            var opts = new NotificationOptions();
            configure?.Invoke(opts);
            return CreateTaskbarService(opts);
        }

        private static ITaskbarProgressService CreateTaskbarService(NotificationOptions opts)
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                return new WindowsTaskbarProgressService();

            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                return new LinuxTaskbarProgressService(
                    opts.DesktopFileId ?? System.Diagnostics.Process.GetCurrentProcess().ProcessName);

            if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                return new MacOSTaskbarProgressService();

            return new NullTaskbarProgressService();
        }
    }

    /// <summary>
    /// Configuration options for the notification service.
    /// Pass to <see cref="ServiceCollectionExtensions.AddNotifications"/> via the configure delegate.
    /// </summary>
    public sealed class NotificationOptions
    {
        /// <summary>
        /// Human-readable application name shown in the notification and Action Centre.
        /// Defaults to the process name.
        /// </summary>
        public string AppName { get; set; } =
            System.Diagnostics.Process.GetCurrentProcess().ProcessName;

        /// <summary>
        /// Windows AppUserModelId (AUMI), e.g. <c>"MyCompany.MyApp"</c>.
        /// Required for notifications to persist in the Windows Action Centre.
        /// The native wrapper creates a Start-Menu shortcut with this AUMI automatically.
        /// Ignored on non-Windows platforms.
        /// </summary>
        public string AppUserModelId { get; set; } =
            System.Diagnostics.Process.GetCurrentProcess().ProcessName;

        /// <summary>
        /// Optional absolute path to an .ico (or .exe/.dll) file whose first icon is stamped onto
        /// the Start-Menu shortcut and shown as the small icon in the top-left corner of every toast
        /// notification from this app. Set once at startup; null uses the host executable's icon.
        /// Windows only — ignored on Linux and macOS.
        /// </summary>
        public string? AppIconPath { get; set; }

        /// <summary>
        /// The application's <c>.desktop</c> file id (with or without the ".desktop" suffix), e.g.
        /// <c>"com.example.MyApp"</c>. Used by the Linux taskbar-progress backend to address the
        /// correct launcher entry via the <c>application://&lt;id&gt;.desktop</c> URI. When null,
        /// the process name is used. Ignored on Windows and macOS.
        /// </summary>
        public string? DesktopFileId { get; set; }
    }

    /// <summary>
    /// No-op implementation used when the current platform has no supported notification backend.
    /// <see cref="IsSupported"/> is always false; calling <see cref="ShowAsync"/> throws
    /// <see cref="Exceptions.PlatformNotSupportedException"/>.
    /// </summary>
    internal sealed class NullNotificationService : INotificationService
    {
        public bool IsSupported => false;

        public Task<long> ShowAsync(NotificationRequest request, CancellationToken cancellationToken = default)
            => throw new Exceptions.PlatformNotSupportedException();

        public Task HideAsync(long notificationId, CancellationToken cancellationToken = default)
            => throw new Exceptions.PlatformNotSupportedException();

        public void Dispose() { }
    }

    /// <summary>
    /// No-op implementation used when the current platform has no supported taskbar-progress
    /// backend. <see cref="IsSupported"/> is always false and every method is a silent no-op.
    /// </summary>
    internal sealed class NullTaskbarProgressService : ITaskbarProgressService
    {
        public bool IsSupported => false;
        public void SetState(TaskbarProgressState state) { }
        public void SetProgress(ulong completed, ulong total) { }
        public void SetProgress(double fraction) { }
        public void SetWindow(IntPtr windowHandle) { }
        public void Dispose() { }
    }
}
