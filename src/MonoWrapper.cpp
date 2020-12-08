/* Mono includes */
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/class.h>
#include <mono/metadata/reflection.h>
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/profiler.h>
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/mono-gc.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-gc.h>
#include <mono/metadata/loader.h>
#include <mono/metadata/profiler.h>

#include "MonoWrapper.h"

#include <assert.h>
#include <string.h>

MonoDomain* g_jitDomain;

struct _MonoProfiler {
	MonoProfilerHandle handle;
	uint64_t totalAllocs;
	uint64_t totalMoves;
	uint64_t bytesMoved;
	uint64_t bytesAlloc;
};

MonoProfiler g_monoProfiler;

/* Profiler methods */
static void Profiler_RuntimeInit(MonoProfiler* prof);
static void Profiler_RuntimeShutdownStart(MonoProfiler* prof);
static void Profiler_RuntimeShutdownEnd(MonoProfiler* prof);
static void Profiler_ContextLoaded(MonoProfiler* prof, MonoAppContext* ctx);
static void Profiler_ContextUnloaded(MonoProfiler* prof, MonoAppContext* ctx);
static void Profiler_GCEvent(MonoProfiler* prof, MonoProfilerGCEvent ev, uint32_t gen, mono_bool isSerial);
static void Profiler_GCAlloc(MonoProfiler* prof, MonoObject* obj);
static void Profiler_GCResize(MonoProfiler* prof, uintptr_t size);


//================================================================//
//
// Managed Assembly
//
//================================================================//
ManagedAssembly::ManagedAssembly(ManagedScriptContext* ctx, const std::string& name, MonoImage* img, MonoAssembly* ass) :
	m_ctx(ctx),
	m_path(name),
	m_image(img),
	m_assembly(ass),
	m_populated(false)
{

}

void ManagedAssembly::PopulateReflectionInfo()
{
	assert(m_ctx);
	assert(m_image);
	assert(m_assembly);
	if(m_populated) return;
	m_populated = true;
	const MonoTableInfo* tab = mono_image_get_table_info(m_image, MONO_TABLE_TYPEDEF);
	int rows = mono_table_info_get_rows(tab);
	for(int i = 0; i < rows; i++) {
		uint32_t cols[MONO_TYPEDEF_SIZE];
		mono_metadata_decode_row(tab, i, cols, MONO_TYPEDEF_SIZE);
		const char* ns = mono_metadata_string_heap(m_image, cols[MONO_TYPEDEF_NAMESPACE]);
		const char* c = mono_metadata_string_heap(m_image, cols[MONO_TYPEDEF_NAME]);
		m_ctx->FindClass(ns, c);
	}
}

/* NOTE: No info is cached here because it should be called sparingly! */
void ManagedAssembly::GetReferencedTypes(std::vector<std::string>& refList)
{
	assert(m_ctx);
	assert(m_image);
	assert(m_assembly);

	const MonoTableInfo* tab = mono_image_get_table_info(m_image, MONO_TABLE_TYPEREF);
	int rows = mono_table_info_get_rows(tab);
	/* Parse all of the referenced types, and add them to the refList */
	for(int i = 0; i < rows; i++) {
		uint32_t cols[MONO_TYPEREF_SIZE];
		mono_metadata_decode_row(tab, i, cols, MONO_TYPEREF_SIZE);
		const char* ns = mono_metadata_string_heap(m_image, cols[MONO_TYPEREF_NAMESPACE]);
		const char* n = mono_metadata_string_heap(m_image, cols[MONO_TYPEREF_NAME]);
		char type[512];
		snprintf(type, sizeof(type), "%s.%s", ns, n);
		refList.push_back((const char*)type);
	}
}

bool ManagedAssembly::ValidateAgainstWhitelist(const std::vector<std::string>& whiteList)
{
	std::vector<std::string> refTypes;
	this->GetReferencedTypes(refTypes);

	for(auto& type : refTypes) {
		bool found = false;
		for(auto& wType : whiteList) {
			if(type == wType) {
				found = true;
			}
		}
		if(!found) 
			return false;
	}
	return true;
}

void ManagedAssembly::DisposeReflectionInfo()
{
	for(auto& kvPair : m_classes) {
		delete kvPair.second;
	}
	m_classes.clear();
}

