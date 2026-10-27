using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

public class ResultTest
{
    [Fact]
    public void DefaultResultShouldBeUnsuccessful()
    {
        var sut = new Result();

        sut.Success.Should().BeFalse();
        sut.Exception.Should().BeNull();
    }

    [Fact]
    public void ShouldRoundTripExceptionAndSuccess()
    {
        var ex = new InvalidOperationException("boom");

        var sut = new Result { Success = true, Exception = ex };

        sut.Success.Should().BeTrue();
        sut.Exception.Should().BeSameAs(ex);
    }

    [Fact]
    public void ComObjectCreationResultShouldInheritFromResult()
    {
        var sut = new ComObjectCreationResult<object>();

        sut.Should().BeAssignableTo<Result>();
        sut.Success.Should().BeFalse();
        sut.Exception.Should().BeNull();
        sut.Value.Should().BeNull();
    }
}
