using System;
using System.CodeDom;
using System.CodeDom.Compiler;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using Microsoft.CSharp;
using Newtonsoft;
using Newtonsoft.Json;

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
		public IList<string> sources { get; set; }
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
		
		public Script(string configFile, string buildDir)
		{
			m_buildDir = buildDir;
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
		}

		/// <summary>
		/// Call this to compile the script.
		/// </summary>
		/// <returns>True if </returns>
		public bool Compile()
		{
			if (!isGood)
				return false;
			
			CSharpCodeProvider provider = new CSharpCodeProvider();
			ICodeCompiler compiler = provider.CreateCompiler();

			CompilerParameters parameters = new CompilerParameters();
			parameters.GenerateInMemory = true;
			parameters.GenerateExecutable = false;
			parameters.IncludeDebugInformation = true;
			parameters.TreatWarningsAsErrors = false;
			parameters.TempFiles = new TempFileCollection(".", false);
			
			CompilerResults results = compiler.CompileAssemblyFromFileBatch(parameters, m_config.sources.ToArray());

			m_compileWarnings = results.Output.ToString();
			
			if (results.Errors.HasErrors)
			{
				m_compileErrors = results.Errors.ToString();
				m_assembly = null;
				return false;
			}

			m_assembly = results.CompiledAssembly;
			return true;
		}

		/// <summary>
		/// Loads the compiled assembly into a new app domain
		/// </summary>
		/// <returns>True if loading succeeds, false if failed</returns>
		public bool Load()
		{
			if (m_assembly == null) return false;

			System.Security.Policy.Evidence policy = new System.Security.Policy.Evidence();
			
			m_domain = AppDomain.CreateDomain(m_config.name, policy);

			return true;
		}

		public bool Unload()
		{
			AppDomain.Unload(m_domain);
			return true;
		}
	}
}