void ManagedAssembly::Unload()
{
	this->InvalidateHandle();
	this->DisposeReflectionInfo();
}

void ManagedAssembly::InvalidateHandle()
{
	ManagedBase::InvalidateHandle();
	for(auto& kv : m_classes) {
		kv.second->InvalidateHandle();
	}
}

//================================================================//
//
// Managed Type
//
//================================================================//


ManagedType::ManagedType(MonoType* type) :
	m_type(type)
{
	m_isVoid = mono_type_is_void(type);
	m_isStruct = mono_type_is_struct(type);
	m_isRef = mono_type_is_reference(type);
	m_isPtr = mono_type_is_pointer(type);
}

bool ManagedType::Equals(const ManagedType* other) const
{
	return mono_type_get_type(m_type) == mono_type_get_type(other->m_type);
}

const std::string& ManagedType::Name() const
{
	if(m_name.empty()) {
		char* c = mono_type_get_name(m_type);
		m_name.copy(c, strlen(c));
		mono_free(c);
	}
	return m_name;
}


//================================================================//
//
// Managed Method
//
//================================================================//

ManagedMethod::ManagedMethod(MonoMethod *method, ManagedClass *cls) :
	m_populated(false)
{
	if (!method) return;
	m_method = method;
	m_attrInfo = mono_custom_attrs_from_method(method);
	m_token = mono_method_get_token(method);
	m_class = cls;
	if(m_token) {
		m_signature = mono_method_get_signature(m_method, m_class->m_assembly->m_image, m_token);
		assert(m_signature);
	}

	m_name = mono_method_get_name(m_method);
	m_paramCount = mono_signature_get_param_count(m_signature);
	
	m_returnType = new ManagedType(mono_signature_get_return_type(m_signature));

	if (!m_attrInfo)
	{
		return;
	}

}

ManagedMethod::~ManagedMethod()
{
	if (m_attrInfo)
		mono_custom_attrs_free(m_attrInfo);
	if(m_returnType)
		delete m_returnType;
	for(auto x : m_params) {
		delete x;
	}
	m_params.clear();
}

ManagedAssembly *ManagedMethod::Assembly() const
{
	return m_class->m_assembly;
}

ManagedClass* ManagedMethod::Class() const
{
	return m_class;
}

void ManagedMethod::InvalidateHandle()
{
	ManagedBase::InvalidateHandle();
	m_returnType->InvalidateHandle();
	for (auto &parm : m_params)
	{
		parm->InvalidateHandle();
	}
	for (auto &a : m_attributes)
	{
		a->InvalidateHandle();
	}
}

bool ManagedMethod::MatchSignature(MonoType* returnval, std::vector<MonoType*> params)
{
	/* Pre-verification that the params are likely to be equal */
	if(m_paramCount != params.size()) {
		return false;
	}

	MonoType* type = mono_signature_get_return_type(m_signature);
	if(!mono_metadata_type_equal(type, returnval))
		return false;

	void* iter = nullptr;
	type = nullptr;
	int i = 0;
	while((type = mono_signature_get_params(m_signature, &iter))) {
		if(!mono_metadata_type_equal(type, params[i]))
			return false;
		i++;
	}

	return true;
}

bool ManagedMethod::MatchSignature(std::vector<MonoType*> params)
{
	if(m_paramCount != params.size()) {
		return false;
	}

	// Verify that return type is VOID
	// quite messy indeed
	MonoType* type = mono_signature_get_return_type(m_signature);
	if(!mono_metadata_type_equal(type, mono_class_get_type(mono_get_void_class()))) {
		return false;
	}

	void* iter = nullptr;
	type = nullptr;
	int i = 0;
	while((type = mono_signature_get_params(m_signature, &iter))) {
		if(!mono_metadata_type_equal(type, params[i])) {
			return false;
		}
		i++;
	}
	return true;
}

bool ManagedMethod::MatchSignature()
{
	MonoType* type = mono_signature_get_return_type(m_signature);
	if(!mono_metadata_type_equal(type, mono_class_get_type(mono_get_void_class()))) {
		return false;
	}

	// Check param count is 0
	return mono_signature_get_param_count(m_signature) == 0;
}

