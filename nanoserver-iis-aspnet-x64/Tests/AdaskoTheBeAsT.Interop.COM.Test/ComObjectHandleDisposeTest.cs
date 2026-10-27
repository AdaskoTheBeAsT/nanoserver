using System.Runtime.CompilerServices;
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

#pragma warning disable IDISP007 // Don't dispose injected (tests intentionally exercise Dispose)
#pragma warning disable IDISP016 // Don't use disposed instance (tests assert state after Dispose)
public class ComObjectHandleDisposeTest
{
#pragma warning disable IDISP005 // Return type should indicate disposability (constructor throws, no instance produced)
    [Fact]
    public void CtorShouldThrowWhenComObjectIsNull()
    {
        var act = () => new ComObjectHandle<object>(null!, [], []);

        act.Should().Throw<ArgumentNullException>().WithParameterName("comObject");
    }

    [Fact]
    public void CtorShouldThrowWhenActivationContextHandlesIsNull()
    {
        var act = () => new ComObjectHandle<object>(new object(), null!, []);

        act.Should().Throw<ArgumentNullException>().WithParameterName("activationContextHandles");
    }

    [Fact]
    public void CtorShouldThrowWhenActivationCookiesIsNull()
    {
        var act = () => new ComObjectHandle<object>(new object(), [], null!);

        act.Should().Throw<ArgumentNullException>().WithParameterName("activationCookies");
    }
#pragma warning restore IDISP005

#pragma warning disable S1215 // GC.Collect is intentional: exercising the diagnostic finalizer
    [Fact]
    public void FinalizerShouldEmitLeakEventOnEventSourceWhenNotDisposed()
    {
        using var capture = new CapturingEventListener(ComInteropEventSource.Log);

        AllocateAndAbandonHandle();

        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        GC.WaitForPendingFinalizers();

        capture.Events.Should().Contain(
            e => e.EventId == ComInteropEventSource.HandleLeakedEventId
                && e.Payload != null
                && e.Payload.Count > 0
                && string.Equals(e.Payload[0] as string, nameof(ComObjectHandleDisposeTest), StringComparison.Ordinal));
    }
#pragma warning restore S1215

    [Fact]
    public void DisposeShouldReleaseComObjectAndActivationContexts()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var creation = Executor.Create(
            comAssemblyPath,
            manifestPath,
            () => new NativeCOM.StringConcatenatorClass());
        creation.Success.Should().BeTrue();
        var handle = creation.Value!;

        // Act
        handle.Dispose();

        // Assert
        handle.IsReleased.Should().BeTrue();
        handle.ComObject.Should().BeNull();
        handle.ActivationContextHandles.Should().BeEmpty();
        handle.ActivationCookies.Should().BeEmpty();
    }

    [Fact]
    public void DisposeShouldBeIdempotent()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var creation = Executor.Create(
            comAssemblyPath,
            manifestPath,
            () => new NativeCOM.StringConcatenatorClass());
        var handle = creation.Value!;

        // Act
        handle.Dispose();
        var act = handle.Dispose;

        // Assert
        act.Should().NotThrow();
        handle.IsReleased.Should().BeTrue();
    }

    [Fact]
    public void UsingStatementShouldReleaseHandle()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var creation = Executor.Create(
            comAssemblyPath,
            manifestPath,
            () => new NativeCOM.StringConcatenatorClass());
        var handle = creation.Value!;

        // Act
        using (handle)
        {
            handle.ComObject!.ConcatStrings("A", "B").Should().Be("AB");
        }

        // Assert
        handle.IsReleased.Should().BeTrue();
        handle.ComObject.Should().BeNull();
    }

#pragma warning disable IDISP004 // Don't ignore created IDisposable (intentional leak for finalizer test)
#pragma warning disable CA2000 // Dispose objects before losing scope (same reason)
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void AllocateAndAbandonHandle()
    {
        // Bypass Executor.Create — no real native resources are allocated, so the finalizer is safe to run.
        _ = new ComObjectHandle<ComObjectHandleDisposeTest>(
            new ComObjectHandleDisposeTest(),
            [],
            []);
    }
#pragma warning restore CA2000
#pragma warning restore IDISP004
}
#pragma warning restore IDISP016
#pragma warning restore IDISP007
