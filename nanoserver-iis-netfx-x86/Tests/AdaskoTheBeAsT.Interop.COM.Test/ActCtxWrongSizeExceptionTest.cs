#if !NET8_0_OR_GREATER
using System.Runtime.Serialization;
#endif
using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

public class ActCtxWrongSizeExceptionTest
{
    [Fact]
    public void DefaultConstructorShouldProduceInstance()
    {
        var sut = new ActCtxWrongSizeException();

        sut.Should().BeOfType<ActCtxWrongSizeException>();
        sut.InnerException.Should().BeNull();
    }

    [Fact]
    public void MessageConstructorShouldSetMessage()
    {
        const string message = "wrong size";

        var sut = new ActCtxWrongSizeException(message);

        sut.Message.Should().Be(message);
    }

    [Fact]
    public void InnerExceptionConstructorShouldSetMessageAndInnerException()
    {
        const string message = "wrong size";
        var inner = new InvalidOperationException("inner");

        var sut = new ActCtxWrongSizeException(message, inner);

        sut.Message.Should().Be(message);
        sut.InnerException.Should().BeSameAs(inner);
    }

#if !NET8_0_OR_GREATER
    [Fact]
    public void SerializationConstructorShouldRoundTripMessageAndInnerException()
    {
        const string message = "wrong size";
        var inner = new InvalidOperationException("inner");
        var original = new ActCtxWrongSizeException(message, inner);
        var info = new SerializationInfo(typeof(ActCtxWrongSizeException), new FormatterConverter());
        var context = new StreamingContext(StreamingContextStates.All);
        original.GetObjectData(info, context);

        var sut = new SerializableActCtxWrongSizeException(info, context);

        sut.Message.Should().Be(message);
        sut.InnerException.Should().BeSameAs(inner);
    }

#pragma warning disable CA1032, RCS1194, S3925 // Test helper exists only to invoke the protected serialization constructor.
    [Serializable]
    private sealed class SerializableActCtxWrongSizeException : ActCtxWrongSizeException
    {
        public SerializableActCtxWrongSizeException(SerializationInfo info, StreamingContext context)
            : base(info, context)
        {
        }
    }
#pragma warning restore CA1032, RCS1194, S3925
#endif
}
