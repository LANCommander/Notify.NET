using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Notify.NET.Platform.Windows
{
    /// <summary>
    /// Loads WinToastWrapper.dll from the correct runtime-identifier sub-folder before
    /// the first P/Invoke call is made. This ensures the x64/x86/arm64 variant that
    /// matches the current process architecture is used.
    ///
    /// Once LoadLibraryW succeeds, subsequent DllImport("WinToastWrapper") resolutions
    /// find the already-loaded module in the process module list automatically.
    /// </summary>
    internal static class NativeLibraryLoader
    {
        private static volatile bool _loaded;
        private static readonly object _lock = new object();

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string lpLibFileName);

        internal static void EnsureLoaded()
        {
            if (_loaded) return;

            lock (_lock)
            {
                if (_loaded) return;

                string rid = GetRuntimeIdentifier();
                string dllPath = ResolveNativePath(rid);

                IntPtr handle = LoadLibraryW(dllPath);
                if (handle == IntPtr.Zero)
                {
                    int err = Marshal.GetLastWin32Error();
                    throw new DllNotFoundException(
                        $"Failed to load WinToastWrapper.dll from '{dllPath}' (Win32 error {err}). " +
                        "Ensure the native DLL for your platform architecture is present in the " +
                        $"runtimes/{rid}/native/ directory relative to the assembly.");
                }

                _loaded = true;
            }
        }

        private static string GetRuntimeIdentifier()
        {
            switch (RuntimeInformation.ProcessArchitecture)
            {
                case Architecture.X64:   return "win-x64";
                case Architecture.X86:   return "win-x86";
                case Architecture.Arm64: return "win-arm64";
                default:
                    throw new PlatformNotSupportedException(
                        $"No WinToastWrapper.dll is available for architecture {RuntimeInformation.ProcessArchitecture}.");
            }
        }

        private static string ResolveNativePath(string rid)
        {
            // Search order:
            // 1. Alongside the executing assembly (output directory, typical for app projects)
            // 2. Relative to the assembly's location using the NuGet runtimes layout
            string assemblyDir = Path.GetDirectoryName(
                new Uri(typeof(NativeLibraryLoader).Assembly.CodeBase!).LocalPath)!;

            // Typical publish output: <outdir>/WinToastWrapper.dll (copied by MSBuild)
            string flat = Path.Combine(assemblyDir, "WinToastWrapper.dll");
            if (File.Exists(flat)) return flat;

            // NuGet runtimes layout: <assemblyDir>/runtimes/<rid>/native/WinToastWrapper.dll
            string runtimePath = Path.Combine(assemblyDir, "runtimes", rid, "native", "WinToastWrapper.dll");
            if (File.Exists(runtimePath)) return runtimePath;

            // Fallback: relative to the entry assembly location
            string? entryDir = Path.GetDirectoryName(Assembly.GetEntryAssembly()?.Location);
            if (entryDir != null)
            {
                string entryRuntime = Path.Combine(entryDir, "runtimes", rid, "native", "WinToastWrapper.dll");
                if (File.Exists(entryRuntime)) return entryRuntime;

                string entryFlat = Path.Combine(entryDir, "WinToastWrapper.dll");
                if (File.Exists(entryFlat)) return entryFlat;
            }

            // Return the NuGet path even if not found — LoadLibraryW will fail with a useful error
            return Path.Combine(assemblyDir, "runtimes", rid, "native", "WinToastWrapper.dll");
        }
    }
}
