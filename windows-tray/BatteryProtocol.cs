// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Globalization;

namespace USBPodsTray {
    public sealed class DeviceSnapshot {
        public bool Connected;
        public string Address;
        public DateTime ReceivedUtc = DateTime.UtcNow;
        public static bool TryParse(string line, uint nonce, out DeviceSnapshot result) {
            result = null;
            string[] p = line.Trim().Split(new char[] {' '}, StringSplitOptions.RemoveEmptyEntries);
            uint receivedNonce;
            if (p.Length != 4 || p[0] != "[USBPODS-DEVICE/1]" || p[1].Length != 8 ||
                !uint.TryParse(p[1], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out receivedNonce) ||
                receivedNonce != nonce || (p[2] != "0" && p[2] != "1") || !IsAddress(p[3])) return false;
            result = new DeviceSnapshot { Connected = p[2] == "1", Address = p[3].ToUpperInvariant() };
            return true;
        }
        private static bool IsAddress(string value) {
            if (value == null || value.Length != 17) return false;
            for (int i = 0; i < value.Length; i++) {
                if (i % 3 == 2) { if (value[i] != ':') return false; }
                else if (!Uri.IsHexDigit(value[i])) return false;
            }
            return true;
        }
    }
    public sealed class AncSnapshot {
        public int Mode, Mask;
        public uint Session;
        public bool Connected;
        public DateTime ReceivedUtc = DateTime.UtcNow;
        public bool CanSet(int mode) {
            return Connected && (DateTime.UtcNow - ReceivedUtc).TotalSeconds < 8 &&
                mode >= 1 && mode <= 4 && (Mask & (1 << (mode - 1))) != 0;
        }
        public static bool TryParse(string line, uint nonce, out AncSnapshot result) {
            result = null;
            string[] p = line.Trim().Split(' ');
            uint n;
            AncSnapshot s = new AncSnapshot();
            if (p.Length != 6 || p[0] != "[USBPODS-ANC/1]" || p[1].Length != 8 ||
                !uint.TryParse(p[1], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out n) || n != nonce ||
                (p[2] != "0" && p[2] != "1") || !int.TryParse(p[3], out s.Mode) || s.Mode < 0 || s.Mode > 4 ||
                !int.TryParse(p[4], out s.Mask) || s.Mask < 0 || s.Mask > 15 || p[5].Length != 8 ||
                !uint.TryParse(p[5], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out s.Session)) return false;
            if (s.Mode == 0 ? s.Mask != 0 : (s.Mask & (1 << (s.Mode - 1))) == 0) return false;
            s.Connected = p[2] == "1";
            result = s; return true;
        }
    }
    public sealed class BatteryValue {
        public int Level;
        public int Status;
        public uint Age;
    }
    public sealed class BatterySnapshot {
        public bool Connected;
        public BatteryValue[] Values = new BatteryValue[3];
        public DateTime ReceivedUtc;
        public string Port;
        public bool Fresh(int i, DateTime now) {
            return Connected && Values[i].Level >= 0 &&
                (Values[i].Status == 1 || Values[i].Status == 2) &&
                (now - ReceivedUtc).TotalSeconds < 8 &&
                Values[i].Age + (now - ReceivedUtc).TotalMilliseconds < 300000;
        }
    }
    public static class BatteryProtocol {
        public static bool TryParse(string line, uint nonce, out BatterySnapshot result) {
            result = null;
            string[] p = line.Trim().Split(new char[] {' '}, StringSplitOptions.RemoveEmptyEntries);
            uint receivedNonce;
            if (p.Length != 12 || p[0] != "[USBPODS-BATTERY/1]" || p[1].Length != 8 ||
                !uint.TryParse(p[1], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out receivedNonce) ||
                receivedNonce != nonce || (p[2] != "0" && p[2] != "1")) return false;
            BatterySnapshot s = new BatterySnapshot();
            s.Connected = p[2] == "1";
            s.ReceivedUtc = DateTime.UtcNow;
            for (int i = 0; i < 3; i++) {
                BatteryValue v = new BatteryValue();
                int off = 3 + i * 3;
                if (!int.TryParse(p[off], NumberStyles.Integer, CultureInfo.InvariantCulture, out v.Level) ||
                    v.Level < -1 || v.Level > 100 ||
                    !int.TryParse(p[off + 1], out v.Status) ||
                    (v.Status != 0 && v.Status != 1 && v.Status != 2 && v.Status != 4) ||
                    !uint.TryParse(p[off + 2], NumberStyles.None, CultureInfo.InvariantCulture, out v.Age)) return false;
                s.Values[i] = v;
            }
            result = s;
            return true;
        }
    }
}
