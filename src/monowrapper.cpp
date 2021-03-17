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

#include "monowrapper.h"

#include <assert.h>
#include <string.h>

#ifndef ASSERT 
#define ASSERT(x) assert(x)
#endif

MonoDomain* g_jitDomain;

struct _MonoProfiler {
	MonoProfilerHandle handle;
	uint64_t totalAllocs;
	uint64_t totalMoves;
	uint64_t bytesMoved;
	uint64_t bytesAlloc;
	ManagedScriptSystem* scriptsys;
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
	ASSERT(m_ctx);
	ASSERT(m_image);
	ASSERT(m_assembly);
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
	ASSERT(m_ctx);
	ASSERT(m_image);
	ASSERT(m_assembly);

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

void ManagedAssembly::ReportException(MonoObject *exc)
{
	m_ctx->ReportException(exc, this);
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
		ASSERT(m_signature);
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

MonoObject *ManagedMethod::Invoke(ManagedObject *obj, void **params, MonoObject** _exc)
{
	MonoObject * exception = nullptr;
	MonoObject* o = mono_runtime_invoke(m_method, obj, params, _exc ? _exc : &exception);

	if(exception) {
		m_class->m_assembly->ReportException(exception);
		return nullptr;
	}
	return o;
}

MonoObject *ManagedMethod::InvokeStatic(void **params, MonoObject** _exc)
{
	MonoObject * exception = nullptr;
	MonoObject* o = mono_runtime_invoke(m_method, nullptr, params, _exc ? _exc : &exception);

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
	ASSERT(!m_valid);
	if(m_valid) return;
	void *iter = nullptr;

	m_valueClass = mono_class_is_valuetype(m_class);
	m_enumClass = mono_class_is_enum(m_class);
	m_delegateClass = mono_class_is_delegate(m_class);
	m_nullableClass = mono_class_is_nullable(m_class);
	m_size = mono_class_instance_size(m_class);
	m_alignment = mono_class_min_align(m_class);

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

bool ManagedClass::ImplementsInterface(ManagedClass *interface)
{
	return mono_class_implements_interface(m_class, interface->m_class);
}

bool ManagedClass::DerivedFromClass(ManagedClass* cls)
{
	return mono_class_is_subclass_of(m_class, cls->m_class, true);
}

bool ManagedClass::DerivedFromClass(MonoClass* cls)
{
	return mono_class_is_subclass_of(m_class, cls, true);
}


bool ManagedClass::IsVoid()
{
	return m_class == mono_get_void_class();
}

bool ManagedClass::IsInt16()
{
	return m_class == mono_get_int16_class();
}

bool ManagedClass::IsInt32()
{
	return m_class == mono_get_int32_class();
}

bool ManagedClass::IsInt64()
{
	return m_class == mono_get_int64_class();
}

bool ManagedClass::IsDouble()
{
	return m_class == mono_get_double_class();
}

bool ManagedClass::IsIntptr()
{
	return m_class == mono_get_intptr_class();
}

bool ManagedClass::IsThread()
{
	return m_class == mono_get_thread_class();
}

bool ManagedClass::IsArray()
{
	return m_class == mono_get_array_class();
}

bool ManagedClass::IsByte()
{
	return m_class == mono_get_byte_class();
}

bool ManagedClass::IsChar()
{
	return m_class == mono_get_char_class();
}

bool ManagedClass::IsUInt32()
{
	return m_class == mono_get_uint32_class();
}

bool ManagedClass::IsUInt16()
{
	return m_class == mono_get_uint32_class();
}

bool ManagedClass::IsUInt64()
{
	return m_class == mono_get_uint64_class();
}

bool ManagedClass::IsUIntptr()
{
	return m_class == mono_get_uintptr_class();
}

bool ManagedClass::IsBool()
{
	return m_class == mono_get_boolean_class();
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

	if(!prop->m_setMethod)
		return false;

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

	if(!prop->m_getMethod)
		return false;

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
}

bool ManagedScriptContext::Init()
{
	//m_domain = mono_domain_create_appdomain("abcd", nullptr);
	m_domain = g_jitDomain;

	MonoAssembly* ass = mono_domain_assembly_open(m_domain, m_baseImage.c_str());
	if(!ass)
	{
		return false;
	}

	MonoImage* img = mono_assembly_get_image(ass);

	if(!img)
	{
		return false;
	}
	ManagedAssembly* newass = new ManagedAssembly(this, m_baseImage, img, ass);
	m_loadedAssemblies.push_back(newass);
	newass->PopulateReflectionInfo();

	m_initialized = true;
	return true;
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

/* Used to locate a class not added by any assemblies explicitly loaded by the user */
/* These assemblies are usually going to be system assemblies or members of the C# standard library */
MonoClass* ManagedScriptContext::FindSystemClass(const std::string& ns, const std::string& cls)
{
	struct pvt_t {
		ManagedScriptContext* _this;
		const char* ns;
		const char* cls;
		bool isdone;
		MonoClass* result;
	} pvt;

	pvt.cls = cls.c_str();
	pvt.ns = ns.c_str();
	pvt._this = this;
	pvt.result = nullptr;
	pvt.isdone = false;

	mono_assembly_foreach([](void* ass, void* pvt) {
		MonoAssembly* _ass = static_cast<MonoAssembly*>(ass);
		pvt_t* _pvt = static_cast<pvt_t*>(pvt);

		if(_pvt->isdone)
			return;

		MonoImage* img = mono_assembly_get_image(_ass);
		if(!img)
			return;

		MonoClass* res = mono_class_from_name(img, _pvt->ns, _pvt->cls);

		if(res) {
			_pvt->isdone = true;
			_pvt->result = res;
		}
	}, &pvt);

	return pvt.result;
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
	auto exc = this->GetExceptionDescriptor(obj);

	for(auto& c : m_callbacks) {
		c(this, ass, obj, exc);
	}
}

ManagedException_t ManagedScriptContext::GetExceptionDescriptor( MonoObject *exception )
{
	ManagedException_t exc;

	/* Make sure that the baseclass of the exception is System.Exception. If not, we've got some weird object that we shouldn't have */
	MonoClass* cls = mono_object_get_class(exception);
	MonoClass* excClass = FindSystemClass("System", "Exception");
	if(!mono_object_isinst(exception, excClass))
	{
		return exc;
	}

	/* Obtain the properties by iterating through the class's props */
	MonoProperty* propMsg = mono_class_get_property_from_name(cls, "Message");
	MonoProperty* propSrc = mono_class_get_property_from_name(cls, "Source");
	MonoProperty* propST = mono_class_get_property_from_name(cls, "StackTrace");

	/* Obtain the properties */
	/* This is a big weird and unoptimized because mono I guess */
	MonoObject* msg = nullptr;
	MonoObject* src = nullptr;
	MonoObject* stack = nullptr;
	MonoObject* propexcept = nullptr;
	msg = mono_property_get_value(propMsg, exception, nullptr, &propexcept);
	src = mono_property_get_value(propSrc, exception, nullptr, &propexcept);
	stack = mono_property_get_value(propST, exception, nullptr, &propexcept);

	if(msg) {
		char* buf = mono_string_to_utf8(mono_object_to_string(msg, nullptr));
		exc.message = (const char*)buf;
		mono_free(buf);
	}
	if(src) {
		char* buf = mono_string_to_utf8(mono_object_to_string(src, nullptr));
		exc.source = (const char*)buf;
		mono_free(buf);
	}
	if(stack) {
		char* buf = mono_string_to_utf8(mono_object_to_string(stack, nullptr));
		exc.stackTrace = (const char*)buf;
		mono_free(buf);
	}

	exc.klass = mono_class_get_name(cls);
	exc.ns = mono_class_get_namespace(cls);

	MonoObject* pexc = nullptr;
	char* str = mono_string_to_utf8(mono_object_to_string(exception, &pexc));

	if(!pexc && str) {
		exc.string_rep = (const char*)str;
	}

	if(str)
		mono_free(str);

	return exc;
}

//================================================================//
//
// Managed Script System
//
//================================================================//

ManagedScriptSystem::ManagedScriptSystem(ManagedScriptSystemSettings_t settings) :
	m_settings(settings),
	m_curFrame(nullptr)
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

	/* Push a root profiling context */
	this->PushProfilingContext();

	/* Create and register the new profiler */
	g_monoProfiler.handle = mono_profiler_create(&g_monoProfiler);
	g_monoProfiler.scriptsys = this;
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
		ASSERT(0);
		abort();
	}

}

ManagedScriptSystem::~ManagedScriptSystem()
{
	// contexts should be freed before shutdown
	//ASSERT(m_contexts.size() == 0);
	for(auto c : m_contexts) {
		delete (c);
	}
	mono_jit_cleanup(g_jitDomain);
}

ManagedScriptContext* ManagedScriptSystem::CreateContext(const char* image)
{
	ManagedScriptContext* ctx = new ManagedScriptContext(image);

	if(!ctx->Init())
	{
		delete ctx;
		return nullptr;
	}

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

void ManagedScriptSystem::PushProfilingContext()
{
	m_profilingData.push(ManagedProfilingData_t());
	m_curFrame = &m_profilingData.top();
}

void ManagedScriptSystem::PopProfilingContext()
{
	/* There should always be at least one frame in the stack */
	if(m_profilingData.size() > 1)
	{
		m_profilingData.pop();
		m_curFrame = &m_profilingData.top();
	}
}

void ManagedScriptSystem::SetProfilingSettings(ManagedProfilingSettings_t settings)
{
	m_profilingSettings = settings;
	if(m_profilingSettings.profileAllocations) {
		mono_profiler_enable_allocations();
	}
}

static void Profiler_RuntimeInit(MonoProfiler* prof)
{
}

static void Profiler_RuntimeShutdownStart(MonoProfiler* prof)
{
	auto& ctx = prof->scriptsys->CurrentProfilingData();

}

static void Profiler_RuntimeShutdownEnd(MonoProfiler* prof)
{
	auto& ctx = prof->scriptsys->CurrentProfilingData();

}

static void Profiler_ContextLoaded(MonoProfiler* prof, MonoAppContext* _ctx)
{
	auto& ctx = prof->scriptsys->CurrentProfilingData();
	ctx.totalContextLoads++;
}

static void Profiler_ContextUnloaded(MonoProfiler* prof, MonoAppContext* _ctx)
{
	auto& ctx = prof->scriptsys->CurrentProfilingData();
	ctx.totalContextUnloads++;
}

static void Profiler_GCEvent(MonoProfiler* prof, MonoProfilerGCEvent ev, uint32_t gen, mono_bool isSerial)
{
	auto& ctx = prof->scriptsys->CurrentProfilingData();

}

static void Profiler_GCAlloc(MonoProfiler* prof, MonoObject* obj)
{
	auto& ctx = prof->scriptsys->CurrentProfilingData();
	ctx.bytesAlloc += mono_object_get_size(obj);
	ctx.totalAllocs++;
}

static void Profiler_GCResize(MonoProfiler* prof, uintptr_t size)
{
	auto& ctx = prof->scriptsys->CurrentProfilingData();
	ctx.totalMoves++;
	ctx.bytesMoved += size;
}


