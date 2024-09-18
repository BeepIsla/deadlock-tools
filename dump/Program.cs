using SteamDatabase.ValvePak;
using System.Diagnostics;
using System.Text.Json;
using ValveKeyValue;
using ValveResourceFormat;
using ValveResourceFormat.IO;
using ValveResourceFormat.ResourceTypes;
using ValveResourceFormat.Serialization;

namespace Dump
{
	internal class Hero
	{
		public required int id { get; set; }
		public required string name { get; set; }
		public required bool selectable { get; set; }
		public required bool development { get; set; }
		public required bool disabled { get; set; }
	}

	internal class Program
	{
		static void Main(string[] args)
		{
			string? deadlockDir = args.Length > 1 ? args[1] : null;
			if (deadlockDir == null)
			{
				deadlockDir = Steam.GetAppInstallDirectory(1422450 /* Deadlock AppID */);
				if (deadlockDir == null)
				{
					Console.WriteLine("Could not automatically find Deadlock directory, please provide the path to the installation directory manually");
					return;
				}
			}

			if (!IsProbablyDeadlockDirectory(deadlockDir, out var vpkPath, out var locPath))
			{
				Console.WriteLine("Invalid Deadlock directory. Requires files not detected.");
				return;
			}

			var package = new Package();
			package.Read(vpkPath);

			var loc = ReadEnglishTranslations(locPath);
			DumpHeroes(package, loc);
		}

		static bool IsProbablyDeadlockDirectory(string path, out string vpkPath, out string locPath)
		{
			vpkPath = Path.Join(path, "game", "citadel", "pak01_dir.vpk");
			locPath = Path.Join(path, "game", "citadel", "resource", "localization", "citadel_gc", "citadel_gc_english.txt");

			if (!File.Exists(vpkPath))
				return false;
			if (!File.Exists(locPath))
				return false;
			return true;
		}

		static Dictionary<string, string> ReadEnglishTranslations(string locPath)
		{
			using var file = File.Open(locPath, FileMode.Open, FileAccess.Read);
			var kv = KVSerializer.Create(KVSerializationFormat.KeyValues1Text).Deserialize(file);
			var tokens = kv.Children.First(x => x.Name == "Tokens");

			var loc = new Dictionary<string, string>();
			foreach (var child in tokens.Children)
				loc.Add(child.Name.ToLower(), child.Value.ToString() ?? "");
			return loc;
		}

		static string? PanoramaPathToVpkPath(string? path)
		{
			if (path == null)
				return null;

			// Example: m_strIconHeroCard = panorama:"file://{images}/heroes/targetdummy_sm.psd"
			// Which is read by ValveResourceFormat as "file://{images}/heroes/targetdummy_sm.psd"
			// First we just replace the protocol to the correct path
			// If its not a "vtex_c" file we will remove the extension and append it back but with an underscore instead
			path = path.Replace("file://{images}/", "panorama/images/");

			if (!path.EndsWith(".vtex_c"))
			{
				int dot = path.LastIndexOf(".");
				if (dot != -1)
				{
					var extension = path.Substring(dot + 1);
					path = path.Substring(0, dot) + "_" + extension;
				}
				path += ".vtex_c";
			}
			return path;
		}

		static void DumpHeroes(Package package, Dictionary<string, string> loc)
		{
			var entry = package.FindEntry("scripts/heroes.vdata_c");
			package.ReadEntry(entry, out var raw);

			using var ms = new MemoryStream(raw);
			using var resource = new Resource();
			resource.Read(ms);
			Debug.Assert(resource.ResourceType == ResourceType.VData);

			var kv = (BinaryKV3)resource.DataBlock;
			var heroes = new List<Hero>();
			foreach (var pair in kv.Data)
			{
				var value = pair.Value as ValveResourceFormat.Serialization.KeyValues.KVObject;
				if (value == null)
					continue;

				int heroID = value.GetInt32Property("m_HeroID");
				if (heroID == 0)
					continue;

				string? cardVpkPath = PanoramaPathToVpkPath(value.GetStringProperty("m_strIconHeroCard"));
				if (cardVpkPath == null)
					continue;

				loc.TryGetValue(pair.Key, out var heroName);
				if (heroName == null)
					continue;

				DumpHeroImage(package, loc, heroID, heroName, cardVpkPath);

				heroes.Add(new Hero
				{
					id = heroID,
					name = heroName ?? pair.Key,
					selectable = value.GetInt32Property("m_bPlayerSelectable") != 0,
					development = value.GetInt32Property("m_bInDevelopment") != 0,
					disabled = value.GetInt32Property("m_bDisabled") != 0,
				});
			}

			using var file = File.Open("heroes.json", FileMode.Create, FileAccess.Write);
			JsonSerializer.Serialize(file, heroes, new JsonSerializerOptions
			{
				WriteIndented = true,
			});
		}

		static void DumpHeroImage(Package package, Dictionary<string, string> loc, int heroID, string heroName, string cardPath)
		{
			Console.WriteLine($"Dumping '{heroName}'");

			var entry = package.FindEntry(cardPath);
			if (entry == null)
			{
				Console.WriteLine($"\tFailed to find hero portait");
				return;
			}
			package.ReadEntry(entry, out var raw);

			using var ms = new MemoryStream(raw);
			using var resource = new Resource();
			resource.Read(ms);
			Debug.Assert(resource.ResourceType == ResourceType.Texture);

			var texture = (Texture)resource.DataBlock;
			using var bitmap = texture.GenerateBitmap();
			var png = TextureExtract.ToPngImage(bitmap);

			if (!Directory.Exists("heroes"))
				Directory.CreateDirectory("heroes");
			File.WriteAllBytes($"heroes/{heroID}.png", png);
		}
	}
}
