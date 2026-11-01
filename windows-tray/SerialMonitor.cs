// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Text;
using System.Threading;
using Microsoft.Win32;

namespace USBPodsTray {
    public sealed class SerialMonitor : IDisposable {
        private readonly Thread worker;
        private volatile bool stopping;
        private volatile string selectedPort = "";
        public volatile string Message = "Searching for USBAirPods…";
        public volatile BatterySnapshot Snapshot;
        public volatile AncSnapshot Anc;
        public volatile DeviceSnapshot Device;
        public volatile string AncMessage = "ANC information pending";
        private sealed class AncRequest { public int Mode; public uint Session; }
        private AncRequest ancRequest;
        private int ancBusy;
        public bool AncBusy { get { return Interlocked.CompareExchange(ref ancBusy, 0, 0) != 0; } }
        public bool RequestAnc(int mode) {
            AncSnapshot s = Anc;
            BatterySnapshot battery = Snapshot;
            if (battery == null || !battery.Connected || (DateTime.UtcNow - battery.ReceivedUtc).TotalSeconds >= 8 ||
                s == null || !s.CanSet(mode) || s.Mode == mode || Interlocked.CompareExchange(ref ancBusy, 1, 0) != 0) return false;
            Interlocked.Exchange(ref ancRequest, new AncRequest { Mode = mode, Session = s.Session });
            return true;
        }
        private uint nonce = BitConverter.ToUInt32(Guid.NewGuid().ToByteArray(), 0);

        public SerialMonitor() {
            worker = new Thread(Run);
            worker.IsBackground = true;
            worker.Name = "USBAirPods serial reader";
            worker.Start();
        }
        public void SelectPort(string port) {
            selectedPort = port;
            Snapshot = null;
            Anc = null;
            Device = null;
            Interlocked.Exchange(ref ancRequest, null);
        }
        public string SelectedPort { get { return selectedPort; } }

        private static string ConnectionError(Exception error) {
            if (error is UnauthorizedAccessException) return "Access denied. The port may be in use by another application.";
            if (error is TimeoutException) return "The connection timed out.";
            if (error is System.IO.IOException) return "The serial device is unavailable or was disconnected.";
            if (error is ArgumentException) return "The selected serial port is invalid.";
            return "Unable to communicate with the device (" + error.GetType().Name + ").";
        }

