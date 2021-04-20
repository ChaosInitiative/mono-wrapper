
/* Mono includes */
#include "monowrapper.h"
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/class.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/reflection.h>
#include <signal.h>

#include <list>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

#include "util.hpp"

unsigned int util::PassedTests = 0;
unsigned int util::TotalTests = 0;

struct TestContext_t
{
	ManagedScriptSystem* scriptSystem;
	ManagedScriptContext* scriptContext;
	ManagedClass* wrapperTestClass; // The general lame class
	ManagedClass* testClass;		// For object test
	ManagedMethod* test1MethodStatic;
	ManagedMethod* test2Method;
};

static void RunBasicTest(TestContext_t&);
static void RunSimpleReturnTest(TestContext_t&);
static void RunObjectTest(TestContext_t&);
static void RunComplexObjectTest(TestContext_t&);
static void LoadTestDLL(TestContext_t&);

int main(int argc, char** argv)
{
	char* monoLibPath = nullptr;
	if (!(monoLibPath = getenv("MONO_LIB_PATH")))
		printf("WARNING: MONO_LIB_PATH env var is not set, mono might not be "
			   "able to load the core libraries!\n");
	else
		mono_set_assemblies_path(monoLibPath);

	FILE* fp = fopen("mono-config", "r");
	if (!fp) {
		printf("Failed to open config file: %s\n", "mono-config");
		abort();
	}
	static char data[16384];
	fread(data, sizeof(data), 1, fp);
	fclose(fp);

	TestContext_t context;
	memset(&context, 0, sizeof(TestContext_t));

	ManagedScriptSystemSettings_t settings;
	settings.configIsFile = false;
	settings.configData = data;
	settings.scriptSystemDomainName = "TestDomain";
	settings._malloc = malloc;
	settings._free = free;
	settings._calloc = calloc;
	settings._realloc = realloc;

	context.scriptSystem = new ManagedScriptSystem(settings);

	LoadTestDLL(context);

	RunBasicTest(context);
	RunSimpleReturnTest(context);
	RunObjectTest(context);
	RunComplexObjectTest(context);
}

static void LoadTestDLL(TestContext_t& context)
{
	char cwd[512];
	getcwd(cwd, sizeof(cwd));
	char fullpath[512];
	snprintf(fullpath, sizeof(fullpath), "test1.dll", cwd);

	context.scriptContext = context.scriptSystem->CreateContext(fullpath);

	if (!context.scriptContext)
		REPORT_FAIL("Failed to create context on %s", fullpath);
	else
		REPORT_PASS("test1.dll load");
}

static void RunBasicTest(TestContext_t& context)
{
	context.wrapperTestClass = context.scriptContext->FindClass("WrapperTests", "WrapperTestClass");

	if (!context.wrapperTestClass)
		REPORT_FAIL("Failed to find WrapperTests.WrapperTestClass");
	else
		REPORT_PASS("WrapperTests.WrapperTestClass class lookup");

	context.test1MethodStatic = context.wrapperTestClass->FindMethod("Test1");

	if (!context.test1MethodStatic)
		REPORT_FAIL("Failed to find WrapperTest.WrapperTestClass.Test1");
	else
		REPORT_PASS("WrapperTests.WrapperTestClass.Test1 method lookup");

	MonoObject* exc = nullptr;
	MonoObject* ret = context.test1MethodStatic->InvokeStatic(nullptr, &exc);

	if (exc) {
		auto desc = context.scriptContext->GetExceptionDescriptor(exc);
		REPORT_FAIL("WrapperTest.WrapperTestClass.Test1 raised exception:\n%s", desc.string_rep.c_str());
		mono_free(exc);
	} else
		REPORT_PASS("WrapperTests.WrapperTestClass.Test1 static method invoke");
}

static void RunSimpleReturnTest(TestContext_t& context)
{
	const char* curTest = "WrapperTest.WrapperTestClass.Test1";
	MonoObject* exc = nullptr;
	MonoObject* ret = context.test1MethodStatic->InvokeStatic(nullptr, &exc);

	if (exc) {
		auto desc = context.scriptContext->GetExceptionDescriptor(exc);
		REPORT_FAIL("%s raised exception:\n%s", curTest, desc.string_rep.c_str());
		mono_free(exc);
	}

	if (mono_object_isinst(ret, mono_get_boolean_class())) {
		MonoBoolean boolean;
		boolean = *(MonoBoolean*)mono_object_unbox(ret);

		if (boolean != 1)
			REPORT_FAIL("%s return check fail", curTest);
		else
			REPORT_PASS("%s return check OK", curTest);
	} else
		REPORT_FAIL("%s returned a non-boolean object", curTest);
}

static void RunObjectTest(TestContext_t& context)
{
}

static void RunComplexObjectTest(TestContext_t& context)
{
}
