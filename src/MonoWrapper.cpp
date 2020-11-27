/* Mono includes */
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/class.h>
#include <mono/metadata/reflection.h>
#include <mono/metadata/mono-debug.h>

#include "MonoWrapper.h"

#include <assert.h>
#include <string.h>


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
		printf("Class lookup %s.%s\n",ns,c);
		m_ctx->FindClass(ns, c);
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

//================================================================//
//
// Managed Field
//
//================================================================//

ManagedField::ManagedField(MonoClassField *fld, class ManagedClass *cls) :
	m_class(cls),
	m_field(fld)
{

}

ManagedField::~ManagedField()
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
	m_populated(false)
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
	m_assembly(assembly)
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
	if (m_populated) return;
	void *iter = nullptr;

	MonoMethod *method;
	while ((method = mono_class_get_methods(m_class, &iter)))
	{
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

//================================================================//
//
// Managed Script Context
//
//================================================================//

ManagedScriptContext::ManagedScriptContext(const std::string& baseImage) :
	m_baseImage(baseImage)
{
	m_domain = mono_jit_init(baseImage.c_str());
	MonoAssembly* ass = mono_domain_assembly_open(m_domain, baseImage.c_str());
	MonoImage* img = mono_assembly_get_image(ass);
	ManagedAssembly* newass = new ManagedAssembly(this, baseImage, img, ass);
	m_loadedAssemblies.push_back(newass);
	newass->PopulateReflectionInfo();
}

ManagedScriptContext::~ManagedScriptContext()
{
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


//================================================================//
//
// Managed Script System
//
//================================================================//

ManagedScriptSystem::ManagedScriptSystem()
{

}

ManagedScriptSystem::~ManagedScriptSystem()
{

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