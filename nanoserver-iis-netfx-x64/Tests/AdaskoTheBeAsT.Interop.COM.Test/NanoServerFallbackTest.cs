using System.ComponentModel;
using System.Runtime.InteropServices;
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

#pragma warning disable SA1121 // Use built-in type alias
#pragma warning disable IDISP007 // Tests own factory-created handles.
#pragma warning disable IDISP016 // Tests inspect partial cleanup and retry state.
#pragma warning disable SCS0018, SEC0116 // Temporary manifest files use controlled test paths.
public class NanoServerFallbackTest
{
    private const string ConcatenatorClsid = "{03d0aedd-2013-418a-8b92-e57c30f2ab26}";
    private const string SecondClsid = "{50091E2F-6707-4F87-AB4F-6C0A22159F9A}";

    private static string AssemblyPath => TestPaths.AssemblyPath;

    private static string ManifestPath => TestPaths.ManifestPath;

    [Theory]
    [InlineData(NativeMethods.ERROR_INVALID_FUNCTION)]
    [InlineData(NativeMethods.ERROR_NOT_SUPPORTED)]
    [InlineData(NativeMethods.ERROR_CALL_NOT_IMPLEMENTED)]
    public void ExecuteAndCreateShouldUseInProcClassFactoryFallbackWhenSxsIsUnavailable(int sxsErrorCode)
    {
        TestWindow.OnStaThread(() =>
        {
            var api = new SimulatedNanoServerApi(sxsErrorCode);
            var descriptors = new[] { new ComPathDescriptor(AssemblyPath, ManifestPath) };

            string? executeOutput = null;
            var executeResult = Executor.Execute(
                descriptors,
                () =>
                {
                    var instance = new NativeCOM.StringConcatenatorClass();
                    executeOutput = instance.ConcatStrings("Hello, ", "Nano Server!");
                },
                pumpPendingMessages: false,
                api);

            executeResult.Success.Should().BeTrue();
            executeOutput.Should().Be("Hello, Nano Server!");

            Action createOutsideContext = () => _ = new NativeCOM.StringConcatenatorClass();
            createOutsideContext.Should().Throw<COMException>();

            var creation = Executor.Create(
                descriptors,
                () => new NativeCOM.StringConcatenatorClass(),
                pumpPendingMessages: false,
                api);

            creation.Success.Should().BeTrue();
            using var handle = creation.Value!;
            handle.ActivationContextHandles.Should().ContainSingle()
                .Which.ToInt64().Should().BeNegative();
            handle.ActivationCookies.Should().ContainSingle()
                .Which.ToInt64().Should().BeNegative();
            handle.ComObject!.ConcatStrings("Create, ", "Fallback!").Should().Be("Create, Fallback!");

            var freeResult = Executor.Free(handle);
            freeResult.Success.Should().BeTrue();
            handle.IsReleased.Should().BeTrue();
            api.RevokedTokens.Should().HaveCount(2);
        });
    }

    [Fact]
    public void NestedFallbackContextsShouldEnforceLifoReleaseOrder()
    {
        TestWindow.OnStaThread(() =>
        {
            var api = new SimulatedNanoServerApi();
            var descriptors = new[] { new ComPathDescriptor(AssemblyPath, ManifestPath) };

            var outerCreation = Executor.Create(
                descriptors,
                () => new NativeCOM.StringConcatenatorClass(),
                pumpPendingMessages: false,
                api);
            outerCreation.Success.Should().BeTrue();
            using var outer = outerCreation.Value!;

            var innerCreation = Executor.Create(
                descriptors,
                () => new NativeCOM.StringConcatenatorClass(),
                pumpPendingMessages: false,
                api);
            innerCreation.Success.Should().BeTrue();
            using var inner = innerCreation.Value!;

            var outOfOrderRelease = Executor.Free(outer);
            outOfOrderRelease.Success.Should().BeFalse();
            outOfOrderRelease.Exception.Should().BeOfType<InvalidOperationException>();
            outer.IsReleased.Should().BeFalse();

            Executor.Free(inner).Success.Should().BeTrue();
            Executor.Free(outer).Success.Should().BeTrue();
            inner.IsReleased.Should().BeTrue();
            outer.IsReleased.Should().BeTrue();
        });
    }

