using System.Diagnostics.Tracing;
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

public class ComInteropEventSourceTest
{
    [Fact]
    public void ShouldHaveExpectedProviderName()
    {
        ComInteropEventSource.Log.Name.Should().Be(ComInteropEventSource.ProviderName);
        ComInteropEventSource.ProviderName.Should().Be("AdaskoTheBeAsT.Interop.COM");
    }

    [Fact]
    public void HandleLeakedShouldEmitWarningEventWhenListenerIsAttached()
    {
        // Use a unique sentinel type-name per test: ComInteropEventSource.Log is a process-wide
        // singleton, so finalizers from other tests (e.g. ComObjectHandleDisposeTest) can also
        // emit HandleLeaked events on the same listener at unpredictable times. Filtering by
        // payload keeps the assertion stable regardless of cross-test GC timing.
        const string sentinel = "Sentinel_HandleLeakedShouldEmit";
        using var listener = new CapturingEventListener(ComInteropEventSource.Log);

        ComInteropEventSource.Log.HandleLeaked(sentinel);

        var matches = Array.FindAll(
            listener.Events,
            e => e.EventId == ComInteropEventSource.HandleLeakedEventId
                && e.Payload is { Count: > 0 }
                && string.Equals(e.Payload[0] as string, sentinel, System.StringComparison.Ordinal));
        matches.Should().ContainSingle();
        var evt = matches[0];
        evt.Level.Should().Be(EventLevel.Warning);
    }

    [Fact]
    public void HandleLeakedShouldBeSilentWhenNoListenerIsAttached()
    {
        var act = () => ComInteropEventSource.Log.HandleLeaked("IBar");

        act.Should().NotThrow();
    }

    [Fact]
    public void HandleReleaseFailedShouldEmitErrorEventWhenListenerIsAttached()
    {
        const string sentinel = "Sentinel_HandleReleaseFailedShouldEmit";
        using var listener = new CapturingEventListener(ComInteropEventSource.Log);

        ComInteropEventSource.Log.HandleReleaseFailed(sentinel, "native release failed");

        var matches = Array.FindAll(
            listener.Events,
            e => e.EventId == ComInteropEventSource.HandleReleaseFailedEventId
                && e.Payload is { Count: > 1 }
                && string.Equals(e.Payload[0] as string, sentinel, System.StringComparison.Ordinal));
        matches.Should().ContainSingle();
        var evt = matches[0];
        evt.Level.Should().Be(EventLevel.Error);
        var errorMessage = evt.Payload![1] as string;
        errorMessage.Should().Be("native release failed");
    }

    [Fact]
    public void HandleReleaseFailedShouldBeSilentWhenNoListenerIsAttached()
    {
        var act = () => ComInteropEventSource.Log.HandleReleaseFailed("IQux", "err");

        act.Should().NotThrow();
    }

    [Fact]
    public void CaptureShouldRetainAllEventsFromConcurrentWriters()
    {
        const string sentinel = "Sentinel_ConcurrentWriters";
        const int count = 256;
        using var listener = new CapturingEventListener(ComInteropEventSource.Log);

        Parallel.For(0, count, _ => ComInteropEventSource.Log.HandleLeaked(sentinel));

        var matches = Array.FindAll(
            listener.Events,
            e => e.EventId == ComInteropEventSource.HandleLeakedEventId
                && e.Payload is { Count: > 0 }
                && string.Equals(e.Payload[0] as string, sentinel, StringComparison.Ordinal));
        matches.Should().HaveCount(count);
    }
}
