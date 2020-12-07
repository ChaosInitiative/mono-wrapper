
/* Mono includes */
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/class.h>
#include <mono/metadata/reflection.h>
#include <mono/metadata/mono-debug.h>
#include <signal.h>
#include "MonoWrapper.h"
#include "Metrics.hpp"

#include <string>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <list>

#include <unistd.h>

std::string gLibraryName;
std::string gConfigName;

ManagedScriptSystem* g_scriptSystem;

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("Please pass a library name to load!\n");
		exit(1);
	}
	gConfigName = argv[1];
	gLibraryName = argv[2];

	FILE* fp = fopen(gConfigName.c_str(), "r");
	if(!fp) {
		printf("Failed to open config file: %s\n", gConfigName.c_str());
		abort();
	}
	static char data[16384];
	fread(data, sizeof(data), 1, fp);
	fclose(fp);

	ResourceContext *rctx = new ResourceContext();

	ManagedScriptSystemSettings_t settings;
	settings.configIsFile = false;
	settings.configData = data;
	settings.scriptSystemDomainName = "ChaosScriptPOC";
	settings._malloc = malloc;
	settings._free = free;
	settings._calloc = calloc;
	settings._realloc = realloc;

	g_scriptSystem = new ManagedScriptSystem(settings);

#if 1
	rctx->BeginPoint("Initial Create Context");
	ManagedScriptContext *ctx = g_scriptSystem->CreateContext(gLibraryName.c_str());
	rctx->EndPoint();

	rctx->BeginPoint("Initial Find Class");
	ManagedClass *cls = ctx->FindClass("ScriptCompiler", "Compiler");
	rctx->EndPoint();

	rctx->BeginPoint("Second Find Class");
	cls = ctx->FindClass("ScriptCompiler", "Compiler");
	rctx->EndPoint();

	if (!cls)
	{
		printf("Unable to find class!\n");
	}

	std::vector<std::string> types;
	auto a = ctx->FindAssembly(gLibraryName.c_str());

	const char* whitelistCStr[] = {
	    "System.Runtime.CompilerServices.CompilationRelaxationsAttribute",
	    "System.Runtime.CompilerServices.RuntimeCompatibilityAttribute",
	    "System.Diagnostics.DebuggableAttribute",
	    ".DebuggingModes",
	    "System.Reflection.AssemblyTitleAttribute",
	    "System.Reflection.AssemblyDescriptionAttribute",
	    "System.Reflection.AssemblyConfigurationAttribute",
	    "System.Reflection.AssemblyCompanyAttribute",
	    "System.Reflection.AssemblyProductAttribute",
	    "System.Reflection.AssemblyCopyrightAttribute",
	    "System.Reflection.AssemblyTrademarkAttribute",
	    "System.Runtime.InteropServices.ComVisibleAttribute",
	    "System.Runtime.InteropServices.GuidAttribute",
	    "System.Reflection.AssemblyFileVersionAttribute",
	    "System.Runtime.Versioning.TargetFrameworkAttribute",
	    "System.ValueType",
	    "System.Runtime.CompilerServices.CompilerGeneratedAttribute",
	    "System.Diagnostics.DebuggerBrowsableState",
	    "System.Diagnostics.DebuggerBrowsableAttribute",
	    "System.Collections.Generic.IList`1",
	    "System.Object",
	    "System.Reflection.Assembly",
	    "System.AppDomain",
	    "System.IO.FileSystemWatcher",
	    "Microsoft.CodeAnalysis.CSharp.CSharpCompilation",
	    "System.Collections.Generic.Dictionary`2",
	    "System.Reflection.MethodInfo",
	    "System.Exception",
	    "Microsoft.CodeAnalysis.CSharp.CSharpParseOptions",
	    "Microsoft.CodeAnalysis.CSharp.CSharpCompilationOptions",
	    "System.Collections.Generic.List`1",
	    "Microsoft.CodeAnalysis.SyntaxTree",
	    "System.IO.MemoryStream",
	    "System.Collections.Immutable.ImmutableArray`1",
	    "System.Nullable`1",
	    "System.Threading.CancellationToken",
	    "System.Collections.Generic.IEnumerator`1",
	    "Microsoft.CodeAnalysis.AssemblyIdentity",
	    "System.Reflection.TypeInfo",
	    "System.IO.FileSystemEventArgs",
	    "System.AttributeTargets",
	    "System.AttributeUsageAttribute",
	    "System.Attribute",
	    "System.IO.File",
	    "Newtonsoft.Json.JsonConvert",
	    "System.IO.FileSystemEventHandler",
	    "System.IO.RenamedEventHandler",
	    "Microsoft.CodeAnalysis.CSharp.LanguageVersion",
	    "Microsoft.CodeAnalysis.DesktopAssemblyIdentityComparer",
	    "Microsoft.CodeAnalysis.OutputKind",
	    "System.Collections.Generic.IEnumerable`1",
	    "Microsoft.CodeAnalysis.OptimizationLevel",
	    "Microsoft.CodeAnalysis.Platform",
	    "Microsoft.CodeAnalysis.ReportDiagnostic",
	    "System.Collections.Generic.KeyValuePair`2",
	    "Microsoft.CodeAnalysis.XmlReferenceResolver",
	    "Microsoft.CodeAnalysis.SourceReferenceResolver",
	    "Microsoft.CodeAnalysis.MetadataReferenceResolver",
	    "Microsoft.CodeAnalysis.AssemblyIdentityComparer",
	    "Microsoft.CodeAnalysis.StrongNameProvider",
	    "Microsoft.CodeAnalysis.MetadataImportOptions",
	    "Microsoft.CodeAnalysis.NullableContextOptions",
	    "System.IO.Directory",
	    "Microsoft.CodeAnalysis.CSharp.SyntaxFactory",
	    "Microsoft.CodeAnalysis.ParseOptions",
	    "System.Text.Encoding",
	    "System.String",
	    "Microsoft.CodeAnalysis.MetadataReference",
	    "Microsoft.CodeAnalysis.Compilation",
	    "Microsoft.CodeAnalysis.Emit.EmitResult",
	    "System.IO.Stream",
	    "Microsoft.CodeAnalysis.ResourceDescription",
	    "Microsoft.CodeAnalysis.Emit.EmitOptions",
	    "Microsoft.CodeAnalysis.IMethodSymbol",
	    "Microsoft.CodeAnalysis.EmbeddedText",
	    "System.Collections.IEnumerator",
	    "System.IDisposable",
	    "System.Reflection.MethodBase",
	    "System.Reflection.CustomAttributeExtensions",
	    "System.Reflection.MemberInfo",
	};

	std::vector<std::string> whitelist(whitelistCStr, std::end(whitelistCStr));

	rctx->BeginPoint("Validation Test");
	bool valid = a->ValidateAgainstWhitelist(whitelist);
	rctx->EndPoint();

	auto stok = mono_class_get_type_token(mono_get_string_class());
	auto itok = mono_class_get_type_token(mono_get_int32_class());
	std::vector<MonoType*> sigMatch = {
		mono_class_get_type(mono_get_string_class()),
		mono_class_get_type(mono_get_string_class()),
		mono_class_get_type(mono_get_int32_class())
	};

	rctx->BeginPoint("Method Lookup");
	auto method = cls->FindMethod("Compile");
	rctx->EndPoint();

	if(!method) {
		printf("Method not found!\n");
	}

	rctx->BeginPoint("Parameter matching");
	bool ok = method->MatchSignature(sigMatch);
	rctx->EndPoint();

	if(!ok) {
		printf("Failed to match sig.\n");
	}
	else {
		printf("Sig matched!\n");
	}


#endif

#if 0
	printf("Create compiler\n");
	ManagedCompiler* compiler = g_scriptSystem->CreateCompiler("src/ScriptCompiler/bin/Debug/ScriptCompiler.dll");
	printf("Try compile\n");
	auto used = g_scriptSystem->UsedHeapSize();
	auto heap = g_scriptSystem->HeapSize();
	printf("mem: %lu/%lu (%f%%)\n", used, heap, (float)heap/(float)used);

	if(!compiler->Compile("src/Scripts/TestScript/src/", "Test.dll", 7)) {
		printf("Compile failed.\n");
		//assert(0);
	}

	used = g_scriptSystem->UsedHeapSize();
	heap = g_scriptSystem->HeapSize();
	printf("mem: %lu/%lu (%f%%)\n", used, heap, (float)heap/(float)used);
#endif

	rctx->Report();

	g_scriptSystem->ReportProfileStats();
}

