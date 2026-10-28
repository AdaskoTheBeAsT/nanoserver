using System.Runtime.InteropServices;
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

#pragma warning disable SA1121 // Use built-in type alias
#pragma warning disable MA0051 // Method is too long
public class NanoServerWindowManagerTest
{
    private const uint WmUser = 0x0400;
    private const uint WmQuit = 0x0012;
    private const uint PmRemove = 0x0001;
    private const int GwlpWndProc = -4;
    private const int GwlpHInstance = -6;
    private const int GwlpId = -12;
    private const int GwlStyle = -16;
    private const int GwlExStyle = -20;
    private const int GwlpUserData = -21;
    private const int UoiName = 2;

    private static readonly WndProcDelegate CustomWndProc = CustomWndProcCallback;
    private static readonly IntPtr CustomWndProcPtr = Marshal.GetFunctionPointerForDelegate(CustomWndProc);
    private static int _lastObservedInSendMessage;

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate IntPtr WndProcDelegate(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [Fact]
    public void ClassRegistrationAndWindowCreationShouldHandleAtomsStringsAndErrors()
    {
        NanoServerWindowManager.RegisterClassWImpl(IntPtr.Zero).Should().Be(0);
        NanoServerWindowManager.UnregisterClassWImpl(IntPtr.Zero, IntPtr.Zero).Should().Be(0);

        using var emptyClass = AllocateWndClass(IntPtr.Zero, IntPtr.Zero, wndExtra: 0, new IntPtr(0x1234));
        NanoServerWindowManager.RegisterClassWImpl(emptyClass.Pointer).Should().Be(0);

        using var atomClass = AllocateWndClass(new IntPtr(0xBEEF), CustomWndProcPtr, wndExtra: 16, new IntPtr(0x1111));
        var atomFromInt = NanoServerWindowManager.RegisterClassWImpl(atomClass.Pointer);
        atomFromInt.Should().NotBe(0);
        NanoServerWindowManager.UnregisterClassWImpl(new IntPtr(atomFromInt), IntPtr.Zero).Should().Be(1);
        NanoServerWindowManager.UnregisterClassWImpl(new IntPtr(0xFFFF), IntPtr.Zero).Should().Be(1);

        var className = "NanoTestClass_" + Guid.NewGuid().ToString("N");
        var classNamePtr = Marshal.StringToHGlobalUni(className);
        var unknownNamePtr = Marshal.StringToHGlobalUni("UnknownClass_" + Guid.NewGuid().ToString("N"));
        try
        {
            NanoServerWindowManager.CreateWindowExWImpl(
                0,
                IntPtr.Zero,
                IntPtr.Zero,
                0,
                0,
                0,
                0,
                0,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero).Should().Be(IntPtr.Zero);

            NanoServerWindowManager.CreateWindowExWImpl(
                0,
                new IntPtr(0xABCD),
                IntPtr.Zero,
                0,
                0,
                0,
                0,
                0,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero).Should().Be(IntPtr.Zero);

            NanoServerWindowManager.CreateWindowExWImpl(
                0,
                unknownNamePtr,
                IntPtr.Zero,
                0,
                0,
                0,
                0,
                0,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero).Should().Be(IntPtr.Zero);

            using var namedClass = AllocateWndClass(classNamePtr, CustomWndProcPtr, wndExtra: 32, new IntPtr(0x2222));
            var atom1 = NanoServerWindowManager.RegisterClassWImpl(namedClass.Pointer);
            var atom2 = NanoServerWindowManager.RegisterClassWImpl(namedClass.Pointer);
            atom1.Should().NotBe(0);
            atom2.Should().Be(atom1);

            var parentHwnd = NanoServerWindowManager.CreateWindowExWImpl(
                0x10u,
                new IntPtr(atom1),
                IntPtr.Zero,
                0x20u,
                0,
                0,
                100,
                100,
                IntPtr.Zero,
                new IntPtr(10),
                IntPtr.Zero,
                IntPtr.Zero);
            parentHwnd.Should().NotBe(IntPtr.Zero);

            var childHwnd = NanoServerWindowManager.CreateWindowExWImpl(
                0x30u,
                classNamePtr,
                IntPtr.Zero,
                0x40u,
                0,
                0,
                50,
                50,
                parentHwnd,
                new IntPtr(20),
                new IntPtr(0x3333),
                IntPtr.Zero);
            childHwnd.Should().NotBe(IntPtr.Zero);

            NanoServerWindowManager.IsWindowImpl(parentHwnd).Should().Be(1);
            NanoServerWindowManager.IsWindowImpl(new IntPtr(0xDEAD)).Should().Be(0);
            NanoServerWindowManager.IsChildImpl(parentHwnd, childHwnd).Should().Be(1);
            NanoServerWindowManager.IsChildImpl(IntPtr.Zero, parentHwnd).Should().Be(0);
            NanoServerWindowManager.IsChildImpl(childHwnd, parentHwnd).Should().Be(0);
            NanoServerWindowManager.IsChildImpl(parentHwnd, new IntPtr(0xDEAD)).Should().Be(0);

            NanoServerWindowManager.DefWindowProcWImpl(parentHwnd, WmUser, IntPtr.Zero, IntPtr.Zero).Should().Be(IntPtr.Zero);

            NanoServerWindowManager.DestroyWindowImpl(childHwnd).Should().Be(1);
            NanoServerWindowManager.DestroyWindowImpl(parentHwnd).Should().Be(1);
            NanoServerWindowManager.DestroyWindowImpl(parentHwnd).Should().Be(0);

            NanoServerWindowManager.UnregisterClassWImpl(classNamePtr, IntPtr.Zero).Should().Be(1);
            NanoServerWindowManager.UnregisterClassWImpl(classNamePtr, IntPtr.Zero).Should().Be(1);
        }
        finally
        {
            Marshal.FreeHGlobal(unknownNamePtr);
            Marshal.FreeHGlobal(classNamePtr);
        }
    }

    [Fact]
    public void WindowLongAndThreadProcessIdShouldReadAndUpdateAllFields()
    {
        var classNamePtr = Marshal.StringToHGlobalUni("NanoLongClass_" + Guid.NewGuid().ToString("N"));
        var pidPtr = Marshal.AllocHGlobal(4);
        try
        {
            using var wndClass = AllocateWndClass(classNamePtr, CustomWndProcPtr, wndExtra: 32, new IntPtr(0x1010));
            var atom = NanoServerWindowManager.RegisterClassWImpl(wndClass.Pointer);
            var hwnd = NanoServerWindowManager.CreateWindowExWImpl(
                0x11u,
                new IntPtr(atom),
                IntPtr.Zero,
                0x22u,
                0,
                0,
                0,
                0,
                IntPtr.Zero,
                new IntPtr(0x33),
                new IntPtr(0x44),
                IntPtr.Zero);

            NanoServerWindowManager.GetWindowLongPtrWImpl(new IntPtr(0xDEAD), GwlpUserData).Should().Be(IntPtr.Zero);
            NanoServerWindowManager.SetWindowLongPtrWImpl(new IntPtr(0xDEAD), GwlpUserData, new IntPtr(1)).Should().Be(IntPtr.Zero);
            NanoServerWindowManager.GetWindowLongWImpl(new IntPtr(0xDEAD), GwlStyle).Should().Be(0);
            NanoServerWindowManager.SetWindowLongWImpl(new IntPtr(0xDEAD), GwlStyle, 1).Should().Be(0);

            NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, 0, new IntPtr(0x1234)).Should().Be(IntPtr.Zero);
            NanoServerWindowManager.GetWindowLongPtrWImpl(hwnd, 0).Should().Be(new IntPtr(0x1234));
            NanoServerWindowManager.GetWindowLongPtrWImpl(hwnd, 4096).Should().Be(IntPtr.Zero);
            NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, 4096, new IntPtr(99)).Should().Be(IntPtr.Zero);

            NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, GwlpWndProc, CustomWndProcPtr).Should().Be(CustomWndProcPtr);
            NanoServerWindowManager.GetWindowLongPtrWImpl(hwnd, GwlpWndProc).Should().Be(CustomWndProcPtr);

            NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, GwlpHInstance, new IntPtr(0x55)).Should().Be(new IntPtr(0x44));
            NanoServerWindowManager.GetWindowLongPtrWImpl(hwnd, GwlpHInstance).Should().Be(new IntPtr(0x55));

            NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, GwlpId, new IntPtr(0x66)).Should().Be(new IntPtr(0x33));
            NanoServerWindowManager.GetWindowLongPtrWImpl(hwnd, GwlpId).Should().Be(new IntPtr(0x66));

            NanoServerWindowManager.SetWindowLongWImpl(hwnd, GwlStyle, 0x77).Should().Be(0x22);
            NanoServerWindowManager.GetWindowLongWImpl(hwnd, GwlStyle).Should().Be(0x77);

            NanoServerWindowManager.SetWindowLongWImpl(hwnd, GwlExStyle, 0x88).Should().Be(0x11);
            NanoServerWindowManager.GetWindowLongWImpl(hwnd, GwlExStyle).Should().Be(0x88);

            NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, GwlpUserData, new IntPtr(0x99)).Should().Be(IntPtr.Zero);
            NanoServerWindowManager.GetWindowLongPtrWImpl(hwnd, GwlpUserData).Should().Be(new IntPtr(0x99));

            NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, -999, new IntPtr(0xAA)).Should().Be(IntPtr.Zero);
            NanoServerWindowManager.GetWindowLongPtrWImpl(hwnd, -999).Should().Be(IntPtr.Zero);

            var currentTid = NativeMethods.GetCurrentThreadId();
            NanoServerWindowManager.GetWindowThreadProcessIdImpl(hwnd, IntPtr.Zero).Should().Be(currentTid);
            NanoServerWindowManager.GetWindowThreadProcessIdImpl(new IntPtr(0xDEAD), pidPtr).Should().Be(currentTid);
            Marshal.ReadInt32(pidPtr).Should().Be(unchecked((int)NativeMethods.GetCurrentProcessId()));

            _ = NanoServerWindowManager.DestroyWindowImpl(hwnd);
            _ = NanoServerWindowManager.UnregisterClassWImpl(new IntPtr(atom), IntPtr.Zero);
        }
        finally
        {
            Marshal.FreeHGlobal(pidPtr);
            Marshal.FreeHGlobal(classNamePtr);
        }
    }

    [Fact]
    public void MessageQueuePeekGetPostAndDispatchShouldHandleFiltersAndQuit()
    {
        var classNamePtr = Marshal.StringToHGlobalUni("NanoMsgClass_" + Guid.NewGuid().ToString("N"));
        var msgPtr = Marshal.AllocHGlobal(IntPtr.Size * 8);
        try
        {
            using var wndClass = AllocateWndClass(classNamePtr, CustomWndProcPtr, wndExtra: 0, IntPtr.Zero);
            var atom = NanoServerWindowManager.RegisterClassWImpl(wndClass.Pointer);
            var hwnd = NanoServerWindowManager.CreateWindowExWImpl(
                0,
                new IntPtr(atom),
                IntPtr.Zero,
                0,
                0,
                0,
                0,
                0,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero);

            while (NanoServerWindowManager.PeekMessageWImpl(IntPtr.Zero, IntPtr.Zero, 0, 0, PmRemove) != 0)
            {
                // Drain any prior messages on this thread.
            }

            NanoServerWindowManager.GetQueueStatusImpl(0).Should().Be(0u);
            NanoServerWindowManager.PeekMessageWImpl(IntPtr.Zero, IntPtr.Zero, 0, 0, 0).Should().Be(0);
            NanoServerWindowManager.PeekMessageWImpl(msgPtr, IntPtr.Zero, 0, 0, 0).Should().Be(0);

            NanoServerWindowManager.PostMessageWImpl(new IntPtr(0xDEAD), WmUser, IntPtr.Zero, IntPtr.Zero).Should().Be(0);
            NanoServerWindowManager.PostMessageWImpl(IntPtr.Zero, WmUser + 1, new IntPtr(1), new IntPtr(2)).Should().Be(1);
            NanoServerWindowManager.PostMessageWImpl(hwnd, WmUser + 2, new IntPtr(10), new IntPtr(20)).Should().Be(1);
            NanoServerWindowManager.GetQueueStatusImpl(0).Should().Be(0x00FF00FFu);

            // Non-matching window and range filters should return 0 without removing.
            NanoServerWindowManager.PeekMessageWImpl(msgPtr, new IntPtr(0xBEEF), 0, 0, 0).Should().Be(0);
            NanoServerWindowManager.PeekMessageWImpl(msgPtr, IntPtr.Zero, WmUser + 50, WmUser + 60, 0).Should().Be(0);

            // Filter by thread messages (hWnd = -1) without removing, then with remove.
            NanoServerWindowManager.PeekMessageWImpl(msgPtr, new IntPtr(-1), WmUser, WmUser + 5, 0).Should().Be(1);
            NanoServerWindowManager.PeekMessageWImpl(IntPtr.Zero, new IntPtr(-1), WmUser, WmUser + 5, PmRemove).Should().Be(1);

            // Peek window message and dispatch it.
            NanoServerWindowManager.PeekMessageWImpl(msgPtr, hwnd, 0, 0, PmRemove).Should().Be(1);
            NanoServerWindowManager.TranslateMessageImpl(msgPtr).Should().Be(0);
            NanoServerWindowManager.DispatchMessageWImpl(IntPtr.Zero).Should().Be(IntPtr.Zero);
            NanoServerWindowManager.DispatchMessageWImpl(msgPtr).Should().Be(new IntPtr(30));

            // Window with null WndProc should return IntPtr.Zero on dispatch.
            _ = NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, GwlpWndProc, IntPtr.Zero);
            NanoServerWindowManager.DispatchMessageWImpl(msgPtr).Should().Be(IntPtr.Zero);
            _ = NanoServerWindowManager.SetWindowLongPtrWImpl(hwnd, GwlpWndProc, CustomWndProcPtr);

            // PostThreadMessageW + GetMessageW + PostQuitMessage.
            var currentTid = NativeMethods.GetCurrentThreadId();
            var poster = new Thread(() =>
            {
                Thread.SpinWait(50000);
                _ = NanoServerWindowManager.PostThreadMessageWImpl(currentTid, WmUser + 3, new IntPtr(7), new IntPtr(8));
            })
            {
                IsBackground = true,
            };
            poster.Start();
            NanoServerWindowManager.GetMessageWImpl(msgPtr, IntPtr.Zero, 0, 0).Should().Be(1);
            poster.Join();

            NanoServerWindowManager.PostQuitMessageImpl(0);
            NanoServerWindowManager.GetMessageWImpl(msgPtr, IntPtr.Zero, 0, 0).Should().Be(0);

            _ = NanoServerWindowManager.DestroyWindowImpl(hwnd);
            _ = NanoServerWindowManager.UnregisterClassWImpl(new IntPtr(atom), IntPtr.Zero);
        }
        finally
        {
            Marshal.FreeHGlobal(msgPtr);
            Marshal.FreeHGlobal(classNamePtr);
        }
    }

    [Fact]
    public void SendMessageAndHostMessageLoopShouldSupportSameThreadCrossThreadAndTimeouts()
    {
        NanoServerWindowManager.SendMessageWImpl(new IntPtr(0xDEAD), WmUser, IntPtr.Zero, IntPtr.Zero).Should().Be(IntPtr.Zero);
        NanoServerWindowManager.InSendMessageExImpl(IntPtr.Zero).Should().Be(0u);
        NanoServerWindowManager.WakeThreadQueue(0xFFFFFFFEu);

        var classNamePtr = Marshal.StringToHGlobalUni("NanoSendClass_" + Guid.NewGuid().ToString("N"));
        try
        {
            using var wndClass = AllocateWndClass(classNamePtr, CustomWndProcPtr, wndExtra: 0, IntPtr.Zero);
            var atom = NanoServerWindowManager.RegisterClassWImpl(wndClass.Pointer);

            var hostTid = NativeMethods.GetCurrentThreadId();
            var hostHwnd = NanoServerWindowManager.CreateWindowExWImpl(
                0,
                new IntPtr(atom),
                IntPtr.Zero,
                0,
                0,
                0,
                0,
                0,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero,
                IntPtr.Zero);
            NanoServerWindowManager.SendMessageWImpl(hostHwnd, WmUser, new IntPtr(3), new IntPtr(4)).Should().Be(new IntPtr(7));

            using var stopEvent = new ManualResetEventSlim(initialState: false);
            var crossResultValue = 0L;
            _ = Interlocked.Exchange(ref _lastObservedInSendMessage, 0);

            var sender = new Thread(() =>
            {
                var crossResult = NanoServerWindowManager.SendMessageWImpl(hostHwnd, WmUser, new IntPtr(100), new IntPtr(200));
                _ = Interlocked.Exchange(ref crossResultValue, crossResult.ToInt64());
                _ = NanoServerWindowManager.PostMessageWImpl(hostHwnd, WmUser, new IntPtr(5), new IntPtr(6));
                _ = NanoServerWindowManager.SendMessageWImpl(hostHwnd, WmUser, new IntPtr(1), new IntPtr(2));
                stopEvent.Set();
                NanoServerWindowManager.WakeThreadQueue(hostTid);
            })
            {
                IsBackground = true,
            };
            sender.Start();
            NanoServerWindowManager.RunHostMessageLoop(stopEvent);
            sender.Join(5000).Should().BeTrue();

            Interlocked.Read(ref crossResultValue).Should().Be(300L);
            Volatile.Read(ref _lastObservedInSendMessage).Should().Be(1);

            // When host loop is no longer running, cross-thread SendMessage should time out and invoke directly.
            var prevTimeout = NanoServerWindowManager.CrossThreadSendTimeoutMs;
            try
            {
                NanoServerWindowManager.CrossThreadSendTimeoutMs = 10;
                var timeoutResultValue = 0L;
                var timeoutSender = new Thread(() =>
                {
                    var res = NanoServerWindowManager.SendMessageWImpl(hostHwnd, WmUser, new IntPtr(8), new IntPtr(9));
                    _ = Interlocked.Exchange(ref timeoutResultValue, res.ToInt64());
                })
                {
                    IsBackground = true,
                };
                timeoutSender.Start();
                timeoutSender.Join(5000).Should().BeTrue();
                Interlocked.Read(ref timeoutResultValue).Should().Be(17L);
            }
            finally
            {
                NanoServerWindowManager.CrossThreadSendTimeoutMs = prevTimeout;
            }

            // Also verify RunHostMessageLoop exits when WmQuit is posted or stopEvent is set during dequeue.
            using var quitStopEvent = new ManualResetEventSlim(initialState: false);
            _ = NanoServerWindowManager.PostMessageWImpl(IntPtr.Zero, WmQuit, IntPtr.Zero, IntPtr.Zero);
            NanoServerWindowManager.RunHostMessageLoop(quitStopEvent);

            using var midLoopStopEvent = new ManualResetEventSlim(initialState: false);
            var stopActionWndProc = new WndProcDelegate((_, _, _, _) =>
            {
                midLoopStopEvent.Set();
                return IntPtr.Zero;
            });
            var stopActionPtr = Marshal.GetFunctionPointerForDelegate(stopActionWndProc);
            _ = NanoServerWindowManager.SetWindowLongPtrWImpl(hostHwnd, GwlpWndProc, stopActionPtr);
            _ = NanoServerWindowManager.PostMessageWImpl(hostHwnd, WmUser, IntPtr.Zero, IntPtr.Zero);
            NanoServerWindowManager.RunHostMessageLoop(midLoopStopEvent);
            GC.KeepAlive(stopActionWndProc);

            _ = NanoServerWindowManager.DestroyWindowImpl(hostHwnd);
            _ = NanoServerWindowManager.UnregisterClassWImpl(new IntPtr(atom), IntPtr.Zero);
        }
        finally
        {
            Marshal.FreeHGlobal(classNamePtr);
        }
    }

    [Fact]
    public void TimersWaitUserObjectsClipboardAndMiscExportsShouldWorkAsExpected()
    {
        NanoServerWindowManager.IsGuiThreadImpl(0).Should().Be(1);
        NanoServerWindowManager.AllowSetForegroundWindowImpl(0).Should().Be(1);
        NanoServerWindowManager.IsThreadMessageQueueAttachedImpl().Should().Be(1);
        NanoServerWindowManager.SetThreadInputBlockedImpl(0).Should().Be(1);
        NanoServerWindowManager.MessageBeepImpl(0).Should().Be(1);

        var timer1 = NanoServerWindowManager.SetTimerImpl(IntPtr.Zero, IntPtr.Zero, 100, IntPtr.Zero);
        var timer2 = NanoServerWindowManager.SetCoalescableTimerImpl(IntPtr.Zero, new IntPtr(42), 100, IntPtr.Zero, 10);
        timer1.Should().NotBe(IntPtr.Zero);
        timer2.Should().Be(new IntPtr(42));
        NanoServerWindowManager.KillTimerImpl(IntPtr.Zero, timer1).Should().Be(1);
        NanoServerWindowManager.KillTimerImpl(IntPtr.Zero, timer2).Should().Be(1);

        // MsgWaitForMultipleObjectsEx when queue has a message vs when queue is empty.
        while (NanoServerWindowManager.PeekMessageWImpl(IntPtr.Zero, IntPtr.Zero, 0, 0, PmRemove) != 0)
        {
            // Drain queue.
        }

        _ = NanoServerWindowManager.PostMessageWImpl(IntPtr.Zero, WmUser, IntPtr.Zero, IntPtr.Zero);
        NanoServerWindowManager.MsgWaitForMultipleObjectsExImpl(2, IntPtr.Zero, 0, 0, 0).Should().Be(2u);
        _ = NanoServerWindowManager.PeekMessageWImpl(IntPtr.Zero, IntPtr.Zero, 0, 0, PmRemove);

        var nativeEvent = NativeMethods.CreateEventW(IntPtr.Zero, bManualReset: true, bInitialState: true, lpName: IntPtr.Zero);
        var handlesBuffer = Marshal.AllocHGlobal(IntPtr.Size);
        try
        {
            Marshal.WriteIntPtr(handlesBuffer, nativeEvent);
            NanoServerWindowManager.MsgWaitForMultipleObjectsExImpl(1, handlesBuffer, 0, 0, 0).Should().Be(0u);
            NanoServerWindowManager.MsgWaitForMultipleObjectsExImpl(0, IntPtr.Zero, 0, 0, 3u).Should().NotBe(uint.MaxValue);
        }
        finally
        {
            Marshal.FreeHGlobal(handlesBuffer);
        }

        // Window station, desktop, and user object information.
        var winSta = NanoServerWindowManager.GetProcessWindowStationImpl();
        var desktop = NanoServerWindowManager.GetThreadDesktopImpl(0);
        winSta.Should().NotBe(IntPtr.Zero);
        desktop.Should().NotBe(IntPtr.Zero);

        var neededPtr = Marshal.AllocHGlobal(4);
        var infoBuffer = Marshal.AllocHGlobal(64);
        try
        {
            NanoServerWindowManager.GetUserObjectInformationWImpl(winSta, 1, IntPtr.Zero, 0, IntPtr.Zero).Should().Be(1);
            NanoServerWindowManager.GetUserObjectInformationWImpl(winSta, 1, IntPtr.Zero, 0, neededPtr).Should().Be(1);
            Marshal.ReadInt32(neededPtr).Should().Be(0);

            NanoServerWindowManager.GetUserObjectInformationWImpl(winSta, UoiName, IntPtr.Zero, 0, neededPtr).Should().Be(0);
            Marshal.ReadInt32(neededPtr).Should().BePositive();
            NanoServerWindowManager.GetUserObjectInformationWImpl(winSta, UoiName, infoBuffer, 2, IntPtr.Zero).Should().Be(0);

            NanoServerWindowManager.GetUserObjectInformationWImpl(winSta, UoiName, infoBuffer, 64, neededPtr).Should().Be(1);
            Marshal.PtrToStringUni(infoBuffer).Should().Be("WinSta0");

            NanoServerWindowManager.GetUserObjectInformationWImpl(desktop, UoiName, infoBuffer, 64, IntPtr.Zero).Should().Be(1);
            Marshal.PtrToStringUni(infoBuffer).Should().Be("Default");

            // Clipboard formats.
            NanoServerWindowManager.RegisterClipboardFormatWImpl(IntPtr.Zero).Should().Be(0u);
            var emptyStrPtr = Marshal.StringToHGlobalUni(string.Empty);
            var formatNamePtr = Marshal.StringToHGlobalUni("CustomClipboardFormat_" + Guid.NewGuid().ToString("N"));
            try
            {
                NanoServerWindowManager.RegisterClipboardFormatWImpl(emptyStrPtr).Should().Be(0u);
                var fmtId1 = NanoServerWindowManager.RegisterClipboardFormatWImpl(formatNamePtr);
                var fmtId2 = NanoServerWindowManager.RegisterClipboardFormatWImpl(formatNamePtr);
                fmtId1.Should().BeGreaterThan(0u);
                fmtId2.Should().Be(fmtId1);

                NanoServerWindowManager.GetClipboardFormatNameWImpl(fmtId1, IntPtr.Zero, 32).Should().Be(0);
                NanoServerWindowManager.GetClipboardFormatNameWImpl(fmtId1, infoBuffer, 0).Should().Be(0);
                NanoServerWindowManager.GetClipboardFormatNameWImpl(fmtId1, infoBuffer, 32).Should().BePositive();
                NanoServerWindowManager.GetClipboardFormatNameWImpl(0x9999u, infoBuffer, 32).Should().BePositive();
                Marshal.PtrToStringUni(infoBuffer).Should().Be("Format_9999");
            }
            finally
            {
                Marshal.FreeHGlobal(formatNamePtr);
                Marshal.FreeHGlobal(emptyStrPtr);
            }
        }
        finally
        {
            Marshal.FreeHGlobal(infoBuffer);
            Marshal.FreeHGlobal(neededPtr);
        }

        TestWindow.OnStaThread(() =>
        {
            var hr = NanoServerWindowManager.CoInitializeImpl(IntPtr.Zero);
            hr.Should().BeGreaterThanOrEqualTo(0);
            NativeMethods.CoUninitialize();
        });
    }

    [Fact]
    public void NanoServerStaShimCallbacksShouldHandleApiSetPresenceDelayLoadAndModuleHandles()
    {
        var prevLibSta = Environment.GetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable);
        var prevOsSta = Environment.GetEnvironmentVariable(NanoServerOptions.DisableOsStaShimEnvironmentVariable);
        try
        {
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, "1");
            NanoServerOptions.IsEnvironmentFlagTrue(NanoServerOptions.DisableStaShimEnvironmentVariable).Should().BeTrue();
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, "true");
            NanoServerOptions.IsEnvironmentFlagTrue(NanoServerOptions.DisableStaShimEnvironmentVariable).Should().BeTrue();
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, "yes");
            NanoServerOptions.IsEnvironmentFlagTrue(NanoServerOptions.DisableStaShimEnvironmentVariable).Should().BeTrue();
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, "0");
            NanoServerOptions.IsEnvironmentFlagTrue(NanoServerOptions.DisableStaShimEnvironmentVariable).Should().BeFalse();
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, value: null);
            NanoServerOptions.IsEnvironmentFlagTrue(NanoServerOptions.DisableStaShimEnvironmentVariable).Should().BeFalse();
            NanoServerStaShim.ReadUnicodeString(IntPtr.Zero).Should().BeNull();

            var emptyUstr = Marshal.AllocHGlobal(IntPtr.Size * 2);
            var presentPtr = Marshal.AllocHGlobal(4);
            var schemaPtr = Marshal.AllocHGlobal(4);
            try
            {
                Marshal.WriteInt16(emptyUstr, 0, 0);
                Marshal.WriteInt16(emptyUstr, 2, 0);
                Marshal.WriteIntPtr(emptyUstr, IntPtr.Size, IntPtr.Zero);
                NanoServerStaShim.ReadUnicodeString(emptyUstr).Should().BeEmpty();

                using var shimmedUstr = AllocateUnicodeString("ext-ms-win-ntuser-window-l1-1-0");
                using var unshimmedUstr = AllocateUnicodeString("api-ms-win-core-synch-l1-1-0");
                NanoServerStaShim.ReadUnicodeString(shimmedUstr.Pointer).Should().Be("ext-ms-win-ntuser-window-l1-1-0");

                NanoServerOptions.ForceInProcessStaShim = true;
                NanoServerOptions.EnableStaShim = true;
                NanoServerOptions.RefreshStaState();
                NanoServerStaShim.IsAnyStaProviderActive().Should().BeTrue();

                NanoServerStaShim.OnApiSetQueryApiSetPresence(shimmedUstr.Pointer, IntPtr.Zero).Should().Be(0);
                NanoServerStaShim.OnApiSetQueryApiSetPresence(shimmedUstr.Pointer, presentPtr).Should().Be(0);
                Marshal.ReadByte(presentPtr).Should().Be(1);

                NanoServerStaShim.OnApiSetQueryApiSetPresenceEx(shimmedUstr.Pointer, IntPtr.Zero, IntPtr.Zero).Should().Be(0);
                NanoServerStaShim.OnApiSetQueryApiSetPresenceEx(shimmedUstr.Pointer, schemaPtr, presentPtr).Should().Be(0);
                Marshal.ReadByte(schemaPtr).Should().Be(1);
                Marshal.ReadByte(presentPtr).Should().Be(1);

                _ = NanoServerStaShim.OnApiSetQueryApiSetPresence(unshimmedUstr.Pointer, presentPtr);
                _ = NanoServerStaShim.OnApiSetQueryApiSetPresenceEx(unshimmedUstr.Pointer, schemaPtr, presentPtr);

                // Module handle resolution for shimmed vs non-shimmed modules.
                var shimmedModPtr = Marshal.StringToHGlobalUni("ext-ms-win-ntuser-window-l1-1-0");
                var kernelModPtr = Marshal.StringToHGlobalUni("kernel32.dll");
                try
                {
                    _ = NanoServerStaShim.OnGetModuleHandleW(IntPtr.Zero);
                    _ = NanoServerStaShim.OnGetModuleHandleW(kernelModPtr);
                    NanoServerStaShim.OnGetModuleHandleW(shimmedModPtr).Should().NotBe(IntPtr.Zero);

                    // Inactive state with OS shim disabled.
                    Environment.SetEnvironmentVariable(NanoServerOptions.DisableOsStaShimEnvironmentVariable, "1");
                    NanoServerOptions.ForceInProcessStaShim = false;
                    NanoServerOptions.EnableStaShim = false;
                    NanoServerOptions.RefreshStaState();
                    NanoServerStaShim.IsAnyStaProviderActive().Should().BeFalse();
                    NanoServerStaShim.OnApiSetQueryApiSetPresence(shimmedUstr.Pointer, presentPtr).Should().Be(0);
                    Marshal.ReadByte(presentPtr).Should().Be(0);
                    NanoServerStaShim.OnApiSetQueryApiSetPresenceEx(shimmedUstr.Pointer, schemaPtr, presentPtr).Should().Be(0);
                    Marshal.ReadByte(presentPtr).Should().Be(0);
                    NanoServerStaShim.OnGetModuleHandleW(shimmedModPtr).Should().Be(IntPtr.Zero);

                    // Inactive in-process shim with OS shim not disabled.
                    Environment.SetEnvironmentVariable(NanoServerOptions.DisableOsStaShimEnvironmentVariable, value: null);
                    _ = NanoServerStaShim.IsAnyStaProviderActive();
                    _ = NanoServerStaShim.OnGetModuleHandleW(shimmedModPtr);
                }
                finally
                {
                    Marshal.FreeHGlobal(kernelModPtr);
                    Marshal.FreeHGlobal(shimmedModPtr);
                }

                // Delay-load resolution tests.
                NanoServerOptions.ForceInProcessStaShim = true;
                NanoServerOptions.EnableStaShim = true;
                NanoServerOptions.RefreshStaState();
                VerifyDelayLoadResolution();
            }
            finally
            {
                Marshal.FreeHGlobal(schemaPtr);
                Marshal.FreeHGlobal(presentPtr);
                Marshal.FreeHGlobal(emptyUstr);
            }
        }
        finally
        {
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, prevLibSta);
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableOsStaShimEnvironmentVariable, prevOsSta);
            NanoServerOptions.ResetToEnvironmentDefaults();
        }
    }

    private static void VerifyDelayLoadResolution()
    {
        var moduleBuf = Marshal.AllocHGlobal(1024);
        try
        {
            for (var i = 0; i < 1024; i++)
            {
                Marshal.WriteByte(moduleBuf, i, 0);
            }

            // Layout inside fake module buffer:
            // +0x040: Delayload descriptor (32 bytes)
            // +0x100: DLL name ("ext-ms-win-ntuser-window-l1-1-0.dll")
            // +0x180: Unshimmed DLL name ("kernel32.dll")
            // +0x200: IAT (4 slots)
            // +0x280: INT (4 slots)
            // +0x300: IMAGE_IMPORT_BY_NAME for "RegisterClassW" (2-byte hint + ASCII string)
            // +0x340: IMAGE_IMPORT_BY_NAME for "UnknownExport"
            // +0x380: IMAGE_IMPORT_BY_NAME for "" (empty string)
            var descriptorPtr = new IntPtr(moduleBuf.ToInt64() + 0x040);
            WriteAscii(moduleBuf, 0x100, "ext-ms-win-ntuser-window-l1-1-0.dll");
            WriteAscii(moduleBuf, 0x180, "kernel32.dll");
            WriteAscii(moduleBuf, 0x302, "RegisterClassW");
            WriteAscii(moduleBuf, 0x342, "UnknownExport");

            Marshal.WriteInt32(descriptorPtr, 4, 0x100);
            Marshal.WriteInt32(descriptorPtr, 12, 0x200);
            Marshal.WriteInt32(descriptorPtr, 16, 0x280);
            var iatSlot0 = new IntPtr(moduleBuf.ToInt64() + 0x200);

            // Unaligned / before-IAT thunk address -> IntPtr.Zero.
            NanoServerStaShim.TryResolveShimmedThunk(moduleBuf, descriptorPtr, new IntPtr(moduleBuf.ToInt64() + 0x1FC))
                .Should().Be(IntPtr.Zero);
            NanoServerStaShim.TryResolveShimmedThunk(moduleBuf, descriptorPtr, new IntPtr(moduleBuf.ToInt64() + 0x201))
                .Should().Be(IntPtr.Zero);

            // Slot 0: zero INT entry -> IntPtr.Zero.
            Marshal.WriteIntPtr(moduleBuf, 0x280, IntPtr.Zero);
            NanoServerStaShim.TryResolveShimmedThunk(moduleBuf, descriptorPtr, iatSlot0).Should().Be(IntPtr.Zero);

            // Slot 0: empty function name -> IntPtr.Zero.
            Marshal.WriteIntPtr(moduleBuf, 0x280, new IntPtr(0x380));
            NanoServerStaShim.TryResolveShimmedThunk(moduleBuf, descriptorPtr, iatSlot0).Should().Be(IntPtr.Zero);

            // Slot 0: unknown function name -> IntPtr.Zero.
            Marshal.WriteIntPtr(moduleBuf, 0x280, new IntPtr(0x340));
            NanoServerStaShim.TryResolveShimmedThunk(moduleBuf, descriptorPtr, iatSlot0).Should().Be(IntPtr.Zero);

            // Slot 0: valid function name ("RegisterClassW") -> resolves and writes pointer to IAT slot!
            Marshal.WriteIntPtr(moduleBuf, 0x280, new IntPtr(0x300));
            var resolved = NanoServerStaShim.OnResolveDelayLoadedApi(moduleBuf, descriptorPtr, IntPtr.Zero, IntPtr.Zero, iatSlot0, 0);
            resolved.Should().Be(NanoServerWindowManager.ResolveFunction("RegisterClassW"));
            Marshal.ReadIntPtr(iatSlot0).Should().Be(resolved);
        }
        finally
        {
            Marshal.FreeHGlobal(moduleBuf);
        }
    }

    private static void WriteAscii(IntPtr basePtr, int offset, string text)
    {
        for (var i = 0; i < text.Length; i++)
        {
            Marshal.WriteByte(basePtr, offset + i, (byte)text[i]);
        }

        Marshal.WriteByte(basePtr, offset + text.Length, 0);
    }

    private static IntPtr CustomWndProcCallback(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        _ = hWnd;
        _ = msg;
        if (NanoServerWindowManager.InSendMessageExImpl(IntPtr.Zero) != 0)
        {
            _ = Interlocked.Exchange(ref _lastObservedInSendMessage, 1);
        }

        return new IntPtr(wParam.ToInt64() + lParam.ToInt64());
    }

    private static UnmanagedBuffer AllocateWndClass(IntPtr classNamePtr, IntPtr wndProc, int wndExtra, IntPtr hInstance)
    {
        var hInstanceOffset = (IntPtr.Size * 2) + 8;
        var classNameOffset = hInstanceOffset + (IntPtr.Size * 5);
        var totalSize = classNameOffset + IntPtr.Size;
        var ptr = Marshal.AllocHGlobal(totalSize);
        for (var i = 0; i < totalSize; i++)
        {
            Marshal.WriteByte(ptr, i, 0);
        }

        Marshal.WriteIntPtr(ptr, IntPtr.Size, wndProc);
        Marshal.WriteInt32(ptr, (IntPtr.Size * 2) + 4, wndExtra);
        Marshal.WriteIntPtr(ptr, hInstanceOffset, hInstance);
        Marshal.WriteIntPtr(ptr, classNameOffset, classNamePtr);
        return new UnmanagedBuffer(ptr);
    }

    private static UnmanagedBuffer AllocateUnicodeString(string value)
    {
        var strPtr = Marshal.StringToHGlobalUni(value);
        var ustrPtr = Marshal.AllocHGlobal(IntPtr.Size * 2);
        var byteLen = checked((short)(value.Length * 2));
        Marshal.WriteInt16(ustrPtr, 0, byteLen);
        Marshal.WriteInt16(ustrPtr, 2, checked((short)(byteLen + 2)));
        Marshal.WriteIntPtr(ustrPtr, IntPtr.Size, strPtr);
        return new UnmanagedBuffer(ustrPtr, strPtr);
    }

    private sealed class UnmanagedBuffer : IDisposable
    {
        private readonly IntPtr _secondary;

        internal UnmanagedBuffer(IntPtr pointer, IntPtr secondary = default)
        {
            Pointer = pointer;
            _secondary = secondary;
        }

        internal IntPtr Pointer { get; }

        public void Dispose()
        {
            if (Pointer != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(Pointer);
            }

            if (_secondary != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(_secondary);
            }
        }
    }
}
#pragma warning restore MA0051
#pragma warning restore SA1121
