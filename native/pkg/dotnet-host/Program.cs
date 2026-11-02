/*
 * MoonlightHost - minimal Tizen .NET host for the no-root dev-cert path.
 *
 * On a retail (unrooted) Samsung TV only Web (.wgt) and .NET (.tpk) apps install.
 * So the installable unit is a dev-signed .NET .tpk; our native streaming engine
 * ships inside it as libmlengine.so and is driven over the stable C ABI (mlengine.h)
 * by the P/Invoke block below - the SAME four symbols the root-path exe uses.
 *
 * STATUS: path-proof skeleton. The P/Invoke signatures are final; the lifecycle and
 * (absent) UI are placeholders. Finalise TFM/privileges/signing against the WORKING
 * `dev.moonlight.tvshell` already installed on the TV (see ../DEV-CERT-PATH.md).
 */
using System;
using System.Runtime.InteropServices;
using Tizen.Applications;

namespace MoonlightHost
{
    internal static class Engine
    {
        // DllImport resolves libmlengine.so from the app's lib/ dir at runtime.
        private const string Lib = "libmlengine.so";

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int ml_engine_start(string host, int appId, string pin,
            int width, int height, int fps, int bitrateKbps, string datadir);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern int ml_engine_running();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ml_engine_wait();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ml_engine_stop();
    }

    internal sealed class App : CoreUIApplication
    {
        protected override void OnCreate()
        {
            base.OnCreate();
            // TODO: real menu + pairing UI (reuse moonlight-tizen web UI, or NUI).
            // Path-proof default: start a session to a host from an env/metadata value.
            string host = Environment.GetEnvironmentVariable("MOONLIGHT_HOST") ?? "192.168.123.3";
            string dataDir = DirectoryInfo.Data;
            int rc = Engine.ml_engine_start(host, 0, null, 1920, 1080, 60, 20000, dataDir);
            Tizen.Log.Info("MLHOST", $"ml_engine_start(host={host}) rc={rc}");
        }

        protected override void OnTerminate()
        {
            Engine.ml_engine_stop();
            base.OnTerminate();
        }

        private static void Main(string[] args) => new App().Run(args);
    }
}