        public static string[] FindUsbPodsPorts() {
            List<string> ports = new List<string>();
            // Only this firmware's composite VID/PID. Never probe unrelated
            // serial devices automatically. No WMI/admin rights required.
            using (RegistryKey usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB")) {
                if (usb == null) return ports.ToArray();
                foreach (string hardware in usb.GetSubKeyNames()) {
                    if (!hardware.StartsWith("VID_CAFE&PID_4011&MI_", StringComparison.OrdinalIgnoreCase)) continue;
                    using (RegistryKey group = usb.OpenSubKey(hardware)) {
                        if (group == null) continue;
                        foreach (string instance in group.GetSubKeyNames()) {
                            using (RegistryKey parameters = group.OpenSubKey(instance + @"\Device Parameters")) {
                                string port = parameters == null ? null : parameters.GetValue("PortName") as string;
                                if (port != null && Array.IndexOf(SerialPort.GetPortNames(), port) >= 0 && !ports.Contains(port)) ports.Add(port);
                            }
                        }
                    }
                }
            }
            ports.Sort(StringComparer.OrdinalIgnoreCase);
            return ports.ToArray();
        }
        private void Run() {
            while (!stopping) {
                try {
                    string choice = selectedPort;
                    string[] ports = choice.Length == 0 ? FindUsbPodsPorts() : new string[] {choice};
                    if (ports.Length == 0) Message = "USBAirPods not found. Check the USB connection.";
                    foreach (string port in ports) {
                        if (stopping || selectedPort != choice) break;
                        try { ReadPort(port, choice); }
                        catch (Exception e) {
                            Anc = null;
                            Device = null;
                            Interlocked.Exchange(ref ancBusy, 0);
                            Snapshot = null;
                            Message = port + ": " + ConnectionError(e);
                        }
                    }
                } catch (Exception e) { Message = "Connection: " + ConnectionError(e); Snapshot = null; Device = null; }
                for (int i = 0; i < 8 && !stopping; i++) Thread.Sleep(250);
            }
        }
        private static void Trace(string text) {
            if (!(text.Contains("[USBPODS-") || text.Contains("[BUILD]") || text.Contains("[OUT]") || text.Contains("[DEC]") || text.Contains("[EVENT]"))) return;
            try {
                string file = System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "companion-status.log");
                if (System.IO.File.Exists(file) && new System.IO.FileInfo(file).Length > 1048576) {
                    string old = file + ".old";
                    if (System.IO.File.Exists(old)) System.IO.File.Delete(old);
                    System.IO.File.Move(file, old);
                }
                System.IO.File.AppendAllText(file, DateTime.UtcNow.ToString("o") + " " + text + Environment.NewLine);
            } catch (System.IO.IOException) { } catch (UnauthorizedAccessException) { }
        }
        private void ReadPort(string name, string choice) {
            Anc = null;
            Device = null;
            AncMessage = "ANC information pending; a firmware update may be needed";
            Interlocked.Exchange(ref ancRequest, null);
            Interlocked.Exchange(ref ancBusy, 0);
            using (SerialPort port = new SerialPort(name, 115200, Parity.None, 8, StopBits.One)) {
                port.DtrEnable = true;
                port.RtsEnable = false;
                port.Handshake = Handshake.None;
                port.ReadTimeout = 250;
                port.WriteTimeout = 500;
                port.Open();
                Snapshot = null;
                Device = null;
                Message = name + ": checking battery support…";
                StringBuilder line = new StringBuilder();
                bool overflow = false;
                DateTime lastReply = DateTime.UtcNow;
                DateTime nextQuery = DateTime.MinValue;
                uint activeNonce = 0;
                uint commandNonce = 0, commandSession = 0;
                int requestedMode = 0;
                DateTime commandDeadline = DateTime.MinValue;
                char[] buffer = new char[512];
                while (!stopping && selectedPort == choice) {
                    AncRequest request = Interlocked.Exchange(ref ancRequest, null);
                    if (request != null && requestedMode == 0) {
                        AncSnapshot current = Anc;
                        if (current != null && current.Session == request.Session && current.CanSet(request.Mode)) {
                            commandNonce = unchecked(++nonce);
                            commandSession = request.Session;
                            requestedMode = request.Mode;
                            commandDeadline = DateTime.UtcNow.AddSeconds(8);
                            AncMessage = "Waiting for mode confirmation…";
                            port.Write("USBPODS ANC " + commandNonce.ToString("X8") + " " + commandSession.ToString("X8") + " " + requestedMode + "\n");
                        } else {
                            Interlocked.Exchange(ref ancBusy, 0);
                            AncMessage = "Connection changed; mode request was not sent";
                        }
                    }
                    if (requestedMode != 0 && DateTime.UtcNow >= commandDeadline) {
                        requestedMode = 0;
                        Interlocked.Exchange(ref ancBusy, 0);
                        AncMessage = "The headphones did not confirm the mode change";
                    }
                    if (DateTime.UtcNow >= nextQuery) {
                        activeNonce = unchecked(++nonce);
                        port.Write("USBPODS BATTERY " + activeNonce.ToString("X8") + "\n");
                        nextQuery = DateTime.UtcNow.AddSeconds(2);
                    }
                    int n = 0;
                    try { n = port.Read(buffer, 0, buffer.Length); }
                    catch (TimeoutException) { }
                    for (int i = 0; i < n; i++) {
                        char c = buffer[i];
                        if (c == '\n') {
                            if (!overflow) Trace(line.ToString());
                            DeviceSnapshot device;
                            if (!overflow && DeviceSnapshot.TryParse(line.ToString(), activeNonce, out device)) Device = device;
                            AncSnapshot anc;
                            if (!overflow && AncSnapshot.TryParse(line.ToString(), activeNonce, out anc)) {
                                Anc = anc;
                                if (!anc.Connected || (requestedMode != 0 && anc.Session != commandSession)) {
                                    requestedMode = 0;
                                    Interlocked.Exchange(ref ancBusy, 0);
                                    AncMessage = "ANC connection unavailable";
                                } else if (requestedMode != 0 && anc.Mode == requestedMode) {
                                    requestedMode = 0;
                                    Interlocked.Exchange(ref ancBusy, 0);
                                    AncMessage = "Mode confirmed by the headphones";
                                } else if (requestedMode == 0 && (AncMessage.StartsWith("ANC") || AncMessage == "Mode received from the headphones")) {
                                    AncMessage = anc.Mode == 0 ? "ANC support has not been confirmed" : "Mode received from the headphones";
                                }
                            }
                            if (!overflow && requestedMode != 0) {
                                string response = line.ToString();
                                string prefix = "[USBPODS-ANC-RESULT/1] " + commandNonce.ToString("X8") + " ";
                                if (response == prefix + "1" || response == prefix + "2") {
                                    requestedMode = 0;
                                    Interlocked.Exchange(ref ancBusy, 0);
                                    AncMessage = "Mode request rejected or connection busy";
                                }
                            }
                            BatterySnapshot snapshot;
                            if (!overflow && BatteryProtocol.TryParse(line.ToString(), activeNonce, out snapshot)) {
                                snapshot.Port = name;
                                Snapshot = snapshot;
                                lastReply = DateTime.UtcNow;
                                Message = snapshot.Connected ? name + " · Headphones connected" : name + " · Waiting for headphones";
                            }
                            line.Length = 0;
                            overflow = false;
                        } else if (c != '\r') {
                            if (line.Length >= 1024) overflow = true;
                            else if (!overflow) line.Append(c);
                        }
                    }
                    if ((DateTime.UtcNow - lastReply).TotalSeconds > 8) {
                        Snapshot = null;
                        Message = name + ": no battery response. Install the battery-enabled UF2 on the Pico.";
                        break;
                    }
                }
                Snapshot = null;
                Anc = null;
                Device = null;
                Interlocked.Exchange(ref ancBusy, 0);
            }
        }
        public void Dispose() {
            stopping = true;
            worker.Join(1500);
        }
    }
}
