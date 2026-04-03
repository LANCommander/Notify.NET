using System;
using System.Collections.Generic;
using Notify.NET.Builder;

namespace Notify.NET.Abstractions
{
    /// <summary>
    /// Immutable description of a notification to be displayed.
    /// Construct instances via <see cref="NotificationBuilder"/>.
    /// </summary>
    public sealed class NotificationRequest
    {
        /// <summary>The primary heading of the notification.</summary>
        public string Title { get; }

        /// <summary>Optional body text shown beneath the title.</summary>
        public string? Body { get; }

        /// <summary>Absolute path to an image file displayed as a square thumbnail.</summary>
        public string? ImagePath { get; }

        /// <summary>
        /// Absolute path to an image file displayed full-width above the title, preserving aspect ratio.
        /// Windows only — ignored on Linux and macOS.
        /// </summary>
        public string? HeroImagePath { get; }

        /// <summary>Action buttons to display. Maximum platform limits apply (typically 5 on Windows, varies on Linux).</summary>
        public IReadOnlyList<NotificationButton> Buttons { get; }

        /// <summary>Optional interface-based handler for notification lifecycle events.</summary>
        public INotificationHandler? Handler { get; }

        /// <summary>How long to display the notification before it expires automatically. Null means use the platform default.</summary>
        public TimeSpan? Expiration { get; }

        /// <summary>Audio behaviour when the notification appears.</summary>
        public NotificationAudio Audio { get; }

        /// <summary>The urgency/scenario of the notification, which may affect how the platform presents it.</summary>
        public NotificationUrgency Urgency { get; }

        internal NotificationRequest(
            string title,
            string? body,
            string? imagePath,
            string? heroImagePath,
            IReadOnlyList<NotificationButton> buttons,
            INotificationHandler? handler,
            TimeSpan? expiration,
            NotificationAudio audio,
            NotificationUrgency urgency)
        {
            if (string.IsNullOrWhiteSpace(title))
                throw new ArgumentException("Notification title must not be empty.", nameof(title));

            Title = title;
            Body = body;
            ImagePath = imagePath;
            HeroImagePath = heroImagePath;
            Buttons = buttons;
            Handler = handler;
            Expiration = expiration;
            Audio = audio;
            Urgency = urgency;
        }
    }

    /// <summary>Controls the audio played when the notification is shown (Windows only; Linux ignores this).</summary>
    public enum NotificationAudio
    {
        /// <summary>Play the platform default notification sound.</summary>
        Default,
        /// <summary>Display silently with no sound.</summary>
        Silent,
        /// <summary>Loop the notification sound until the notification is dismissed.</summary>
        Loop
    }

    /// <summary>Maps to the notification urgency/scenario on each platform.</summary>
    public enum NotificationUrgency
    {
        /// <summary>Standard informational notification.</summary>
        Normal,
        /// <summary>Low-priority; the platform may suppress or delay it.</summary>
        Low,
        /// <summary>High-priority; may bypass Do Not Disturb on some platforms.</summary>
        Critical,
        /// <summary>Alarm scenario (Windows) — may produce a full-screen interrupt.</summary>
        Alarm,
        /// <summary>Reminder scenario (Windows).</summary>
        Reminder
    }

    /// <summary>Reason a notification was dismissed.</summary>
    public enum DismissReason
    {
        /// <summary>The user explicitly dismissed the notification.</summary>
        UserCancelled,
        /// <summary>The notification timed out / expired.</summary>
        TimedOut,
        /// <summary>The application programmatically hid the notification.</summary>
        ApplicationHidden,
        /// <summary>Dismissed for an unspecified or platform-specific reason.</summary>
        Unknown
    }
}