    [Fact]
    public void FallbackShouldResolveDependentManifestsAndFileElements()
    {
        TestWindow.OnStaThread(() =>
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "AdaskoComFallback_" + Guid.NewGuid().ToString("N"));
            try
            {
                var parentManifest = CreateNestedManifestFixture(tempDir);
                var api = new SimulatedNanoServerApi();
                var dummyAssemblyInTempDir = Path.Combine(tempDir, "NativeCOM.dll");
                var descriptors = new[] { new ComPathDescriptor(dummyAssemblyInTempDir, parentManifest) };

                string? value = null;
                var result = Executor.Execute(
                    descriptors,
                    () =>
                    {
                        var obj = new NativeCOM.StringConcatenatorClass();
                        value = obj.ConcatStrings("Nested ", "Manifest");
                    },
                    pumpPendingMessages: false,
                    api);

                result.Success.Should().BeTrue();
                value.Should().Be("Nested Manifest");
            }
            finally
            {
                if (Directory.Exists(tempDir))
                {
                    Directory.Delete(tempDir, recursive: true);
                }
            }
        });
    }

    [Fact]
    public void FallbackShouldRetryWithIUnknownAndAutoInitializeMtaWhenNeeded()
    {
        TestWindow.OnStaThread(() =>
        {
            var api = new SimulatedNanoServerApi
            {
                ReturnNoInterfaceOnFirstGetClassObject = true,
                ReturnNotInitializedOnFirstRegister = true,
            };
            var descriptors = new[] { new ComPathDescriptor(AssemblyPath, ManifestPath) };

            string? output = null;
            var result = Executor.Execute(
                descriptors,
                () =>
                {
                    var obj = new NativeCOM.StringConcatenatorClass();
                    output = obj.ConcatStrings("MTA ", "Retry");
                },
                pumpPendingMessages: false,
                api);

            result.Success.Should().BeTrue();
            output.Should().Be("MTA Retry");
            api.MtaInitializedCount.Should().Be(1);
            api.GetClassObjectCallCount.Should().Be(2);
        });
    }

    [Fact]
    public void FallbackShouldUnwindFirstRegistrationWhenSecondRegistrationFails()
    {
        TestWindow.OnStaThread(() =>
        {
            var tempManifest = Path.Combine(Path.GetTempPath(), "TwoClasses_" + Guid.NewGuid().ToString("N") + ".manifest");
            try
            {
                File.WriteAllText(
                    tempManifest,
                    $"""
                    <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                    <assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
                      <file name="NativeCOM.dll">
                        <comClass clsid="{ConcatenatorClsid}" />
                        <comClass clsid="{SecondClsid}" />
                      </file>
                    </assembly>
                    """);

                var api = new SimulatedNanoServerApi
                {
                    FailRegisterCallIndex = 2,
                    FakeSecondClsidFactory = true,
                };
                var descriptors = new[] { new ComPathDescriptor(AssemblyPath, tempManifest) };

                var result = Executor.Execute(descriptors, () => { }, pumpPendingMessages: false, api);

                result.Success.Should().BeFalse();
                result.Exception.Should().BeOfType<Win32Exception>();
                api.RegisteredTokens.Should().ContainSingle();
                api.RevokedTokens.Should().Equal(api.RegisteredTokens);
                api.ReleasedFactoriesCount.Should().Be(2);
            }
            finally
            {
                if (File.Exists(tempManifest))
                {
                    File.Delete(tempManifest);
                }
            }
        });
    }

    [Fact]
    public void FallbackShouldRetainFailedRevocationForRetryAndRejectWrongThreadDeactivation()
    {
        TestWindow.OnStaThread(() =>
        {
            var api = new SimulatedNanoServerApi
            {
                FailRevokeCallIndex = 1,
            };
            var descriptors = new[] { new ComPathDescriptor(AssemblyPath, ManifestPath) };

            var creation = Executor.Create(
                descriptors,
                () => new NativeCOM.StringConcatenatorClass(),
                pumpPendingMessages: false,
                api);
            creation.Success.Should().BeTrue();
            using var handle = creation.Value!;
            var cookie = handle.ActivationCookies[0];

            var wrongThreadDeactivate = true;
            var thread = new Thread(() => wrongThreadDeactivate = api.DeactivateActCtx(cookie));
            thread.Start();
            thread.Join();
            wrongThreadDeactivate.Should().BeFalse();

            var firstFree = Executor.Free(handle);
            firstFree.Success.Should().BeFalse();
            firstFree.Exception.Should().BeOfType<Win32Exception>();
            handle.IsReleased.Should().BeFalse();

            var secondFree = Executor.Free(handle);
            secondFree.Success.Should().BeTrue();
            handle.IsReleased.Should().BeTrue();
        });
    }

    [Theory]
    [InlineData("NonFallbackError")]
    [InlineData("MissingManifest")]
    [InlineData("MalformedXml")]
    [InlineData("NonAssemblyRoot")]
    [InlineData("EmptyXml")]
    [InlineData("InvalidClsid")]
    [InlineData("MissingDllPath")]
    [InlineData("MissingDependencyName")]
    [InlineData("MissingDependencyFile")]
    [InlineData("LoadLibraryFailure")]
    [InlineData("GetProcAddressFailure")]
    [InlineData("GetClassObjectFailure")]
    public void FallbackShouldReportExpectedWin32ErrorsForInvalidInputsAndNativeFailures(string scenario)
    {
        TestWindow.OnStaThread(() =>
        {
            var tempManifest = Path.Combine(Path.GetTempPath(), $"Scenario_{scenario}_{Guid.NewGuid():N}.manifest");
            try
            {
                var api = ConfigureScenario(scenario, tempManifest, out var context);
                var defaultAssembly = string.Equals(scenario, "MissingDllPath", StringComparison.Ordinal)
                    ? null
                    : AssemblyPath;
                var handle = api.CreateActCtx(ref context, defaultAssembly, out var createError);

                if (handle == (IntPtr)(-1))
                {
                    createError.Should().NotBe(0);
                    return;
                }

                var activated = api.ActivateActCtx(handle, out _);
                activated.Should().BeFalse();
                api.ConsumeLastError().Should().NotBe(0);
                api.ReleaseActCtx(handle);
            }
            finally
            {
                if (File.Exists(tempManifest))
                {
                    File.Delete(tempManifest);
                }
            }
        });
    }

    [Fact]
    public void ExecuteAndCreateShouldFailWhenSxsFallbackIsDisabled()
    {
        TestWindow.OnStaThread(() =>
        {
            var api = new SimulatedNanoServerApi(NativeMethods.ERROR_INVALID_FUNCTION);
            var descriptors = new[] { new ComPathDescriptor(AssemblyPath, ManifestPath) };
            var prevSxs = Environment.GetEnvironmentVariable(NanoServerOptions.DisableSxsFallbackEnvironmentVariable);
            var prevSta = Environment.GetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable);

            try
            {
                NanoServerOptions.EnableSxsFallback = false;
                NanoServerOptions.EnableSxsFallback.Should().BeFalse();

                var executeResult = Executor.Execute(
                    descriptors,
                    () => _ = new NativeCOM.StringConcatenatorClass(),
                    pumpPendingMessages: false,
                    api);
                executeResult.Success.Should().BeFalse();
                executeResult.Exception.Should().BeOfType<Win32Exception>()
                    .Which.NativeErrorCode.Should().Be(NativeMethods.ERROR_INVALID_FUNCTION);

                var createResult = Executor.Create(
                    descriptors,
                    () => new NativeCOM.StringConcatenatorClass(),
                    pumpPendingMessages: false,
                    api);
                createResult.Success.Should().BeFalse();
                createResult.Exception.Should().BeOfType<Win32Exception>()
                    .Which.NativeErrorCode.Should().Be(NativeMethods.ERROR_INVALID_FUNCTION);

                Environment.SetEnvironmentVariable(NanoServerOptions.DisableSxsFallbackEnvironmentVariable, "true");
                Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, "1");
                NanoServerOptions.ResetToEnvironmentDefaults();
                NanoServerOptions.EnableSxsFallback.Should().BeFalse();
                NanoServerOptions.EnableStaShim.Should().BeFalse();
            }
            finally
            {
                Environment.SetEnvironmentVariable(NanoServerOptions.DisableSxsFallbackEnvironmentVariable, prevSxs);
                Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, prevSta);
                NanoServerOptions.ResetToEnvironmentDefaults();
            }
        });
    }

    [Fact]
    public void ExecuteOnStaThreadWithMessagePumpShouldInvokeSampleComObjectAndPumpWindowMessages()
    {
        TestWindow.OnStaThread(() =>
        {
            Thread.CurrentThread.GetApartmentState().Should().Be(ApartmentState.STA);
            using var window = new TestWindow();
            window.Post(wParam: 99, lParam: 1234);

            var api = new SimulatedNanoServerApi(NativeMethods.ERROR_INVALID_FUNCTION);
            var descriptors = new[] { new ComPathDescriptor(AssemblyPath, ManifestPath) };
            string? output = null;

            var result = Executor.Execute(
                descriptors,
                () =>
                {
                    var instance = new NativeCOM.StringConcatenatorClass();
                    output = instance.ConcatStrings("STA, ", "SampleCOM!");
                },
                pumpPendingMessages: true,
                api);

            result.Success.Should().BeTrue();
            output.Should().Be("STA, SampleCOM!");
            window.DeliveryCount.Should().Be(1);
            window.LastWParam.Should().Be(new UIntPtr(99));
            window.LastLParam.Should().Be(new IntPtr(1234));
        });
    }

    [Fact]
    public void NanoServerOptionsShouldManageStaShimStateAndIdentifyShimmedApiSets()
    {
        var prevLibSta = Environment.GetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable);
        var prevOsSta = Environment.GetEnvironmentVariable(NanoServerOptions.DisableOsStaShimEnvironmentVariable);
        try
        {
            NanoServerStaShim.IsShimmedApiSet(null).Should().BeFalse();
            NanoServerStaShim.IsShimmedApiSet(string.Empty).Should().BeFalse();
            NanoServerStaShim.IsShimmedApiSet("api-ms-win-core-synch-l1-1-0").Should().BeFalse();
            NanoServerStaShim.IsShimmedApiSet("ext-ms-win-rtcore-ntuser-window-ext-l1-1-0").Should().BeTrue();
            NanoServerStaShim.IsShimmedApiSet("ext-ms-win-ntuser-message-l1-1-0").Should().BeTrue();
            NanoServerStaShim.IsShimmedApiSet("ext-ms-win-com-sta-l1-1-0").Should().BeTrue();

            Environment.SetEnvironmentVariable(NanoServerOptions.DisableOsStaShimEnvironmentVariable, "1");
            NanoServerOptions.EnableStaShim = false;
            NanoServerOptions.RefreshStaState();
            NanoServerOptions.IsInProcessStaShimActive.Should().BeFalse();

            Environment.SetEnvironmentVariable(NanoServerOptions.DisableOsStaShimEnvironmentVariable, value: null);
            NanoServerOptions.EnableStaShim = false;
            NanoServerOptions.RefreshStaState();
            NanoServerOptions.IsInProcessStaShimActive.Should().BeFalse();

            NanoServerOptions.ForceInProcessStaShim = true;
            NanoServerOptions.ForceInProcessStaShim.Should().BeTrue();
            NanoServerOptions.EnableStaShim = true;
            NanoServerOptions.EnableStaShim.Should().BeTrue();
            NanoServerOptions.RefreshStaState();
            NanoServerOptions.IsInProcessStaShimActive.Should().BeTrue();

            NanoServerWindowManager.ResolveFunction("RegisterClassW").Should().NotBe(IntPtr.Zero);
            NanoServerWindowManager.ResolveFunction("CreateWindowExW").Should().NotBe(IntPtr.Zero);
            NanoServerWindowManager.ResolveFunction("PeekMessageW").Should().NotBe(IntPtr.Zero);
            NanoServerWindowManager.ResolveFunction("CoInitialize").Should().NotBe(IntPtr.Zero);
            NanoServerWindowManager.ResolveFunction("NonExistentFunction").Should().Be(IntPtr.Zero);
        }
        finally
        {
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableStaShimEnvironmentVariable, prevLibSta);
            Environment.SetEnvironmentVariable(NanoServerOptions.DisableOsStaShimEnvironmentVariable, prevOsSta);
            NanoServerOptions.ResetToEnvironmentDefaults();
        }
    }

    private static string CreateNestedManifestFixture(string tempDir)
    {
        var subFolder = Path.Combine(tempDir, "SubChild");
        var siblingFolder = Path.Combine(tempDir, "Sibling");
        Directory.CreateDirectory(subFolder);
        Directory.CreateDirectory(siblingFolder);

        File.WriteAllText(
            Path.Combine(tempDir, "DirectChild.manifest"),
            "<assembly><dependency><dependentAssembly><assemblyIdentity name=\"SubChild\" /></dependentAssembly></dependency></assembly>");
        File.WriteAllText(
            Path.Combine(subFolder, "SubChild.manifest"),
            $"<assembly><file name=\"{AssemblyPath}\"><comClass clsid=\"{ConcatenatorClsid}\" /></file></assembly>");
        File.WriteAllText(
            Path.Combine(siblingFolder, "SiblingChild.manifest"),
            "<assembly><file name=\"EmptyElement.dll\" /><dependency><dependentAssembly /></dependency></assembly>");

        var parentManifest = Path.Combine(siblingFolder, "Parent.manifest");
        const string parentXml =
            "<assembly>" +
            "<dependency><dependentAssembly><assemblyIdentity name=\"DirectChild\" /></dependentAssembly></dependency>" +
            "<dependency><dependentAssembly><assemblyIdentity name=\"DirectChild\" /></dependentAssembly></dependency>" +
            "<dependency><dependentAssembly><assemblyIdentity name=\"SiblingChild\" /></dependentAssembly></dependency>" +
            "</assembly>";
        File.WriteAllText(parentManifest, parentXml);
        return parentManifest;
    }

    private static SimulatedNanoServerApi ConfigureScenario(
        string scenario,
        string tempManifest,
        out ActCtx context)
    {
        var sxsError = string.Equals(scenario, "NonFallbackError", StringComparison.Ordinal)
            ? 5
            : NativeMethods.ERROR_INVALID_FUNCTION;
        var api = new SimulatedNanoServerApi(sxsError);
        var manifestToUse = ResolveScenarioManifest(scenario, tempManifest, api);

        context = new ActCtx
        {
            cbSize = Marshal.SizeOf<ActCtx>(),
            dwFlags = NativeMethods.ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID,
            lpSource = manifestToUse,
            lpAssemblyDirectory = Path.GetDirectoryName(AssemblyPath) ?? string.Empty,
        };
        return api;
    }

    private static string ResolveScenarioManifest(string scenario, string tempManifest, SimulatedNanoServerApi api)
    {
        switch (scenario)
        {
            case "MissingManifest":
                return tempManifest + ".doesnotexist";
            case "MalformedXml":
                File.WriteAllText(tempManifest, "<assembly><unclosed>");
                return tempManifest;
            case "NonAssemblyRoot":
                File.WriteAllText(tempManifest, "<?xml version=\"1.0\"?><wrongRoot />");
                return tempManifest;
            case "EmptyXml":
                File.WriteAllText(tempManifest, "<?xml version=\"1.0\"?><!-- comment only -->");
                return tempManifest;
            case "InvalidClsid":
                File.WriteAllText(tempManifest, "<assembly><comClass clsid=\"not-a-guid\" /></assembly>");
                return tempManifest;
            case "MissingDllPath":
                File.WriteAllText(tempManifest, $"<assembly><comClass clsid=\"{ConcatenatorClsid}\" /></assembly>");
                return tempManifest;
            case "MissingDependencyName":
                File.WriteAllText(tempManifest, "<assembly><dependency><dependentAssembly><assemblyIdentity name=\"\" /></dependentAssembly></dependency></assembly>");
                return tempManifest;
            case "MissingDependencyFile":
                File.WriteAllText(tempManifest, "<assembly><dependency><dependentAssembly><assemblyIdentity name=\"DoesNotExist\" /></dependentAssembly></dependency></assembly>");
                return tempManifest;
            case "LoadLibraryFailure":
                api.FailLoadLibrary = true;
                return ManifestPath;
            case "GetProcAddressFailure":
                api.FailGetProcAddress = true;
                return ManifestPath;
            case "GetClassObjectFailure":
                api.FailGetClassObject = true;
                return ManifestPath;
            default:
                return ManifestPath;
        }
    }

    private sealed class SimulatedNanoServerApi : ActivationContextApi
    {
        private readonly int _sxsErrorCode;
        private int _registerCallCount;
        private int _revokeCallCount;

        internal SimulatedNanoServerApi(int sxsErrorCode = NativeMethods.ERROR_INVALID_FUNCTION)
        {
            _sxsErrorCode = sxsErrorCode;
        }

        internal bool ReturnNoInterfaceOnFirstGetClassObject { get; set; }

        internal bool ReturnNotInitializedOnFirstRegister { get; set; }

        internal bool FakeSecondClsidFactory { get; set; }

        internal bool FailLoadLibrary { get; set; }

        internal bool FailGetProcAddress { get; set; }

        internal bool FailGetClassObject { get; set; }

        internal int FailRegisterCallIndex { get; set; }

        internal int FailRevokeCallIndex { get; set; }

        internal int GetClassObjectCallCount { get; private set; }

        internal int MtaInitializedCount { get; private set; }

        internal int ReleasedFactoriesCount { get; private set; }

        internal List<uint> RegisteredTokens { get; } = [];

        internal List<uint> RevokedTokens { get; } = [];

        internal override IntPtr CreateActCtx(ref ActCtx context) => new(-1);

        internal override int GetLastWin32Error() => _sxsErrorCode;

        internal override IntPtr LoadLibrary(string dllPath)
            => FailLoadLibrary ? IntPtr.Zero : base.LoadLibrary(dllPath);

        internal override IntPtr GetProcAddress(IntPtr moduleHandle, string procName)
            => FailGetProcAddress ? IntPtr.Zero : base.GetProcAddress(moduleHandle, procName);

        internal override int GetClassObject(IntPtr procAddress, ref Guid clsid, ref Guid iid, out IntPtr classFactory)
        {
            GetClassObjectCallCount++;
            if (FailGetClassObject)
            {
                classFactory = IntPtr.Zero;
                return NativeMethods.CLASS_E_CLASSNOTAVAILABLE;
            }

            if (ReturnNoInterfaceOnFirstGetClassObject && GetClassObjectCallCount == 1)
            {
                classFactory = IntPtr.Zero;
                return NativeMethods.E_NOINTERFACE;
            }

#pragma warning disable MA0176 // Optimize guid creation
            if (FakeSecondClsidFactory && clsid == Guid.Parse(SecondClsid))
            {
                var primaryClsid = Guid.Parse(ConcatenatorClsid);
                return base.GetClassObject(procAddress, ref primaryClsid, ref iid, out classFactory);
            }
#pragma warning restore MA0176

            return base.GetClassObject(procAddress, ref clsid, ref iid, out classFactory);
        }

        internal override int RegisterClassObject(ref Guid clsid, IntPtr classFactory, out uint registrationToken)
        {
            _registerCallCount++;
            if (ReturnNotInitializedOnFirstRegister && _registerCallCount == 1)
            {
                registrationToken = 0;
                return NativeMethods.CO_E_NOTINITIALIZED;
            }

            if (FailRegisterCallIndex > 0 && _registerCallCount == FailRegisterCallIndex)
            {
                registrationToken = 0;
                return unchecked((int)0x8000FFFF);
            }

            var hr = base.RegisterClassObject(ref clsid, classFactory, out registrationToken);
            if (hr >= 0)
            {
                RegisteredTokens.Add(registrationToken);
            }

            return hr;
        }

        internal override int InitializeMultithreadedApartment()
        {
            MtaInitializedCount++;
            return 0;
        }

        internal override int RevokeClassObject(uint registrationToken)
        {
            _revokeCallCount++;
            if (FailRevokeCallIndex > 0 && _revokeCallCount == FailRevokeCallIndex)
            {
                return unchecked((int)0x8000FFFF);
            }

            var hr = base.RevokeClassObject(registrationToken);
            if (hr >= 0)
            {
                RevokedTokens.Add(registrationToken);
            }

            return hr;
        }

        internal override void ReleaseClassObject(IntPtr classFactory)
        {
            ReleasedFactoriesCount++;
            base.ReleaseClassObject(classFactory);
        }
    }
}
#pragma warning restore SCS0018, SEC0116
#pragma warning restore IDISP016
#pragma warning restore IDISP007
#pragma warning restore SA1121 // Use built-in type alias
