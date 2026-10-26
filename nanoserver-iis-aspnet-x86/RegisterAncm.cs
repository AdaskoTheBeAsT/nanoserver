#:property PublishAot=false
using System.Xml;

var sysnativePath = @"C:\Windows\Sysnative\inetsrv\config\applicationHost.config";
var system32Path = @"C:\Windows\System32\inetsrv\config\applicationHost.config";
var path = args.Length > 0 ? args[0] : (File.Exists(sysnativePath) ? sysnativePath : system32Path);

var doc = new XmlDocument { PreserveWhitespace = true };
doc.Load(path);

if (doc.SelectSingleNode("//configuration/configSections/sectionGroup[@name='system.webServer']") is XmlElement group &&
    group.SelectSingleNode("section[@name='aspNetCore']") is null)
{
    var section = doc.CreateElement("section");
    section.SetAttribute("name", "aspNetCore");
    section.SetAttribute("overrideModeDefault", "Allow");
    group.AppendChild(section);
    doc.Save(path);
}
