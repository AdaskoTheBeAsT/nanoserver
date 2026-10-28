using System.ComponentModel;
using System.Runtime.InteropServices;
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

#pragma warning disable SA1121 // Use built-in type alias
#pragma warning disable IDISP016 // Tests inspect handles after explicit release.
#pragma warning disable IDISP007 // These tests own and explicitly dispose factory-created handles.
public class ExecutorRegressionTest
{
    private static string AssemblyPath => TestPaths.AssemblyPath;

    private static string ManifestPath => TestPaths.ManifestPath;

    [Fact]
    public void ExecuteShouldRejectNullActionBeforeCreatingContext()
    {
        var act = () => Executor.Execute("missing.dll", "missing.manifest", null!);

        act.Should().Throw<ArgumentNullException>().WithParameterName("action");
    }

    [Fact]
    public void ExecuteCollectionShouldRejectNullActionBeforeCreatingContext()
    {
        var descriptors = new[] { new ComPathDescriptor("missing.dll", "missing.manifest") };
        var act = () => Executor.Execute(descriptors, null!);

        act.Should().Throw<ArgumentNullException>().WithParameterName("action");
    }

#pragma warning disable SCS0018, SEC0116 // Paths are uniquely generated test fixtures, not user input.
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void OperationsShouldRejectNullDescriptorBeforeCreatingContexts(bool create)
    {
        var descriptors = new[]
        {
            new ComPathDescriptor("missing.dll", "missing.manifest"),
            null!,
        };
        Action act = () => _ = create
            ? Executor.Create(descriptors, () => new object())
            : Executor.Execute(descriptors, () => { });

        act.Should().Throw<ArgumentException>().WithParameterName("comPathDescriptors");
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void OperationsShouldReturnFailureForMissingManifest(bool create)
    {
        var missing = Path.Combine(AppContext.BaseDirectory, Guid.NewGuid().ToString("N") + ".manifest");
        var invoked = false;
        Result result;
        if (create)
        {
            var creation = Executor.Create(AssemblyPath, missing, () =>
            {
                invoked = true;
                return new object();
            });
            creation.Value.Should().BeNull();
            result = creation;
        }
        else
        {
            result = Executor.Execute(AssemblyPath, missing, () => invoked = true);
        }

        result.Success.Should().BeFalse();
        result.Exception.Should().BeOfType<Win32Exception>();
        invoked.Should().BeFalse();
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void OperationsShouldReturnFailureForMalformedManifest(bool create)
    {
        var path = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N") + ".manifest");
        try
        {
            File.WriteAllText(path, "<not-a-valid-manifest");
            var result = create
                ? Executor.Create(AssemblyPath, path, () => new object())
                : Executor.Execute(AssemblyPath, path, () => { });

            result.Success.Should().BeFalse();
            result.Exception.Should().BeOfType<Win32Exception>();
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void FailedSecondContextShouldLeaveExistingHandleUsable(bool create)
    {
        using var outer = CreateComHandle();
        var descriptors = new[]
        {
            new ComPathDescriptor(AssemblyPath, ManifestPath),
            new ComPathDescriptor(AssemblyPath, ManifestPath + ".missing"),
        };
        var invoked = false;
        Result result;
        if (create)
        {
            var creation = Executor.Create(descriptors, () =>
            {
                invoked = true;
                return new object();
            });
            creation.Value.Should().BeNull();
            result = creation;
        }
        else
        {
            result = Executor.Execute(descriptors, () => invoked = true);
        }

        result.Success.Should().BeFalse();
        result.Exception.Should().BeOfType<Win32Exception>();
        invoked.Should().BeFalse();
        outer.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
        Executor.Free(outer).Success.Should().BeTrue();
    }

    [Fact]
    public void FreeShouldRejectWrongThreadAndAllowRetryOnOwner()
    {
        using var handle = CreateComHandle();
        Result? release = null;
        var thread = new Thread(() => release = Executor.Free(handle));
        thread.Start();
        thread.Join();

        release.Should().NotBeNull();
        release.Success.Should().BeFalse();
        release.Exception.Should().BeOfType<InvalidOperationException>();
        handle.IsReleased.Should().BeFalse();
        handle.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
        Executor.Free(handle).Success.Should().BeTrue();
    }

    [Fact]
    public void DisposeShouldRejectWrongThreadAndAllowRetryOnOwner()
    {
        using var handle = CreateComHandle();
        var thread = new Thread(handle.Dispose);
        thread.Start();
        thread.Join();

        handle.IsReleased.Should().BeFalse();
        handle.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
        handle.Dispose();
        handle.IsReleased.Should().BeTrue();
    }

    [Fact]
    public void FreeShouldRejectReentrantReleaseBeforeTouchingResources()
    {
        using var handle = CreateComHandle();
        Result release;
        handle.IsReleasing = true;
        try
        {
            release = Executor.Free(handle);
        }
        finally
        {
            handle.IsReleasing = false;
        }

        release.Success.Should().BeFalse();
        release.Exception.Should().BeOfType<InvalidOperationException>();
        handle.IsReleased.Should().BeFalse();
        handle.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
        Executor.Free(handle).Success.Should().BeTrue();
    }

    [Fact]
    public void FreeShouldRejectAnUntrackedNativeContextAboveHandle()
    {
        using var handle = CreateComHandle();
        var context = new ActCtx
        {
            cbSize = Marshal.SizeOf<ActCtx>(),
            lpSource = ManifestPath,
        };
        var api = ActivationContextApi.Instance;
        var nativeHandle = api.CreateActCtx(ref context, AssemblyPath, out _);
        nativeHandle.Should().NotBe(new IntPtr(-1));
        try
        {
            api.ActivateActCtx(nativeHandle, out var cookie).Should().BeTrue();
            try
            {
                var release = Executor.Free(handle);

                release.Success.Should().BeFalse();
                release.Exception.Should().BeOfType<InvalidOperationException>();
                handle.IsReleased.Should().BeFalse();
                handle.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
            }
            finally
            {
                api.DeactivateActCtx(cookie).Should().BeTrue();
            }
        }
        finally
        {
            api.ReleaseActCtx(nativeHandle);
        }

        Executor.Free(handle).Success.Should().BeTrue();
    }

    [Fact]
    public void FreeShouldRejectNonLifoOrderAcrossDifferentHandleTypes()
    {
        using var first = CreateComHandle();
        using var second = Executor.Create(AssemblyPath, ManifestPath, () => new object()).Value!;

        var release = Executor.Free(first);

        release.Success.Should().BeFalse();
        release.Exception.Should().BeOfType<InvalidOperationException>();
        first.IsReleased.Should().BeFalse();
        first.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
        Executor.Free(second).Success.Should().BeTrue();
        Executor.Free(first).Success.Should().BeTrue();
    }

    [Fact]
    public void FreeShouldRejectHandleUnderExecuteContext()
    {
        using var handle = CreateComHandle();
        Result? release = null;

        var execution = Executor.Execute(AssemblyPath, ManifestPath, () => release = Executor.Free(handle));

        execution.Success.Should().BeTrue();
        release!.Success.Should().BeFalse();
        release.Exception.Should().BeOfType<InvalidOperationException>();
        handle.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
        Executor.Free(handle).Success.Should().BeTrue();
    }

    [Fact]
    public void DisposeShouldLeaveOutOfOrderHandleAvailableForRetry()
    {
        using var first = CreateComHandle();
        using var second = Executor.Create(AssemblyPath, ManifestPath, () => new object()).Value!;

        first.Dispose();

        first.IsReleased.Should().BeFalse();
        first.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
        second.Dispose();
        first.Dispose();
        first.IsReleased.Should().BeTrue();
    }

    [Fact]
    public void FailedNestedFactoryShouldRestoreOuterActivation()
    {
        using var outer = CreateComHandle();
        var failure = new InvalidOperationException("factory failure");
        var descriptors = new[]
        {
            new ComPathDescriptor(AssemblyPath, ManifestPath),
            new ComPathDescriptor(AssemblyPath, ManifestPath),
        };

        var creation = Executor.Create<object>(descriptors, () => throw failure);

        creation.Success.Should().BeFalse();
        creation.Exception.Should().BeSameAs(failure);
        creation.Value.Should().BeNull();
        Executor.Free(outer).Success.Should().BeTrue();
    }

    [Fact]
    public void FreeShouldReleaseMultipleContextsAndRestoreOuterHandle()
    {
        using var outer = CreateComHandle();
        var descriptors = new[]
        {
            new ComPathDescriptor(AssemblyPath, ManifestPath),
            new ComPathDescriptor(AssemblyPath, ManifestPath),
        };
        var creation = Executor.Create(descriptors, () => new NativeCOM.StringConcatenatorClass());
        creation.Success.Should().BeTrue();
        using var inner = creation.Value!;

        Executor.Free(inner).Success.Should().BeTrue();

        inner.ActivationCookies.Should().BeEmpty();
        inner.ActivationContextHandles.Should().BeEmpty();
        outer.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
        Executor.Free(outer).Success.Should().BeTrue();
    }

    [Theory]
    [InlineData(ApartmentState.STA)]
    [InlineData(ApartmentState.MTA)]
    public void ExecuteShouldPreserveCallingThreadAndApartment(ApartmentState apartment)
    {
        var sameThread = false;
        var observedApartment = ApartmentState.Unknown;
        Result? result = null;
        var thread = new Thread(() =>
        {
            var owner = Thread.CurrentThread;
            result = Executor.Execute(AssemblyPath, ManifestPath, () =>
            {
                sameThread = ReferenceEquals(Thread.CurrentThread, owner);
                observedApartment = Thread.CurrentThread.GetApartmentState();
            });
        });
        thread.SetApartmentState(apartment);
        thread.Start();
        thread.Join();

        result!.Success.Should().BeTrue();
        sameThread.Should().BeTrue();
        observedApartment.Should().Be(apartment);
    }

    [Fact]
    public void ExecuteShouldProbePrivateAssembliesInDllDirectory()
    {
        var root = Path.Combine(Path.GetTempPath(), "COM-probing-" + Guid.NewGuid().ToString("N"));
        var manifests = Path.Combine(root, "manifests");
        var binaries = Path.Combine(root, "binaries");
        try
        {
            Directory.CreateDirectory(manifests);
            Directory.CreateDirectory(binaries);
            var appManifest = Path.Combine(manifests, "Application.manifest");
            var architecture = IntPtr.Size == 4 ? "x86" : "amd64";
            var content = "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
                + "<assemblyIdentity name=\"RegressionHost\" version=\"1.0.0.0\" type=\"win32\"/>"
                + "<dependency><dependentAssembly><assemblyIdentity name=\"ProbeDependency\" version=\"1.0.0.0\""
                + " type=\"win32\" processorArchitecture=\"" + architecture + "\"/>"
                + "</dependentAssembly></dependency></assembly>";
            File.WriteAllText(appManifest, content);
            var dependency = "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
                + "<assemblyIdentity name=\"ProbeDependency\" version=\"1.0.0.0\" type=\"win32\""
                + " processorArchitecture=\"" + architecture + "\"/></assembly>";
            File.WriteAllText(Path.Combine(binaries, "ProbeDependency.manifest"), dependency);
            File.Copy(AssemblyPath, Path.Combine(binaries, "NativeCOM.dll"));

            var result = Executor.Execute(Path.Combine(binaries, "NativeCOM.dll"), appManifest, () => { });

            result.Exception.Should().BeNull();
            result.Success.Should().BeTrue();
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

#pragma warning restore SCS0018, SEC0116

    [Fact]
    public void FreeShouldSucceedWhenHandleHasNoActivationCookies()
    {
        TestWindow.OnStaThread(() =>
        {
            using var handle = new ComObjectHandle<object>(
                new object(),
                [],
                [],
                pumpPendingMessages: false);

            var release = Executor.Free(handle);

            release.Success.Should().BeTrue();
            handle.IsReleased.Should().BeTrue();
        });
    }

    [Fact]
    public void FreeShouldRejectHandleWhenThreadCookieStackIsSmallerThanHandleCookies()
    {
        TestWindow.OnStaThread(() =>
        {
            using var handle = new ComObjectHandle<object>(
                new object(),
                [new IntPtr(1)],
                [new IntPtr(1)],
                pumpPendingMessages: false);

            var release = Executor.Free(handle);

            release.Success.Should().BeFalse();
            release.Exception.Should().BeOfType<InvalidOperationException>();
            handle.IsReleased.Should().BeFalse();
            handle.MarkReleased();
        });
    }

    private static ComObjectHandle<NativeCOM.StringConcatenatorClass> CreateComHandle()
    {
        var creation = Executor.Create(AssemblyPath, ManifestPath, () => new NativeCOM.StringConcatenatorClass());
        creation.Exception.Should().BeNull();
        creation.Success.Should().BeTrue();
        return creation.Value!;
    }
}
#pragma warning restore IDISP016
#pragma warning restore IDISP007
#pragma warning restore SA1121 // Use built-in type alias
