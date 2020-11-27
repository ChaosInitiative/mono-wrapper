using System;
using System.IO;
using Microsoft.CSharp;
using System.Collections;
using System.Collections.Generic;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace ScriptCompiler
{
	
	public static class Compiler
	{
		public static void Compile(string OutputFile, string BuildDirectory, int languageVersion)
		{
			/* Set language version */
			LanguageVersion lver;
			switch (languageVersion)
			{
				case 1:
					lver = LanguageVersion.CSharp1;
					break;
				case 2:
					lver = LanguageVersion.CSharp2;
					break;
				case 3:
					lver = LanguageVersion.CSharp3;
					break;
				case 4:
					lver = LanguageVersion.CSharp4;
					break;
				case 5:
					lver = LanguageVersion.CSharp5;
					break;
				case 6:
					lver = LanguageVersion.CSharp6;
					break;
				case 7:
					lver = LanguageVersion.CSharp7;
					break;
				case 9:
					lver = LanguageVersion.CSharp9;
					break;
				default:
					lver = LanguageVersion.CSharp8;
					break;
			}
			
			var options = CSharpParseOptions.Default.WithLanguageVersion(lver);
			var compileOptions = new CSharpCompilationOptions(
				OutputKind.DynamicallyLinkedLibrary,
				optimizationLevel: OptimizationLevel.Release,
				platform: Platform.AnyCpu,
				assemblyIdentityComparer: DesktopAssemblyIdentityComparer.Default
			);

			var files = Directory.GetFiles(BuildDirectory);
			List<SyntaxTree> trees = new List<SyntaxTree>();
			foreach (var f in files)
			{
				trees.Add(SyntaxFactory.ParseSyntaxTree(File.ReadAllText(f), options));
			}

			var _compilation = CSharpCompilation.Create(OutputFile, trees.ToArray(),
				options: compileOptions);

			var dataStream = new FileStream(OutputFile, FileMode.Create);
			_compilation.Emit(dataStream);
		}
	}
}