using AwesomeAssertions;

namespace AdaskoTheBeAsT.Interop.COM.Test;

internal static class TestPaths
{
    internal static string AssemblyPath => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "NativeCOM.dll"));

    internal static string ManifestPath => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "NativeCOM.manifest"));

    internal static (string ComAssemblyPath, string ManifestPath) GetPaths()
    {
        var comAssemblyPath = AssemblyPath;
        var manifestPath = ManifestPath;

        File.Exists(comAssemblyPath).Should().BeTrue();
        File.Exists(manifestPath).Should().BeTrue();

        return (comAssemblyPath, manifestPath);
    }
}
