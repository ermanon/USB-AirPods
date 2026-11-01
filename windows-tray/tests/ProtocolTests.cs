using System;
using USBPodsTray;
class ProtocolTests {
    static int count;
    static void Check(bool ok, string name) { count++; if (!ok) throw new Exception(name); }
    static void Main() {
        AncSnapshot anc;
        Check(AncSnapshot.TryParse("[USBPODS-ANC/1] 12345678 1 2 10 ABCDEF01", 0x12345678, out anc), "ANC snapshot");
        Check(anc.Mode == 2 && anc.CanSet(4) && !anc.CanSet(1), "Only verified modes");
        anc.ReceivedUtc = DateTime.UtcNow.AddSeconds(-9);
        Check(!anc.CanSet(2), "Stale ANC disabled");
        Check(!AncSnapshot.TryParse("[USBPODS-ANC/1] 12345678 1 2 1 ABCDEF01", 0x12345678, out anc), "Inconsistent mode mask");
        Check(!AncSnapshot.TryParse("[USBPODS-ANC/1] 12345678 1 2 10 ABCDEF01", 1, out anc), "Stale ANC nonce");
        Check(AncSnapshot.TryParse("[USBPODS-ANC/1] 00000001 0 0 0 00000002", 1, out anc) && !anc.CanSet(2), "ANC disconnect");
        BatterySnapshot s;
        const string valid = "[USBPODS-BATTERY/1] 1234ABCD 1 99 1 10 100 2 10 17 2 10";
        Check(BatteryProtocol.TryParse(valid, 0x1234ABCD, out s), "Valid snapshot");
        Check(s.Values[0].Level == 99 && s.Values[1].Level == 100 && s.Values[2].Level == 17, "Component order");
        Check(s.Fresh(0, DateTime.UtcNow), "Current reading");
        Check(!s.Fresh(0, s.ReceivedUtc.AddSeconds(9)), "Transport timeout");
        s.Values[0].Age = 300000;
        Check(!s.Fresh(0, DateTime.UtcNow), "Old component");
        Check(!BatteryProtocol.TryParse(valid, 0x1234ABCE, out s), "Reject buffered old nonce");
        Check(!BatteryProtocol.TryParse(valid + " 1", 0x1234ABCD, out s), "Extra fields");
        Check(!BatteryProtocol.TryParse(valid.Replace("99 1", "101 1"), 0x1234ABCD, out s), "Invalid percent");
        Check(!BatteryProtocol.TryParse(valid.Replace("99 1", "99 3"), 0x1234ABCD, out s), "Invalid status");
        Check(!BatteryProtocol.TryParse("[MIC] sdu/s=1", 0, out s), "Ignore audio logs");
        Check(BatteryProtocol.TryParse("[USBPODS-BATTERY/1] 00000001 0 -1 0 4294967295 -1 0 4294967295 -1 0 4294967295", 1, out s) && !s.Fresh(0, DateTime.UtcNow), "Disconnected");
        Check(BatteryProtocol.TryParse(valid.Replace("99 1", "0 2"), 0x1234ABCD, out s) && s.Fresh(0, DateTime.UtcNow), "Zero percent is valid");
        Check(BatteryProtocol.TryParse(valid.Replace("99 1", "99 4"), 0x1234ABCD, out s) && !s.Fresh(0, DateTime.UtcNow), "Disconnected component is not current");
        DeviceSnapshot device;
        Check(DeviceSnapshot.TryParse("[USBPODS-DEVICE/1] 1234ABCD 1 AA:BB:CC:11:22:33", 0x1234ABCD, out device), "Device identity");
        Check(device.Connected && device.Address == "AA:BB:CC:11:22:33", "Device address preserved");
        Check(!DeviceSnapshot.TryParse("[USBPODS-DEVICE/1] 1234ABCD 1 AA:BB:CC:11:22:3Z", 0x1234ABCD, out device), "Reject malformed address");
        Check(!DeviceSnapshot.TryParse("[USBPODS-DEVICE/1] 1234ABCD 1 AA:BB:CC:11:22:33", 1, out device), "Reject old device nonce");
        Console.WriteLine("PASS: " + count + " protocol checks");
    }
}
