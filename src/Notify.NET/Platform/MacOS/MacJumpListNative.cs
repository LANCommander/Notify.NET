using System;
using System.Runtime.InteropServices;

namespace Notify.NET.Platform.MacOS
{
    /// <summary>
    /// P/Invoke declarations for the Dock-menu ("jump list") entry points exported by
    /// <c>libMacNotifyWrapper.dylib</c> (see <c>MacNotifyWrapper.h</c>).
    ///
    /// Unlike the Windows/Linux jump lists, the macOS Dock menu fires a live in-process
    /// callback (<see cref="DockMenuCallback"/>) — there is no relaunch. The entry points are
    /// only effective for a regular bundled GUI application with a running main loop; a bare
    /// console process has no Dock menu and the calls are harmless no-ops.
    ///
    /// All strings are UTF-8; on macOS the ANSI code page is UTF-8 so <see cref="UnmanagedType.LPStr"/>
    /// marshalling is a faithful round-trip. Every function uses the C calling convention (cdecl).
    /// </summary>
    internal static class MacJumpListNative
    {
        internal const string LibName = "MacNotifyWrapper";

        /// <summary>
        /// Fired on the main thread when the user clicks a Dock-menu item; <paramref name="taskId"/>
        /// is the id supplied to <see cref="MNW_SetDockMenu"/> for that item.
        /// </summary>
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        internal delegate void DockMenuCallback([MarshalAs(UnmanagedType.LPStr)] string taskId);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        internal static extern bool MNW_IsSupported();

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void MNW_SetDockMenuHandler(DockMenuCallback? callback);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void MNW_SetDockMenu(
            [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPStr)] string[]? ids,
            [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPStr)] string[]? titles,
            int count);

        [DllImport(LibName, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void MNW_ClearDockMenu();
    }
}