MonoObject *ManagedMethod::Invoke(ManagedObject *obj, void **params)
{
	MonoObject * exception = nullptr;
	MonoObject* o = mono_runtime_invoke(m_method, obj, params, &exception);

	if(exception) {
		m_class->m_assembly->ReportException(exception);
		return nullptr;
	}
	return o;
}

MonoObject *ManagedMethod::InvokeStatic(void **params)
{
	MonoObject * exception = nullptr;
	MonoObject* o = mono_runtime_invoke(m_method, nullptr, params, &exception);

	if(exception) {
		m_class->m_assembly->ReportException(exception);
		return nullptr;
	}
	return o;
}



//================================================================//
//
// Managed Field
//
//================================================================//

ManagedField::ManagedField(MonoClassField *fld, class ManagedClass *cls) :
	m_class(cls),
	m_field(fld)
{
	const char* n = mono_field_get_name(fld);
	m_name = n;
}

ManagedField::~ManagedField()
{

}


//================================================================//
//
// Managed Property
//
//================================================================//

ManagedProperty::ManagedProperty(MonoProperty* prop, ManagedClass* cls) :
	m_class(cls),
	m_property(prop)
{
	const char* n = mono_property_get_name(prop);
	m_name = n;
	m_getMethod = mono_property_get_get_method(prop);
	m_setMethod = mono_property_get_set_method(prop);
}

ManagedProperty::~ManagedProperty()
{

}

//================================================================//
//
// Managed Class
//
//================================================================//

ManagedClass::ManagedClass(ManagedAssembly *assembly, const std::string &ns, const std::string &cls) :
	m_assembly(assembly),
	m_className(cls),
	m_namespaceName(ns),
	m_populated(false),
	m_numConstructors(0)
{
	m_class = mono_class_from_name(m_assembly->m_image, ns.c_str(), cls.c_str());
	if (!m_class)
	{
		return;
	}
	m_attrInfo = mono_custom_attrs_from_class(m_class);

	/* If there is no class name or namespace, something is fucky */
	if (!cls.empty() && m_attrInfo)
	{
		if(mono_custom_attrs_has_attr(m_attrInfo, m_class)) {
			m_attributes.push_back(new ManagedObject(mono_custom_attrs_get_attr(m_attrInfo, m_class), this));
		}
	}

	PopulateReflectionInfo();
}

ManagedClass::ManagedClass(ManagedAssembly *assembly, MonoClass *_cls, const std::string &ns, const std::string &cls) :
	m_className(cls),
	m_class(_cls),
	m_namespaceName(ns),
	m_populated(false),
	m_assembly(assembly),
	m_numConstructors(0)
{
	m_attrInfo = mono_custom_attrs_from_class(m_class);

	/* If there is no class name or namespace, something is fucky */
	if (!cls.empty() && m_attrInfo)
	{
		if(mono_custom_attrs_has_attr(m_attrInfo, m_class)) {
			m_attributes.push_back(new ManagedObject(mono_custom_attrs_get_attr(m_attrInfo, m_class), this));
		}
	}

	PopulateReflectionInfo();
}

ManagedClass::~ManagedClass()
{
	if (m_attrInfo)
		mono_custom_attrs_free(m_attrInfo);

}

void ManagedClass::PopulateReflectionInfo()
{
	assert(!m_valid);
	if(m_valid) return;
	void *iter = nullptr;

	m_valueClass = mono_class_is_valuetype(m_class);
	m_enumClass = mono_class_is_enum(m_class);
	m_delegateClass = mono_class_is_delegate(m_class);
	m_nullableClass = mono_class_is_nullable(m_class);
	m_size = mono_class_instance_size(m_class);

	MonoMethod *method;
	while ((method = mono_class_get_methods(m_class, &iter)))
	{
		if(strcmp(mono_method_get_name(method), ".ctor") == 0)
			m_numConstructors++;
		m_methods.push_back(new ManagedMethod(method, this));
	}

	MonoClassField *field;
	iter = nullptr;
	while ((field = mono_class_get_fields(m_class, &iter)))
	{
		m_fields.push_back(new ManagedField(field, this));
	}

	MonoProperty *props;
	iter = nullptr;
	while ((props = mono_class_get_properties(m_class, &iter)))
	{
		m_properties.push_back(new ManagedProperty(props, this));
	}

	m_populated = true;
}
void ManagedClass::InvalidateHandle()
{
	ManagedBase<ManagedClass>::InvalidateHandle();
	for(auto& attr : m_attributes) {
		attr->InvalidateHandle();
	}
	for(auto& meth : m_methods) {
		meth->InvalidateHandle();
	}
}

