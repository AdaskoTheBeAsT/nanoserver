using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

public class ExecuteValidationTest
{
    [Fact]
    public void ExecuteShouldThrowWhenAssemblyPathIsNull()
    {
        var act = () => Executor.Execute(null!, "manifest", () => { });

        act.Should().Throw<ArgumentNullException>().WithParameterName("comAssemblyPath");
    }

    [Fact]
    public void ExecuteShouldThrowWhenManifestPathIsNull()
    {
        var act = () => Executor.Execute("assembly", null!, () => { });

        act.Should().Throw<ArgumentNullException>().WithParameterName("manifestPath");
    }

    [Theory]
    [InlineData("")]
    [InlineData("  ")]
    public void ExecuteShouldThrowWhenAssemblyPathIsWhitespace(string value)
    {
        var act = () => Executor.Execute(value, "manifest", () => { });

        act.Should().Throw<ArgumentException>().WithParameterName("comAssemblyPath");
    }

    [Theory]
    [InlineData("")]
    [InlineData("  ")]
    public void ExecuteShouldThrowWhenManifestPathIsWhitespace(string value)
    {
        var act = () => Executor.Execute("assembly", value, () => { });

        act.Should().Throw<ArgumentException>().WithParameterName("manifestPath");
    }

    [Fact]
    public void ExecuteCollectionOverloadShouldThrowWhenDescriptorsIsNull()
    {
        var act = () => Executor.Execute((ICollection<ComPathDescriptor>)null!, () => { });

        act.Should().Throw<ArgumentNullException>().WithParameterName("comPathDescriptors");
    }

    [Fact]
    public void ExecuteCollectionOverloadShouldThrowWhenDescriptorsIsEmpty()
    {
        var act = () => Executor.Execute(Array.Empty<ComPathDescriptor>(), () => { });

        act.Should().Throw<ArgumentException>().WithParameterName("comPathDescriptors");
    }

    [Fact]
    public void CreateCollectionOverloadShouldThrowWhenDescriptorsIsNull()
    {
        var act = () => Executor.Create<object>((ICollection<ComPathDescriptor>)null!, () => new object());

        act.Should().Throw<ArgumentNullException>().WithParameterName("comPathDescriptors");
    }

    [Fact]
    public void CreateCollectionOverloadShouldThrowWhenDescriptorsIsEmpty()
    {
        var act = () => Executor.Create<object>(Array.Empty<ComPathDescriptor>(), () => new object());

        act.Should().Throw<ArgumentException>().WithParameterName("comPathDescriptors");
    }

    [Fact]
    public void ExecuteShouldReturnFailureResultWhenActionThrows()
    {
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var boom = new InvalidOperationException("boom");

        var result = Executor.Execute(
            comAssemblyPath,
            manifestPath,
            () => throw boom);

        result.Success.Should().BeFalse();
        result.Exception.Should().BeSameAs(boom);
    }

    [Fact]
    public void ExecuteCollectionOverloadShouldRunAction()
    {
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var descriptors = new[] { new ComPathDescriptor(comAssemblyPath, manifestPath) };
        string? output = null;

        var result = Executor.Execute(descriptors, () =>
        {
            var obj = new NativeCOM.StringConcatenatorClass();
            output = obj.ConcatStrings("Foo", "Bar");
        });

        result.Success.Should().BeTrue();
        result.Exception.Should().BeNull();
        output.Should().Be("FooBar");
    }

    [Fact]
    public void ExecuteCollectionOverloadShouldReturnFailureResultWhenActionThrows()
    {
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var descriptors = new[] { new ComPathDescriptor(comAssemblyPath, manifestPath) };
        var boom = new InvalidOperationException("boom-collection");

        var result = Executor.Execute(
            descriptors,
            () => throw boom);

        result.Success.Should().BeFalse();
        result.Exception.Should().BeSameAs(boom);
    }

    [Fact]
    public void CreateShouldThrowWhenAssemblyPathIsNull()
    {
        var act = () => Executor.Create<object>(null!, "manifest", () => new object());

        act.Should().Throw<ArgumentNullException>().WithParameterName("comAssemblyPath");
    }

    [Fact]
    public void CreateShouldThrowWhenManifestPathIsNull()
    {
        var act = () => Executor.Create<object>("assembly", null!, () => new object());

        act.Should().Throw<ArgumentNullException>().WithParameterName("manifestPath");
    }

    [Fact]
    public void CreateShouldThrowWhenFactoryIsNull()
    {
        var act = () => Executor.Create<object>("assembly", "manifest", null!);

        act.Should().Throw<ArgumentNullException>().WithParameterName("factory");
    }

    [Theory]
    [InlineData("")]
    [InlineData("  ")]
    public void CreateShouldThrowWhenAssemblyPathIsWhitespace(string value)
    {
        var act = () => Executor.Create<object>(value, "manifest", () => new object());

        act.Should().Throw<ArgumentException>().WithParameterName("comAssemblyPath");
    }

    [Theory]
    [InlineData("")]
    [InlineData("  ")]
    public void CreateShouldThrowWhenManifestPathIsWhitespace(string value)
    {
        var act = () => Executor.Create<object>("assembly", value, () => new object());

        act.Should().Throw<ArgumentException>().WithParameterName("manifestPath");
    }

    [Fact]
    public void CreateCollectionOverloadShouldThrowWhenFactoryIsNull()
    {
        var descriptors = new[] { new ComPathDescriptor("dll", "manifest") };

        var act = () => Executor.Create<object>(descriptors, null!);

        act.Should().Throw<ArgumentNullException>().WithParameterName("factory");
    }

    [Fact]
    public void CreateShouldReturnFailureResultWhenFactoryThrows()
    {
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var boom = new InvalidOperationException("factory-boom");

        var creation = Executor.Create<object>(
            comAssemblyPath,
            manifestPath,
            () => throw boom);

        creation.Success.Should().BeFalse();
        creation.Value.Should().BeNull();
        creation.Exception.Should().BeSameAs(boom);
    }

    [Fact]
    public void CreateShouldReturnFailureResultWhenFactoryReturnsNull()
    {
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();

        var creation = Executor.Create<object>(
            comAssemblyPath,
            manifestPath,
            () => null!);

        creation.Success.Should().BeFalse();
        creation.Value.Should().BeNull();
        creation.Exception.Should().BeOfType<InvalidOperationException>();
    }

    [Fact]
    public void FreeShouldThrowWhenHandleIsNull()
    {
        var act = () => Executor.Free<object>(null!);

        act.Should().Throw<ArgumentNullException>().WithParameterName("comObjectHandle");
    }

    [Fact]
    public void FreeShouldReturnSuccessWhenHandleAlreadyReleased()
    {
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var creation = Executor.Create(
            comAssemblyPath,
            manifestPath,
            () => new NativeCOM.StringConcatenatorClass());

        Executor.Free(creation.Value!).Success.Should().BeTrue();
        var secondRelease = Executor.Free(creation.Value!);

        secondRelease.Success.Should().BeTrue();
    }
}
