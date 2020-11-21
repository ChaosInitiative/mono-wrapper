using System;
using System.CodeDom;
using System.CodeDom.Compiler;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using Microsoft.CSharp;
using Microsoft.CodeAnalysis;
using Newtonsoft;
using Newtonsoft.Json;
using Microsoft.CodeAnalysis.CSharp;
using ScriptSystem.Events;

namespace ScriptSystem
{
	struct ScriptConfigFile
	{
		/* Title of the addon. Should be nice and pretty */
		public string title { get; set; }
		
		/* Name of the addon. Much less pretty, used to reference the mod from commands */
		public string name { get; set; }
		
		/* List of authors of the mod */
		public IList<string> authors { get; set; }
		
		/* Website of the mod */
		public string website { get; set; }
		
		/* License of the mod (e.g. GPLv3, MIT, etc.) */
		public string license { get; set; }
		
		/* Short description of the mod */
		public string description { get; set; }
		
		/* List of CSharp sources */
		public string srcdir { get; set; }
		
		/* Assets this addon contains */
		public IList<string> assets { get; set; }
		
		/* List of addon dependencies we have */
		public IList<string> dependencies { get; set; }
	}

	public class Script
	{
		private ScriptConfigFile m_config;
		public bool isGood { private set; get; }
		public string m_buildDir { private set; get; }
		public string m_compileErrors { private set; get; }
		public string m_compileWarnings { private set; get; }
		public Assembly m_assembly { private set; get; }
		private AppDomain m_domain;
		public int m_permissionLevel { private set; get; }
		private bool _watchFiles;
		private FileSystemWatcher _watcher;
		private bool _loaded = false;
		private byte[] _assemblyData;
		private CSharpCompilation _compilation;

		/* Method references and such */
		private Dictionary<string, MethodInfo> _eventHandlers;
		
		public Script(string configFile, string buildDir, int permissionLevel, bool watchFiles = false)
		{
			m_permissionLevel = permissionLevel;
			m_buildDir = buildDir;
			_watchFiles = watchFiles;

			try
			{
				string txt = File.ReadAllText(configFile);
				m_config = JsonConvert.DeserializeObject<ScriptConfigFile>(txt);
				isGood = true;
			}
			catch (Exception e)
			{
				isGood = false;
			}
			
			
			/* If file watching is enabled, let's go ahead and subscribe to it */
			if (watchFiles)
			{
				_watcher = new FileSystemWatcher(m_config.srcdir);
				_watcher.Changed += OnChanged;
				_watcher.Created += OnChanged;
				_watcher.Deleted += OnChanged;
				_watcher.Renamed += OnChanged;
				_watcher.IncludeSubdirectories = true;
				_watcher.EnableRaisingEvents = true;
			}
			
			
		}

		public bool Compile()
		{
			/* TODO: Allow for compilation against different C# versions */
			var options = CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.CSharp8);
			var compileOptions = new CSharpCompilationOptions(
				OutputKind.DynamicallyLinkedLibrary,
				optimizationLevel: OptimizationLevel.Release,
				platform: Platform.AnyCpu,
				assemblyIdentityComparer: DesktopAssemblyIdentityComparer.Default
			);

			var files = Directory.GetFiles(m_config.srcdir);
			List<SyntaxTree> trees = new List<SyntaxTree>();
			foreach (var f in files)
			{
				trees.Add(SyntaxFactory.ParseSyntaxTree(File.ReadAllText(f), options));
			}

			_compilation = CSharpCompilation.Create(m_config.title + ".dll", trees.ToArray(), options: compileOptions);

			var dataStream = new MemoryStream();
			_compilation.Emit(dataStream);
			_assemblyData = dataStream.GetBuffer();

			return true;
		}

		public bool Load()
		{
			if (!DoSecurityAudit())
				return false;
			m_domain = AppDomain.CreateDomain(m_config.name);
			m_assembly = m_domain.Load(_assemblyData);

			RegisterEventHandlers();
			
			
			_loaded = true;

			return true;
		}

		private bool DoSecurityAudit()
		{
			// TODO: How do we get a full list of type references?? 
			foreach (var assembly in _compilation.ReferencedAssemblyNames)
			{
			}
			return false;
		}

		/* Registers all event handlers */
		private void RegisterEventHandlers()
		{
			foreach (var type in m_assembly.DefinedTypes)
			{
				foreach (var method in type.DeclaredMethods)
				{
					if(!method.IsStatic) continue;
					Event evattr = method.GetCustomAttribute<Event>();
					if (evattr != null)
					{
						_eventHandlers.Add(evattr.EventName, method);
					}
				}
			}
		}
		
		/* Unregisters all event handlers */
		private void UnregisterEventHandlers()
		{
			_eventHandlers.Clear();
		}

		public bool Unload()
		{
			if (!_loaded) return false;

			UnregisterEventHandlers();
			
			_loaded = false;
			return true;
		}
		
		private void OnChanged(object src, FileSystemEventArgs args)
		{
			/* Hot loading code here; Unload, Compile and reload the code */
			if(!Unload()) return;

			if(!Compile()) return;

			Load();
		}
		
		
	}
}