param([Parameter(Mandatory=$true)][string]$Left,
      [Parameter(Mandatory=$true)][string]$Right,
      [Parameter(Mandatory=$true)][int]$Width,
      [Parameter(Mandatory=$true)][int]$Height,
      [ValidateSet('RGBA16F','R32F','RGBA8')][string]$Format = 'RGBA16F')
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class VCloudDumpDiff {
    static double Half(ushort bits) {
        int e = (bits >> 10) & 31, m = bits & 1023;
        double v = e == 0 ? m * Math.Pow(2, -24) :
            e == 31 ? (m == 0 ? double.PositiveInfinity : double.NaN) :
            (1024 + m) * Math.Pow(2, e - 25);
        return (bits & 32768) != 0 ? -v : v;
    }
    public static string Compare(string left, string right, int width, int height, string format) {
        byte[] a = File.ReadAllBytes(left), b = File.ReadAllBytes(right);
        if (a.Length != b.Length) throw new Exception("Different dump sizes");
        int size = format == "RGBA16F" ? 2 : format == "R32F" ? 4 : 1;
        int channels = format == "R32F" ? 1 : 4;
        long count = 0; int first = -1; double maximum = 0;
        double firstA = 0, firstB = 0; uint bitsA = 0, bitsB = 0;
        for (int i = 0; i < a.Length; i += size) {
            uint x = size == 2 ? BitConverter.ToUInt16(a,i) : size == 4 ? BitConverter.ToUInt32(a,i) : a[i];
            uint y = size == 2 ? BitConverter.ToUInt16(b,i) : size == 4 ? BitConverter.ToUInt32(b,i) : b[i];
            if (x == y) continue;
            double vx = size == 2 ? Half((ushort)x) : size == 4 ? BitConverter.ToSingle(a,i) : x;
            double vy = size == 2 ? Half((ushort)y) : size == 4 ? BitConverter.ToSingle(b,i) : y;
            if (first < 0) { first=i; firstA=vx; firstB=vy; bitsA=x; bitsB=y; }
            count++; maximum=Math.Max(maximum,Math.Abs(vx-vy));
        }
        int pixel = first < 0 ? 0 : first / (size * channels);
        return String.Format(System.Globalization.CultureInfo.InvariantCulture,
            "differentElements={0} firstByte={1} xyz=({2},{3},{4}) channel={5} bits=0x{6:x}->0x{7:x} values={8:R}->{9:R} maxAbs={10:R}",
            count, first, pixel%width, pixel/width%height, pixel/(width*height),
            first < 0 ? -1 : first/size%channels, bitsA, bitsB, firstA, firstB, maximum);
    }
}
'@
[VCloudDumpDiff]::Compare($Left,$Right,$Width,$Height,$Format)
