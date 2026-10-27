using System.ComponentModel;
using System.Runtime.InteropServices;
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

#pragma warning disable SA1121 // Use built-in type alias
#pragma warning disable IDISP007 // Tests own factory-created handles.
#pragma warning disable IDISP016 // Tests inspect partial cleanup and retry state.
public class ActivationContextFailureTest
{
    private static string AssemblyPath => TestPaths.AssemblyPath;

    private static string ManifestPath => TestPaths.ManifestPath;

    [Theory]
    [InlineData(false, @"C:\", false)]
    [InlineData(true, @"C:\", false)]
    [InlineData(false, @"\\server\share", false)]
    [InlineData(true, @"\\server\share", false)]
    [InlineData(false, @"C:\", true)]
    [InlineData(true, @"C:\", true)]
    [InlineData(false, @"\\server\share", true)]
    [InlineData(true, @"\\server\share", true)]
    public void OperationsShouldRejectRootAssemblyPathAndCleanUpPreparedContexts(bool create, string assemblyPath, bool prepareFirstContext)
    {
        TestWindow.OnStaThread(() =>
        {
            using var api = new FailingActivationContextApi();
            var descriptors = new List<ComPathDescriptor>();
            if (prepareFirstContext)
            {
                descriptors.Add(new ComPathDescriptor(AssemblyPath, ManifestPath));
            }

            descriptors.Add(new ComPathDescriptor(assemblyPath, ManifestPath));
            var invoked = false;
            Result result;
            if (create)
            {
                var creation = Executor.Create(
                    descriptors,
                    () =>
                    {
                        invoked = true;
                        return new object();
                    },
                    pumpPendingMessages: false,
                    api);
                creation.Value.Should().BeNull();
                result = creation;
            }
            else
            {
                result = Executor.Execute(descriptors, () => invoked = true, pumpPendingMessages: false, api);
            }

            result.Success.Should().BeFalse();
            var exception = result.Exception.Should().BeOfType<ArgumentException>().Which;
            exception.ParamName.Should().Be("comPathDescriptor");
            invoked.Should().BeFalse();
            api.CreationCallCount.Should().Be(prepareFirstContext ? 1 : 0);
            api.ActivationCallCount.Should().Be(0);
            api.OutstandingReferenceCount.Should().Be(0);
            api.ActiveCookies.Should().BeEmpty();
        });
    }

    [Theory]
    [InlineData(false, false)]
    [InlineData(false, true)]
    [InlineData(true, false)]
    [InlineData(true, true)]
    public void OperationsShouldCleanUpPartialSetup(bool create, bool failActivation)
    {
        TestWindow.OnStaThread(() =>
        {
            using var api = new FailingActivationContextApi();
            var outerCreation = Executor.Create(
                [new ComPathDescriptor(AssemblyPath, ManifestPath)],
                () => new NativeCOM.StringConcatenatorClass(),
                pumpPendingMessages: false,
                api);
            outerCreation.Success.Should().BeTrue();
            using var outer = outerCreation.Value!;
            if (failActivation)
            {
                api.FailActivationCall = api.ActivationCallCount + 2;
            }
            else
            {
                api.FailCreationCall = api.CreationCallCount + 2;
            }

            var invoked = false;
            var result = RunOperation(create, () => invoked = true, api);

            result.Success.Should().BeFalse();
            result.Exception.Should().BeOfType<Win32Exception>();
            invoked.Should().BeFalse();
            api.OutstandingReferenceCount.Should().Be(1);
            api.ActiveCookies.Should().Equal(outer.ActivationCookies);
            api.SuccessfulDeactivations.Should().HaveCount(failActivation ? 1 : 0);
            outer.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");
            Executor.Free(outer).Success.Should().BeTrue();
            api.OutstandingReferenceCount.Should().Be(0);
            api.ActiveCookies.Should().BeEmpty();
        });
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ReleaseShouldRetainFailedDeactivationForRetry(bool dispose)
    {
        TestWindow.OnStaThread(() =>
        {
            using var api = new FailingActivationContextApi { FailDeactivationCall = 2 };
            var creation = Executor.Create(
                Descriptors(),
                () => new NativeCOM.StringConcatenatorClass(),
                pumpPendingMessages: false,
                api);
            creation.Success.Should().BeTrue();
            using var handle = creation.Value!;
            var cookies = handle.ActivationCookies.ToArray();
            var alias = handle.ComObject!;

            if (dispose)
            {
                handle.Dispose();
            }
            else
            {
                var release = Executor.Free(handle);
                release.Success.Should().BeFalse();
                release.Exception.Should().BeOfType<Win32Exception>();
            }

            handle.IsReleased.Should().BeFalse();
            handle.IsReleasing.Should().BeFalse();
            handle.ComObject.Should().BeNull();
            Action useAlias = () => alias.ConcatStrings("released", " RCW");
            useAlias.Should().Throw<InvalidComObjectException>();
            handle.ActivationCookies.Should().Equal(cookies[0]);
            handle.ActivationContextHandles.Should().HaveCount(2);
            api.ActiveCookies.Should().Equal(cookies[0]);
            api.SuccessfulDeactivations.Should().Equal(cookies[1]);
            api.OutstandingReferenceCount.Should().Be(2);

            Executor.Free(handle).Success.Should().BeTrue();

            handle.IsReleased.Should().BeTrue();
            handle.ActivationCookies.Should().BeEmpty();
            handle.ActivationContextHandles.Should().BeEmpty();
            api.ActiveCookies.Should().BeEmpty();
            api.OutstandingReferenceCount.Should().Be(0);
            api.DeactivationAttempts.Should().Equal(cookies[1], cookies[0], cookies[0]);
            Executor.Free(handle).Success.Should().BeTrue();
            api.DeactivationAttempts.Should().HaveCount(3);
        });
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void OperationsShouldAggregateCallbackAndCleanupFailures(bool create)
    {
        TestWindow.OnStaThread(() =>
        {
            using var api = new FailingActivationContextApi { FailDeactivationCall = 2 };
            var failure = new InvalidOperationException("Operation failed before cleanup.");

            var result = RunOperation(create, () => throw failure, api);

            result.Success.Should().BeFalse();
            var aggregate = result.Exception.Should().BeOfType<AggregateException>().Which;
            aggregate.InnerExceptions.Should().HaveCount(2);
            aggregate.InnerExceptions[0].Should().BeSameAs(failure);
            aggregate.InnerExceptions[1].Should().BeOfType<Win32Exception>();
            api.ActiveCookies.Should().ContainSingle();
            api.SuccessfulDeactivations.Should().ContainSingle();
            api.OutstandingReferenceCount.Should().Be(2);

            // Execute/failed Create expose no retry handle. The fixture restores native state
            // before this isolated STA exits, discarding its remaining managed cookie tracking.
        });
    }

    [Fact]
    public void ExecuteShouldReportCleanupFailureAfterSuccessfulAction()
    {
        TestWindow.OnStaThread(() =>
        {
            using var api = new FailingActivationContextApi { FailDeactivationCall = 2 };
            var invoked = false;

            var result = RunOperation(create: false, () => invoked = true, api);

            invoked.Should().BeTrue();
            result.Success.Should().BeFalse();
            result.Exception.Should().BeOfType<Win32Exception>();
            api.ActiveCookies.Should().ContainSingle();
            api.OutstandingReferenceCount.Should().Be(2);
        });
    }

    [Fact]
    public void FreeShouldRejectWhenGetCurrentActCtxFailsAndAllowRetry()
    {
        TestWindow.OnStaThread(() =>
        {
            using var api = new FailingActivationContextApi();
            var creation = Executor.Create(
                [new ComPathDescriptor(AssemblyPath, ManifestPath)],
                () => new NativeCOM.StringConcatenatorClass(),
                pumpPendingMessages: false,
                api);
            creation.Success.Should().BeTrue();
            using var handle = creation.Value!;
            api.FailGetCurrentActCtx = true;

            var failedRelease = Executor.Free(handle);

            failedRelease.Success.Should().BeFalse();
            failedRelease.Exception.Should().BeOfType<Win32Exception>();
            handle.IsReleased.Should().BeFalse();
            handle.ComObject!.ConcatStrings("still", " alive").Should().Be("still alive");

            api.FailGetCurrentActCtx = false;
            Executor.Free(handle).Success.Should().BeTrue();
            handle.IsReleased.Should().BeTrue();
            api.OutstandingReferenceCount.Should().Be(0);
            api.ActiveCookies.Should().BeEmpty();
        });
    }

    private static ComPathDescriptor[] Descriptors()
        => [new(AssemblyPath, ManifestPath), new(AssemblyPath, ManifestPath)];

    private static Result RunOperation(bool create, Action action, ActivationContextApi api)
    {
        if (!create)
        {
            return Executor.Execute(Descriptors(), action, pumpPendingMessages: false, api);
        }

        var creation = Executor.Create(
            Descriptors(),
            () =>
            {
                action?.Invoke();
                return new object();
            },
            pumpPendingMessages: false,
            api);
        creation.Value.Should().BeNull();
        return creation;
    }

    // Successful calls use the real Windows API. Only selected calls fail before touching native
    // state; reference/cookie accounting also verifies cleanup and repairs deliberate failures.
#pragma warning disable CA2216 // Fixture cleanup is thread-affine and runs on its owning STA.
    private sealed class FailingActivationContextApi : ActivationContextApi, IDisposable
    {
        private readonly Dictionary<IntPtr, int> _references = [];

        internal int FailCreationCall { get; set; }

        internal int FailActivationCall { get; set; }

        internal int FailDeactivationCall { get; set; }

        internal bool FailGetCurrentActCtx { get; set; }

        internal int CreationCallCount { get; private set; }

        internal int ActivationCallCount { get; private set; }

        internal int OutstandingReferenceCount => _references.Values.Sum();

        internal List<IntPtr> ActiveCookies { get; } = [];

        internal List<IntPtr> DeactivationAttempts { get; } = [];

        internal List<IntPtr> SuccessfulDeactivations { get; } = [];

        public void Dispose()
        {
            // Bypass injected failures so even a failing assertion cannot leak native state.
            while (ActiveCookies.Count > 0)
            {
                var index = ActiveCookies.Count - 1;
                base.DeactivateActCtx(ActiveCookies[index]).Should().BeTrue("the fixture must restore the native activation stack");

                ActiveCookies.RemoveAt(index);
            }

            foreach (var pair in _references)
            {
                for (var count = 0; count < pair.Value; count++)
                {
                    base.ReleaseActCtx(pair.Key);
                }
            }

            _references.Clear();
        }

        internal override IntPtr CreateActCtx(ref ActCtx context, string? defaultAssemblyPath, out int errorCode)
        {
            CreationCallCount++;
            if (CreationCallCount == FailCreationCall)
            {
                errorCode = NativeMethods.ERROR_INVALID_PARAMETER;
                return new IntPtr(-1);
            }

            var handle = base.CreateActCtx(ref context, defaultAssemblyPath, out errorCode);
            if (handle != new IntPtr(-1))
            {
                AddReference(handle);
            }

            return handle;
        }

        internal override bool ActivateActCtx(IntPtr handle, out IntPtr cookie)
        {
            ActivationCallCount++;
            cookie = IntPtr.Zero;
            if (ActivationCallCount == FailActivationCall || !base.ActivateActCtx(handle, out cookie))
            {
                return false;
            }

            ActiveCookies.Add(cookie);
            return true;
        }

        internal override bool DeactivateActCtx(IntPtr cookie)
        {
            DeactivationAttempts.Add(cookie);
            if (DeactivationAttempts.Count == FailDeactivationCall || !base.DeactivateActCtx(cookie))
            {
                return false;
            }

            ActiveCookies.RemoveAt(ActiveCookies.Count - 1);
            SuccessfulDeactivations.Add(cookie);
            return true;
        }

        internal override bool GetCurrentActCtx(out IntPtr handle)
        {
            if (FailGetCurrentActCtx)
            {
                handle = IntPtr.Zero;
                return false;
            }

            var success = base.GetCurrentActCtx(out handle);
            if (success && handle != IntPtr.Zero)
            {
                AddReference(handle);
            }

            return success;
        }

        internal override void ReleaseActCtx(IntPtr handle)
        {
            var references = _references[handle];
            if (references == 1)
            {
                _references.Remove(handle);
            }
            else
            {
                _references[handle] = references - 1;
            }

            base.ReleaseActCtx(handle);
        }

        private void AddReference(IntPtr handle)
        {
            _references.TryGetValue(handle, out var count);
            _references[handle] = count + 1;
        }
    }
#pragma warning restore CA2216
}
#pragma warning restore IDISP016
#pragma warning restore IDISP007
#pragma warning restore SA1121 // Use built-in type alias
