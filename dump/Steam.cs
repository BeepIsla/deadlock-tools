using Microsoft.Win32;
using ValveKeyValue;

namespace Dump
{
	public class Steam
	{
		private static string? GetSteamDirectory()
		{
			if (OperatingSystem.IsWindows())
			{
				string key = Environment.Is64BitProcess ? @"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam" : @"HKEY_LOCAL_MACHINE\Software\Valve\Steam";
				return (string?)Registry.GetValue(key, "InstallPath", null);
			}
			else
			{
				return "~/.steam/steam";
			}
		}

		private static string? GetAppLibraryDirectory(uint appID)
		{
			string vdfPath = Path.Join(GetSteamDirectory(), "config", "libraryfolders.vdf");

			using var file = File.Open(vdfPath, FileMode.Open, FileAccess.Read);
			var kv = KVSerializer.Create(KVSerializationFormat.KeyValues1Text).Deserialize(file);
			foreach (var child in kv.Children)
			{
				KVObject apps = child.Children.First(x => x.Name.ToLower() == "apps");
				foreach (var app in apps)
				{
					uint.TryParse(app.Name, out var value);
					if (value == appID)
						return child.Children.First(x => x.Name.ToLower() == "path").Value.ToString();
				}
			}
			return null;
		}

		public static string? GetAppInstallDirectory(uint appID)
		{
			string? libraryPath = GetAppLibraryDirectory(appID);
			if (libraryPath == null)
				return null;

			var vdfPath = Path.Join(libraryPath, "steamapps", $"appmanifest_{appID}.acf");
			if (!File.Exists(vdfPath))
				return null;

			using var file = File.Open(vdfPath, FileMode.Open, FileAccess.Read);
			var kv = KVSerializer.Create(KVSerializationFormat.KeyValues1Text).Deserialize(file);
			var installDir = kv.Children.First(x => x.Name.ToLower() == "installdir");
			return Path.Join(libraryPath, "steamapps", "common", installDir.Value.ToString());
		}
	}
}
