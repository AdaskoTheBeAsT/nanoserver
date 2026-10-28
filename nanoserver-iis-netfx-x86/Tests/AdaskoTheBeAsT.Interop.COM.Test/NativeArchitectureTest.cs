using System.Runtime.InteropServices;
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

#pragma warning disable SA1121 // Use built-in type alias
public class NativeArchitectureTest
{
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void NativePayloadShouldMatchTestProcess(bool lifetimeProbe)
    {
        var fileName = lifetimeProbe ? "NativeLifetimeProbe.dll" : "NativeCOM.dll";
#pragma warning disable SCS0018 // Both filenames are fixed local test fixtures, not caller-supplied paths.
        using var stream = File.OpenRead(Path.Combine(AppContext.BaseDirectory, fileName));
#pragma warning restore SCS0018
        using var reader = new BinaryReader(stream);
        reader.ReadUInt16().Should().Be(0x5A4D);
        stream.Position = 0x3C;
        var peOffset = reader.ReadInt32();
        stream.Position = peOffset;
        reader.ReadUInt32().Should().Be(0x00004550);
        reader.ReadUInt16().Should().Be((ushort)(IntPtr.Size == 4 ? 0x014C : 0x8664));
    }

    [Fact]
    public void NativeStructuresShouldMatchWindowsAbi()
    {
        Marshal.SizeOf<ActCtx>().Should().Be(IntPtr.Size == 4 ? 32 : 56);
        Marshal.SizeOf<NativeMethods.MSG>().Should().Be(IntPtr.Size == 4 ? 32 : 48);
        Marshal.OffsetOf<NativeMethods.MSG>(nameof(NativeMethods.MSG.wParam))
            .Should().Be(new IntPtr(IntPtr.Size == 4 ? 8 : 16));
        Marshal.OffsetOf<ActCtx>(nameof(ActCtx.lpAssemblyDirectory))
            .Should().Be(new IntPtr(IntPtr.Size == 4 ? 16 : 24));
    }
}
#pragma warning restore SA1121 // Use built-in type alias
