using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

public class ExecutorTest
{
    [Fact]
    public void AddStringsTest()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        const string str1 = "Hello";
        const string str2 = "World!";
        string? output = null;
        Action action = () =>
        {
            var executor = new NativeCOM.StringConcatenatorClass();
            output = executor.ConcatStrings(str1, str2);
        };

        // Act
        var result = Executor.Execute(comAssemblyPath, manifestPath, action);

        // Assert
        result.Should().NotBeNull();
        result.Success.Should().BeTrue();
        result.Exception.Should().BeNull();
        output.Should().Be("HelloWorld!");
    }

    [Fact]
    public void CreateAndFreeTest()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();

        // Act
        var creation = Executor.Create(
            comAssemblyPath,
            manifestPath,
            () => new NativeCOM.StringConcatenatorClass());

        // Assert
        creation.Should().NotBeNull();
        creation.Success.Should().BeTrue();
        creation.Exception.Should().BeNull();
        creation.Value.Should().NotBeNull();
        creation.Value.ComObject.Should().NotBeNull();
        creation.Value.ComObject.ConcatStrings("Hello", "World!").Should().Be("HelloWorld!");

        var release = Executor.Free(creation.Value);

        release.Should().NotBeNull();
        release.Success.Should().BeTrue();
        release.Exception.Should().BeNull();
        creation.Value.IsReleased.Should().BeTrue();
        creation.Value.ComObject.Should().BeNull();
        creation.Value.ActivationContextHandles.Should().BeEmpty();
        creation.Value.ActivationCookies.Should().BeEmpty();
    }

    [Fact]
    public void CreateWithCollectionOverloadShouldKeepActivationStateUntilFree()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var descriptors = new[]
        {
            new ComPathDescriptor(comAssemblyPath, manifestPath),
        };

        // Act
        var creation = Executor.Create(
            descriptors,
            () => new NativeCOM.StringConcatenatorClass());

        // Assert
        creation.Success.Should().BeTrue();
        creation.Exception.Should().BeNull();
        creation.Value.Should().NotBeNull();
        creation.Value.IsReleased.Should().BeFalse();
        creation.Value.ActivationContextHandles.Should().ContainSingle();
        creation.Value.ActivationCookies.Should().ContainSingle();
        creation.Value.ComObject.Should().NotBeNull();
        creation.Value.ComObject.ConcatStrings("Foo", "Bar").Should().Be("FooBar");

        var release = Executor.Free(creation.Value);

        release.Success.Should().BeTrue();
        creation.Value.IsReleased.Should().BeTrue();
        creation.Value.ActivationContextHandles.Should().BeEmpty();
        creation.Value.ActivationCookies.Should().BeEmpty();
    }

    [Fact]
    public void CreateShouldFailWhenFactoryReturnsNull()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();

        // Act
        var creation = Executor.Create<NativeCOM.StringConcatenatorClass>(
            comAssemblyPath,
            manifestPath,
            () => null!);

        // Assert
        creation.Success.Should().BeFalse();
        creation.Value.Should().BeNull();
        creation.Exception.Should().BeOfType<InvalidOperationException>();
        creation.Exception.Message.Should().Be("The COM factory returned null.");
    }

    [Fact]
    public void FreeShouldBeIdempotent()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();
        var creation = Executor.Create(
            comAssemblyPath,
            manifestPath,
            () => new NativeCOM.StringConcatenatorClass());

        // Act
        var firstRelease = Executor.Free(creation.Value!);
        var secondRelease = Executor.Free(creation.Value!);

        // Assert
        firstRelease.Success.Should().BeTrue();
        firstRelease.Exception.Should().BeNull();
        secondRelease.Success.Should().BeTrue();
        secondRelease.Exception.Should().BeNull();
        creation.Value!.IsReleased.Should().BeTrue();
    }

    [Fact]
    public void CreateShouldThrowWhenFactoryIsNull()
    {
        // Arrange
        var (comAssemblyPath, manifestPath) = TestPaths.GetPaths();

        // Act
        var act = () => Executor.Create<NativeCOM.StringConcatenatorClass>(comAssemblyPath, manifestPath, null!);

        // Assert
        act.Should().Throw<ArgumentNullException>().WithParameterName("factory");
    }

    [Fact]
    public void FreeShouldThrowWhenHandleIsNull()
    {
        // Act
        var act = () => Executor.Free<NativeCOM.StringConcatenatorClass>(null!);

        // Assert
        act.Should().Throw<ArgumentNullException>().WithParameterName("comObjectHandle");
    }
}
