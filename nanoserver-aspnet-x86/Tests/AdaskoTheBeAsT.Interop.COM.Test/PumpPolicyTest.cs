using AwesomeAssertions;
using Xunit;

namespace AdaskoTheBeAsT.Interop.COM.Test;

#pragma warning disable IDISP007 // Tests own the handles returned by their local factories.
public class PumpPolicyTest
{
    private static string AssemblyPath => TestPaths.AssemblyPath;

    private static string ManifestPath => TestPaths.ManifestPath;

    [Theory]
    [InlineData(false, false, false)]
    [InlineData(false, false, true)]
    [InlineData(false, true, false)]
    [InlineData(false, true, true)]
    [InlineData(true, false, false)]
    [InlineData(true, false, true)]
    [InlineData(true, true, false)]
    [InlineData(true, true, true)]
    public void ExecuteShouldHonorPumpPolicy(bool pump, bool collection, bool useStatic)
    {
        TestWindow.OnStaThread(() =>
        {
            using var window = new TestWindow();
            var descriptors = new[] { new ComPathDescriptor(AssemblyPath, ManifestPath) };
            var executor = new ComExecutor(pump);
            window.Post();
            var result = (useStatic, collection) switch
            {
                (true, true) => Executor.Execute(descriptors, () => { }, pump),
                (true, false) => Executor.Execute(AssemblyPath, ManifestPath, () => { }, pump),
                (false, true) => executor.Execute(descriptors, () => { }),
                (false, false) => executor.Execute(AssemblyPath, ManifestPath, () => { }),
            };

            result.Success.Should().BeTrue();
            window.DeliveryCount.Should().Be(pump ? 1 : 0);
            NativeMethods.PumpPendingMessages();
            window.DeliveryCount.Should().Be(1);
        });
    }

    [Theory]
    [InlineData(false, false, false)]
    [InlineData(false, false, true)]
    [InlineData(false, true, false)]
    [InlineData(false, true, true)]
    [InlineData(true, false, false)]
    [InlineData(true, false, true)]
    [InlineData(true, true, false)]
    [InlineData(true, true, true)]
    public void CreateAndFreeShouldRetainCreationPolicy(bool pump, bool collection, bool useStatic)
    {
        TestWindow.OnStaThread(() =>
        {
            using var window = new TestWindow();
            var descriptors = new[] { new ComPathDescriptor(AssemblyPath, ManifestPath) };
            var executor = new ComExecutor(pump);
            window.Post();
            var creation = (useStatic, collection) switch
            {
                (true, true) => Executor.Create(descriptors, () => new object(), pump),
                (true, false) => Executor.Create(AssemblyPath, ManifestPath, () => new object(), pump),
                (false, true) => executor.Create(descriptors, () => new object()),
                (false, false) => executor.Create(AssemblyPath, ManifestPath, () => new object()),
            };

            creation.Success.Should().BeTrue();
            using var handle = creation.Value!;
            window.DeliveryCount.Should().Be(pump ? 1 : 0);
            window.Post();

            // A different executor cannot change an existing handle's policy.
            var release = useStatic ? Executor.Free(handle) : new ComExecutor(!pump).Free(handle);

            release.Success.Should().BeTrue();
            window.DeliveryCount.Should().Be(pump ? 2 : 0);
            NativeMethods.PumpPendingMessages();
            window.DeliveryCount.Should().Be(2);
        });
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void DisposeShouldRetainCreationPolicy(bool pump)
    {
        TestWindow.OnStaThread(() =>
        {
            using var window = new TestWindow();
            var creation = new ComExecutor(pump).Create(AssemblyPath, ManifestPath, () => new object());
            creation.Success.Should().BeTrue();
            window.Post();

            creation.Value!.Dispose();

            window.DeliveryCount.Should().Be(pump ? 1 : 0);
            NativeMethods.PumpPendingMessages();
            window.DeliveryCount.Should().Be(1);
        });
    }

    [Fact]
    public void DefaultExecutorShouldKeepAutomaticPumpingEnabled()
    {
        TestWindow.OnStaThread(() =>
        {
            using var window = new TestWindow();
            window.Post();
            new ComExecutor().Execute(AssemblyPath, ManifestPath, () => { }).Success.Should().BeTrue();
            window.DeliveryCount.Should().Be(1);
        });
    }
}
#pragma warning restore IDISP007
