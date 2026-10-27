using System.Runtime.InteropServices;
using System.Security;
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

#pragma warning disable SA1121 // Use built-in type alias
#pragma warning disable MA0204
public partial class MessagePumpTest
#pragma warning restore MA0204
{
    private const uint WmQuit = 0x0012;
    private const uint PmNoRemove = 0x0000;

    [Theory]
    [InlineData(0)]
    [InlineData(42)]
    [InlineData(-1)]
    public void PumpPendingMessagesShouldPreserveQuitAndExitCode(int exitCode)
    {
        NativeMethods.PostQuitMessage(exitCode);
        try
        {
            NativeMethods.PumpPendingMessages();

            var pending = NativeMethods.PeekMessage(
                out var message,
                IntPtr.Zero,
                0,
                0,
                PmNoRemove);
            pending.Should().BeTrue();
            message.message.Should().Be(WmQuit);
            unchecked((int)message.wParam.ToUInt64()).Should().Be(exitCode);
        }
        finally
        {
            _ = NativeMethods.PeekMessage(
                out _,
                IntPtr.Zero,
                WmQuit,
                WmQuit,
                NativeMethods.PM_REMOVE);
        }
    }

    [Fact]
    public void PumpPendingMessagesShouldLeaveMessagesBeyondBudgetQueued()
    {
        TestWindow.OnStaThread(() =>
        {
            using var window = new TestWindow();
            const int budget = NativeMethods.MaxMessagesPerPump;
            for (var i = 0; i <= budget; i++)
            {
                window.Post();
            }

            NativeMethods.PumpPendingMessages();
            window.DeliveryCount.Should().Be(budget);
            NativeMethods.PeekMessage(out _, IntPtr.Zero, 0, 0, PmNoRemove).Should().BeTrue();

            NativeMethods.PumpPendingMessages();
            window.DeliveryCount.Should().Be(budget + 1);
            NativeMethods.PeekMessage(out _, IntPtr.Zero, 0, 0, PmNoRemove).Should().BeFalse();
        });
    }

    [Fact]
    public void PumpPendingMessagesShouldTranslateAndDispatchQueuedMessage()
    {
        TestWindow.OnStaThread(() =>
        {
            using var window = new TestWindow();
            window.Post(wParam: 42, lParam: -7);
            var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();

            var result = Executor.Execute(comAssemblyPath, manifestPath, () => { });

            result.Success.Should().BeTrue();
            window.DeliveryCount.Should().Be(1);
            window.LastWParam.Should().Be(new UIntPtr(42));
            window.LastLParam.Should().Be(new IntPtr(-7));
        });
    }

    [Theory]
    [InlineData(0x4E2D)]
    [InlineData(0x03A9)]
    [InlineData(0x0416)]
    public void PumpPendingMessagesShouldPreserveUnicodeCharacters(int character)
    {
        TestWindow.OnStaThread(() =>
        {
            using var window = new TestWindow();
            window.Post(TestWindow.CharacterMessage, (uint)character);

            NativeMethods.PumpPendingMessages();

            window.DeliveryCount.Should().Be(1);
            window.LastWParam.Should().Be(new UIntPtr((uint)character));
        });
    }

    [Fact]
    public void DisabledPumpingShouldPreserveCustomHostThreadMessages()
    {
        TestWindow.OnStaThread(() =>
        {
            const uint hostMessage = 0x8123;
            _ = NativeMethods.PeekMessage(out _, IntPtr.Zero, 0, 0, PmNoRemove);
            SafeNativeMethods.PostThreadMessage(
                NativeMethods.GetCurrentThreadId(), hostMessage, new IntPtr(42), new IntPtr(-7)).Should().BeTrue();
            var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
            var executor = new ComExecutor(pumpPendingMessages: false);

            executor.Execute(comAssemblyPath, manifestPath, () => { }).Success.Should().BeTrue();
            var creation = executor.Create(comAssemblyPath, manifestPath, () => new object());
            creation.Success.Should().BeTrue();
            creation.Value!.Dispose();

            NativeMethods.PeekMessage(
                out var message,
                IntPtr.Zero,
                hostMessage,
                hostMessage,
                NativeMethods.PM_REMOVE).Should().BeTrue();
            message.wParam.Should().Be(new UIntPtr(42));
            message.lParam.Should().Be(new IntPtr(-7));
        });
    }

#if NET8_0_OR_GREATER
    [SuppressUnmanagedCodeSecurity]
    internal static partial class SafeNativeMethods
    {
        [LibraryImport("user32.dll", EntryPoint = "PostThreadMessageW", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static partial bool PostThreadMessage(
            uint idThread,
            uint msg,
            IntPtr wParam,
            IntPtr lParam);
    }
#else
    [SuppressUnmanagedCodeSecurity]
    internal static class SafeNativeMethods
    {
        [DllImport("user32.dll", EntryPoint = "PostThreadMessageW", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool PostThreadMessage(
            uint idThread,
            uint msg,
            IntPtr wParam,
            IntPtr lParam);
    }
#endif
}
#pragma warning restore SA1121 // Use built-in type alias