// TODO: Investigate perf of this, maybe use a hashmap? Might just be faster to not though.
ManagedMethod *ManagedClass::FindMethod(const std::string &name)
{
	for(auto m : m_methods) {
		if(m->m_name == name) return m;
	}
	return nullptr;
}

ManagedField *ManagedClass::FindField(const std::string &name)
{
	for(auto& f : m_fields) {
		if(f->m_name == name)
			return f;
	}
	return nullptr;
}

ManagedProperty *ManagedClass::FindProperty(const std::string &prop)
{
	for(auto& p : m_properties) {
		if(p->m_name == prop)
			return p;
	}
	return nullptr;
}

/* Creates an instance of a this class */ 
ManagedObject* ManagedClass::CreateInstance(std::vector<MonoType*> signature, void** params)
{
	for(auto& method : m_methods) {
		if(method->m_name == ".ctor" && method->MatchSignature(signature)) {
			MonoObject* exception = nullptr;
			MonoObject* obj = mono_object_new(m_assembly->m_ctx->m_domain, m_class); // Allocate storage
			mono_runtime_object_init(obj); // Invoke default constructor
			if(signature.size() > 0) {
				mono_runtime_invoke(method->m_method, obj, params, &exception);
			}
			if(exception || !obj) {
				m_assembly->m_ctx->ReportException(obj, m_assembly);
				return nullptr;
			}
			return new ManagedObject(obj, this);
		}
	}

	return nullptr;
}

mono_byte ManagedClass::NumConstructors() const
{
	return m_numConstructors;
}

//================================================================//
//
// Managed Object
//
//================================================================//

bool ManagedObject::SetProperty(struct ManagedProperty *prop, void *value)
{
	MonoObject * exception = nullptr;
	void* params[] = { value };

	MonoObject* res = mono_runtime_invoke(prop->m_setMethod, m_obj, params, &exception);

	if(exception) return false;
	return true;
}

bool ManagedObject::SetField(struct ManagedField *prop, void *value)
{
	mono_field_set_value(m_obj, prop->RawField(), value);
	return true;
}

bool ManagedObject::GetProperty(struct ManagedProperty *prop, void **outValue)
{
	MonoObject* exception = nullptr;
	void* params[] = {outValue};

	MonoObject* res = mono_runtime_invoke(prop->m_getMethod, m_obj, NULL, &exception);

	if(!res || exception) {
		return false;
	}

	*outValue = mono_object_unbox(res);
	return true;
}

bool ManagedObject::GetField(struct ManagedField *prop, void *outValue)
{
	mono_field_get_value(m_obj, prop->RawField(), outValue);
	return true;
}

bool ManagedObject::SetProperty(const std::string &p, void *value)
{
	for(auto prop : m_class->m_properties) {
		if(p == prop->m_name) {
			return this->SetProperty(prop, value);
		}
	}
	return false;
}

bool ManagedObject::SetField(const std::string &p, void *value)
{
	for(auto& f : m_class->m_fields) {
		if(f->m_name == p) {
			return this->SetField(f, value);
		}
	}
	return false;
}

bool ManagedObject::GetProperty(const std::string &p, void **outValue)
{
	for(auto& prop : m_class->m_properties) {
		if(prop->m_name == p) {
			return this->GetProperty(prop, outValue);
		}
	}
	return false;
}

bool ManagedObject::GetField(const std::string &p, void *outValue)
{
	for(auto& f : m_class->m_fields) {
		if(f->m_name == p) {
			return this->GetField(f, outValue);
		}
	}
	return false;
}

MonoObject *ManagedObject::Invoke(struct ManagedMethod *method, void **params)
{
	return method->Invoke(this, params);
}



//================================================================//
//
// Managed Script Context
//
//================================================================//

