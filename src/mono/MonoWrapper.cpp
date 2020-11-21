/* Mono includes */
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/class.h>
#include <mono/metadata/reflection.h>
#include <mono/metadata/mono-debug.h>

#include "MonoWrapper.h"

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
	m_signature = mono_method_get_signature(m_method, m_class->m_assembly->m_image, m_token);
	if (!m_attrInfo)
	{
		return;
	}

}

ManagedMethod::~ManagedMethod()
{
	if (m_attrInfo)
		mono_custom_attrs_free(m_attrInfo);
}

ManagedAssembly *ManagedMethod::Assembly() const
{
	return nullptr;
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
	if (!cls.empty())
	{
		m_attributes.push_back(new ManagedObject(mono_custom_attrs_get_attr(m_attrInfo, m_class), this));
	}

	PopulateReflectionInfo();
}

ManagedClass::ManagedClass(ManagedAssembly *assembly, MonoClass *_cls, const std::string &ns, const std::string &cls) :
	m_className(cls),
	m_class(_cls),
	m_namespaceName(ns),
	m_populated(false)
{
	m_attrInfo = mono_custom_attrs_from_class(m_class);

	/* If there is no class name or namespace, something is fucky */
	if (!cls.empty())
	{
		m_attributes.push_back(new ManagedObject(mono_custom_attrs_get_attr(m_attrInfo, m_class), this));
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
	ManagedAssembly assemblyDesc;
	assemblyDesc.m_assembly = ass;
	assemblyDesc.m_image = img;
	assemblyDesc.m_path = path;
	m_loadedAssemblies.push_back(assemblyDesc);
	return true;
}

bool ManagedScriptContext::UnloadAssembly(const std::string& name)
{
	for(auto it = m_loadedAssemblies.begin(); it != m_loadedAssemblies.end(); ++it)
	{
		if(it->m_path == name)
		{
			if(it->m_image)
				mono_image_close(it->m_image);
			if(it->m_assembly)
				mono_assembly_close(it->m_assembly);
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
		if((_cls = FindClass(&a, ns, cls)))
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
		if(a.m_path == path)
		{
			return &a;
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
		for(auto& kvPair : a.m_classes)
		{
			delete kvPair.second;
		}
		a.m_classes.clear();
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