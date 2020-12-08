
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
ResourceContext* g_resourceContext = new ResourceContext();


static void RunMethodTests();
static void RunClassTests();
static void RunWhitelistTests();
static void RunGeneralTests();
static void RunCompileTests();

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

	ManagedScriptSystemSettings_t settings;
	settings.configIsFile = false;
	settings.configData = data;
	settings.scriptSystemDomainName = "ChaosScriptPOC";
	settings._malloc = malloc;
	settings._free = free;
	settings._calloc = calloc;
	settings._realloc = realloc;

	g_scriptSystem = new ManagedScriptSystem(settings);

	RunGeneralTests();
	RunMethodTests();
	RunClassTests();
	RunWhitelistTests();
	//RunCompileTests();

	g_resourceContext->Report();
	g_scriptSystem->ReportProfileStats();
}

ManagedScriptContext* g_mainContext;

static void RunGeneralTests()
{
	g_resourceContext->BeginPoint("Create Context");
	g_mainContext = g_scriptSystem->CreateContext(gLibraryName.c_str());
	g_resourceContext->EndPoint();

	assert(g_mainContext);
}

static void RunMethodTests()
{
	auto _class = g_mainContext->FindClass("ScriptCompiler", "Compiler");

	g_resourceContext->BeginPoint("Method Lookup");
	auto method = _class->FindMethod("Test");
	g_resourceContext->EndPoint();

	/* Signature matching test */
	std::vector<MonoType*> sigMatch = {
	};
	g_resourceContext->BeginPoint("Signature Match 1");
	assert(method->MatchSignature(sigMatch));
	g_resourceContext->EndPoint();

	std::vector<MonoType*> sigMatch2 = {
		mono_class_get_type(mono_get_string_class())
	};
	g_resourceContext->BeginPoint("Signature Match 2 (Intentional Fail)");
	assert(!method->MatchSignature(sigMatch2));
	g_resourceContext->EndPoint();
}

static void RunClassTests()
{
	g_resourceContext->BeginPoint("Class Lookup 1");
	assert(g_mainContext->FindClass("ScriptCompiler", "Compiler"));
	g_resourceContext->EndPoint();

	g_resourceContext->BeginPoint("Class Lookup 2");
	assert(g_mainContext->FindClass("ScriptCompiler", "Compiler"));
	g_resourceContext->EndPoint();


}

static void RunWhitelistTests()
{
	std::vector<std::string> types;

	g_resourceContext->BeginPoint("Find Assembly test");
	auto a = g_mainContext->FindAssembly(gLibraryName.c_str());
	g_resourceContext->EndPoint();
	assert(a);

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

	g_resourceContext->BeginPoint("Whitelist Validation Test");
	bool valid = a->ValidateAgainstWhitelist(whitelist);
	g_resourceContext->EndPoint();
}

// TODO: make this more generalized
static void RunCompileTests()
{
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
}