ManagedScriptContext::ManagedScriptContext(const std::string& baseImage) :
	m_baseImage(baseImage)
{
	//m_domain = mono_jit_init(baseImage.c_str());
	m_domain = mono_domain_create_appdomain(strdup(baseImage.c_str()), strdup("mono-config"));
	//m_domain = mono_domain_create();

	MonoAssembly* ass = mono_domain_assembly_open(m_domain, baseImage.c_str());
	MonoImage* img = mono_assembly_get_image(ass);
	ManagedAssembly* newass = new ManagedAssembly(this, baseImage, img, ass);
	m_loadedAssemblies.push_back(newass);
	newass->PopulateReflectionInfo();
}

ManagedScriptContext::~ManagedScriptContext()
{
	for(auto& a : m_loadedAssemblies) 
	{
		if(a->m_image)
			mono_image_close(a->m_image);
		if(a->m_assembly)
			mono_assembly_close(a->m_assembly);
	}
	if(m_domain)
		mono_domain_unload(m_domain);
}

bool ManagedScriptContext::LoadAssembly(const char* path)
{
	if(!m_domain) return false;
	MonoAssembly* ass = mono_domain_assembly_open(m_domain, path);
	if(!ass) return false;

	MonoImage* img = mono_assembly_get_image(ass);
	if(!img) {
		return false;
	}
	ManagedAssembly* newass = new ManagedAssembly(this, path, img, ass);
	m_loadedAssemblies.push_back(newass);
	newass->PopulateReflectionInfo();
	return true;
}

bool ManagedScriptContext::UnloadAssembly(const std::string& name)
{
	for(auto it = m_loadedAssemblies.begin(); it != m_loadedAssemblies.end(); ++it)
	{
		if((*it)->m_path == name)
		{
			if((*it)->m_image)
				mono_image_close((*it)->m_image);
			if((*it)->m_assembly)
				mono_assembly_close((*it)->m_assembly);
			m_loadedAssemblies.erase(it);
			return true;
		}
	}
	return false;
}

/* Performs a class search in all loaded assemblies */
/* If you have the assembly name, please use the alternative version of this function */
ManagedClass* ManagedScriptContext::FindClass(const std::string& ns, const std::string& cls)
{
	/* Try to find the managed class in each of the assemblies. if found, create the managed class and return */
	/* Also check the hashmap we have setup */
	for(auto& a : m_loadedAssemblies)
	{
		ManagedClass* _cls = nullptr;
		if((_cls = FindClass(a, ns, cls)))
			return _cls;
	}
	return nullptr;
}

ManagedClass* ManagedScriptContext::FindClass(ManagedAssembly* assembly, const std::string& ns, const std::string& cls)
{
	auto itpair = assembly->m_classes.equal_range(ns);
	for(auto it = itpair.first; it != itpair.second; ++it)
	{
		if(it->second->m_className == cls)
			return it->second;
	}

	/* Have mono perform the class lookup. If it's there, create and add a new managed class */
	MonoClass* monoClass = mono_class_from_name(assembly->m_image, ns.c_str(), cls.c_str());
	if(monoClass)
	{
		ManagedClass* _class = new ManagedClass(assembly, monoClass, ns, cls);
		assembly->m_classes.insert({ns, _class});
		return _class;
	}

	return nullptr;
}


ManagedAssembly* ManagedScriptContext::FindAssembly(const std::string &path)
{
	for(auto& a : m_loadedAssemblies)
	{
		if(a->m_path == path)
		{
			return a;
		}
	}
	return nullptr;
}

/* Clears all reflection info stored in each assembly description */
/* WARNING: this will invalidate your handles! */
void ManagedScriptContext::ClearReflectionInfo()
{
	for(auto& a : m_loadedAssemblies)
	{
		for(auto& kvPair : a->m_classes)
		{
			delete kvPair.second;
		}
		a->m_classes.clear();
	}
}

void ManagedScriptContext::PopulateReflectionInfo()
{
	for(auto& a : m_loadedAssemblies) {
		a->PopulateReflectionInfo();
	}
}


bool ManagedScriptContext::ValidateAgainstWhitelist(const std::vector<std::string>& whitelist)
{
	for(auto& a : m_loadedAssemblies) {
		if(!a->ValidateAgainstWhitelist(whitelist))
			return false;
	}
	return true;
}

