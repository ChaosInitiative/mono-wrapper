using System;
using System.Collections;
using System.CodeDom;
using System.CodeDom.Compiler;
using Microsoft.CSharp;

namespace ScriptSystem
{
	public class ScriptCompiler
	{
		private string m_assemblyName;
		private string[] m_files;
		
		/* Properties that enable/disable features */
		public bool IsExecutable { get; set; }
		public bool IncludeDebugInfo { get; set; }
		public int WarningLevel { get; set; }
		public bool GenerateFile { get; set; }
		public bool Optimize { get; set; }
		public string BuildDirectory { get; set; }

		public ScriptCompiler(string assemblyName)
		{
			m_assemblyName = assemblyName;
			BuildDirectory = ".";
		}

		public void SetSourceFiles(string[] files)
		{
			m_files = files;
		}
		
		public string[] GetSourceFiles()
		{
			return m_files;
		}

		public bool Compile()
		{
			CSharpCodeProvider provider = new CSharpCodeProvider();

			CompilerParameters parms = new CompilerParameters();

			/* Setup the compiler params */
			parms.GenerateExecutable = IsExecutable;
			parms.IncludeDebugInformation = IncludeDebugInfo;
			parms.WarningLevel = WarningLevel;
			parms.GenerateInMemory = !GenerateFile;
			if (Optimize)
				parms.CompilerOptions = "/optimize";

			TempFileCollection buildFiles = new TempFileCollection(BuildDirectory, false);
			
			
			
			
			return true;
		}
	}
}