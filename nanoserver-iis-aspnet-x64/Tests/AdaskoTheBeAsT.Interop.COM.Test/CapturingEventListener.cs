using System.Collections.Concurrent;
using System.Diagnostics.Tracing;

namespace AdaskoTheBeAsT.Interop.COM.Test;

internal sealed class CapturingEventListener
    : EventListener
{
    private readonly EventSource _target;
    private readonly ConcurrentQueue<EventWrittenEventArgs> _events = new();

    public CapturingEventListener(EventSource target)
    {
        _target = target;
        EnableEvents(target, EventLevel.Verbose);
    }

    public EventWrittenEventArgs[] Events => _events.ToArray();

    protected override void OnEventWritten(EventWrittenEventArgs eventData)
    {
        if (ReferenceEquals(eventData.EventSource, _target))
        {
            _events.Enqueue(eventData);
        }
    }
}