void ManagedScriptContext::ReportException(MonoObject *obj, ManagedAssembly* ass)
{
	ManagedException_t exc;

	/* Obtain the properties from the method itself */
	ManagedObject* o = new ManagedObject(obj, FindClass(ass, "System", "Exception"));
	MonoString* msg = nullptr;
	MonoString* src = nullptr;
	MonoString* stack = nullptr;
	o->GetProperty(std::string("Message"), reinterpret_cast<void **>(&msg));
	o->GetProperty(std::string("Source"), reinterpret_cast<void **>(&src));
	o->GetProperty(std::string("StackTrace"), reinterpret_cast<void**>(&stack));

	char* stackTraceStr = mono_string_to_utf8(stack);
	char* sourceStr = mono_string_to_utf8(src);
	char* messageStr = mono_string_to_utf8(msg);

	exc.stackTrace = (const char*)stackTraceStr;
	exc.source = (const char*)sourceStr;
	exc.message = (const char*)messageStr;

	mono_free(stackTraceStr);
	mono_free(sourceStr);
	mono_free(messageStr);

	for(auto& c : m_callbacks) {
		c(this, ass, obj, exc);
	}
}


//================================================================//
//
// Managed Script System
//
//================================================================//

ManagedScriptSystem::ManagedScriptSystem(ManagedScriptSystemSettings_t settings) :
	m_settings(settings)
{
	/* Basically just a guard to ensure we dont have multiple per process */
	static bool g_managedScriptSystemExists = false;
	if(g_managedScriptSystemExists) {
		printf("A managed script system already exists!\n\tThere can only be one per process because of how mono works.\n\tPlease fix your program so only one script system is made\n\n");
		abort();
	}
	g_managedScriptSystemExists = true;

	if(settings.configIsFile)
		mono_config_parse(settings.configData);
	else
		mono_config_parse_memory(settings.configData);

	/* Create and register the new profiler */
	g_monoProfiler.handle = mono_profiler_create(&g_monoProfiler);
	mono_profiler_set_runtime_initialized_callback(g_monoProfiler.handle, Profiler_RuntimeInit);
	mono_profiler_set_runtime_shutdown_begin_callback(g_monoProfiler.handle, Profiler_RuntimeShutdownStart);
	mono_profiler_set_runtime_shutdown_end_callback(g_monoProfiler.handle, Profiler_RuntimeShutdownEnd);
	mono_profiler_set_gc_allocation_callback(g_monoProfiler.handle, Profiler_GCAlloc);
	mono_profiler_set_gc_event_callback(g_monoProfiler.handle, Profiler_GCEvent);
	mono_profiler_set_gc_resize_callback(g_monoProfiler.handle, Profiler_GCResize);
	mono_profiler_set_context_loaded_callback(g_monoProfiler.handle, Profiler_ContextLoaded);
	mono_profiler_set_context_unloaded_callback(g_monoProfiler.handle, Profiler_ContextUnloaded);

	/* Register our memory allocator for mono */
	if(!settings._malloc) settings._malloc = malloc;
	if(!settings._realloc) settings._realloc = realloc;
	if(!settings._free) settings._free = free;
	if(!settings._calloc) settings._calloc = calloc;
	m_allocator = {
		MONO_ALLOCATOR_VTABLE_VERSION,
		settings._malloc,
		settings._realloc,
		settings._free,
		settings._calloc
	};
	mono_set_allocator_vtable(&m_allocator);

	// Create a SINGLE jit environment!
	g_jitDomain = mono_jit_init("abcd");
	if(!g_jitDomain) {
		printf("Failure while creating mono jit!\n");
		assert(0);
		abort();
	}

}

ManagedScriptSystem::~ManagedScriptSystem()
{
	// contexts should be freed before shutdown
	//assert(m_contexts.size() == 0);
	for(auto c : m_contexts) { 
		delete (c);
	}
	mono_jit_cleanup(g_jitDomain);
}

ManagedScriptContext* ManagedScriptSystem::CreateContext(const char* image)
{
	ManagedScriptContext* ctx = new ManagedScriptContext(image);
	m_contexts.push_back(ctx);
	return ctx;
}

