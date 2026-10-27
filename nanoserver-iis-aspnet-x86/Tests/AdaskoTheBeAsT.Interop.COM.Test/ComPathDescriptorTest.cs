using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

public class ComPathDescriptorTest
{
    [Fact]
    public void ConstructorShouldAssignProperties()
    {
        const string asm = @"C:\temp\MyCom.dll";
        const string manifest = @"C:\temp\MyCom.manifest";

        var sut = new ComPathDescriptor(asm, manifest);

        sut.ComAssemblyPath.Should().Be(asm);
        sut.ComManifestPath.Should().Be(manifest);
    }

    [Fact]
    public void ConstructorShouldThrowWhenAssemblyPathIsNull()
    {
        var act = () => new ComPathDescriptor(null!, @"C:\temp\MyCom.manifest");

        act.Should().Throw<ArgumentNullException>().WithParameterName("comAssemblyPath");
    }

    [Fact]
    public void ConstructorShouldThrowWhenManifestPathIsNull()
    {
        var act = () => new ComPathDescriptor(@"C:\temp\MyCom.dll", null!);

        act.Should().Throw<ArgumentNullException>().WithParameterName("comManifestPath");
    }

    [Theory]
    [InlineData("")]
    [InlineData(" ")]
    [InlineData("\t")]
    public void ConstructorShouldThrowWhenAssemblyPathIsEmptyOrWhitespace(string value)
    {
        var act = () => new ComPathDescriptor(value, @"C:\temp\MyCom.manifest");

        act.Should().Throw<ArgumentException>().WithParameterName("comAssemblyPath");
    }

    [Theory]
    [InlineData("")]
    [InlineData(" ")]
    [InlineData("\t")]
    public void ConstructorShouldThrowWhenManifestPathIsEmptyOrWhitespace(string value)
    {
        var act = () => new ComPathDescriptor(@"C:\temp\MyCom.dll", value);

        act.Should().Throw<ArgumentException>().WithParameterName("comManifestPath");
    }
}