void ManagedScriptSystem::DestroyContext(ManagedScriptContext* ctx)
{
	for(auto it = m_contexts.begin(); it != m_contexts.end(); ++it)
	{
		if((*it) == ctx)
		{
			m_contexts.erase(it);
			delete ctx;
			return;
		}
	}
}

uint64_t ManagedScriptSystem::HeapSize() const
{
	return mono_gc_get_heap_size();
}

uint64_t ManagedScriptSystem::UsedHeapSize() const
{
	return mono_gc_get_used_size();
}

class ManagedCompiler *ManagedScriptSystem::CreateCompiler(const std::string& cbin)
{
	ManagedCompiler* c = new ManagedCompiler();
	c->m_sys = this;
	c->m_ctx = CreateContext(cbin.c_str());
	c->Setup();
	return c;
}

void ManagedScriptSystem::DestroyCompiler(ManagedCompiler *c)
{
	DestroyContext(c->m_ctx);
}

void ManagedScriptSystem::RegisterNativeFunction(const char *name, void *func)
{
	mono_add_internal_call(name, func);
}

void ManagedScriptSystem::ReportProfileStats()
{
	MonoProfiler* prof = &g_monoProfiler;
	printf("---- MONO PROFILE REPORT ----\n");
	printf("Total Allocations: %lu\nBytes Allocated: %lu\nTotal Moves: %lu\nBytes Moved: %lu\n", prof->totalAllocs, prof->bytesAlloc, prof->totalMoves, prof->bytesMoved);
}

uint32_t ManagedScriptSystem::MaxGCGeneration()
{
	return mono_gc_max_generation();
}

void ManagedScriptSystem::RunGCCollect(uint32_t gen)
{
	mono_gc_collect(gen);
}

void ManagedScriptSystem::RunGCCollectAll()
{
	for(int i = 0; i < mono_gc_max_generation(); i++) {
		mono_gc_collect(i);
	}
}

//================================================================//
//
// Managed Compiler
//
//================================================================//

ManagedCompiler::ManagedCompiler()
{

}

ManagedCompiler::~ManagedCompiler()
{

}

void ManagedCompiler::Setup()
{
	m_compilerClass = m_ctx->FindClass("ScriptCompiler", "Compiler");
	m_compileMethod = m_compilerClass->FindMethod("Compile");
}


bool ManagedCompiler::Compile(const std::string &buildDir, const std::string &outFile, int langVer)
{
	MonoString* outFileString = mono_string_new_len(m_ctx->m_domain, outFile.c_str(), outFile.length());
	MonoString* buildDirString = mono_string_new_len(m_ctx->m_domain, buildDir.c_str(), buildDir.length());

	assert(outFileString);
	assert(buildDirString);

	void* params[] = {
		&outFileString,
		&buildDirString,
		&langVer
	};
	MonoObject* exception = nullptr;
	MonoObject* ret = mono_runtime_invoke(m_compileMethod->RawMethod(), nullptr, params, nullptr);

	if(exception) {
		printf("Invocation of ScriptCompiler::Compiler::Compile failed due to exception\n");

	} else {
		mono_free(outFileString);
		mono_free(buildDirString);
	}


	return ret != nullptr;
}

static void Profiler_RuntimeInit(MonoProfiler* prof)
{
	prof->bytesMoved = 0;
	prof->totalAllocs = 0;
	prof->totalMoves = 0;
}

static void Profiler_RuntimeShutdownStart(MonoProfiler* prof)
{

}

static void Profiler_RuntimeShutdownEnd(MonoProfiler* prof)
{

}

static void Profiler_ContextLoaded(MonoProfiler* prof, MonoAppContext* ctx)
{

}

static void Profiler_ContextUnloaded(MonoProfiler* prof, MonoAppContext* ctx)
{

}

static void Profiler_GCEvent(MonoProfiler* prof, MonoProfilerGCEvent ev, uint32_t gen, mono_bool isSerial)
{

}

static void Profiler_GCAlloc(MonoProfiler* prof, MonoObject* obj)
{
	prof->bytesAlloc += mono_object_get_size(obj);
	prof->totalAllocs++;
}

static void Profiler_GCResize(MonoProfiler* prof, uintptr_t size)
{
	prof->totalMoves++;
	prof->bytesMoved += size;
}


