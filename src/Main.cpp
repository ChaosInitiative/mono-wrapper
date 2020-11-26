
/* Mono includes */
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/class.h>
#include <mono/metadata/reflection.h>
#include <mono/metadata/mono-debug.h>

#include "MonoWrapper.h"

#include <string>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <list>

#include <unistd.h>

std::string gLibraryName;

struct ClassDescription
{
	MonoClass* klass;
	std::vector<MonoMethod*> methods;
	std::vector<MonoClassField*> fields;
	std::vector<MonoProperty*> properties;
	std::vector<MonoObject*> attributes;
	std::string name;
	std::string ns; // namespace
};

struct MethodDescription
{
	ClassDescription* klass;
	std::vector<MonoObject*> attributes;
};

struct FieldDescription
{
	MonoClassField* field;
	std::vector<MonoObject*> attributes;
};


class ExecutionContext
{
private:
	MonoDomain* m_domain;
	std::string m_file;
	bool m_good;
	friend class MonoScriptSystem;

	struct Assembly_t
	{
		std::string file;
		MonoAssembly* assembly;
		MonoImage* image;
	};

	std::vector<Assembly_t> m_assemblies;

	// Classes NON FULLY QUALIFIED NAME is first param
	std::unordered_multimap<std::string, ClassDescription> m_classes;

	ExecutionContext() {};
	ExecutionContext(const char* file) :
		m_file(file),
		m_good(false)
	{
		m_domain = mono_jit_init(file);
		if(!m_domain) {
			return;
		}
		LoadAssembly(file);
		m_good = true;
	}

	~ExecutionContext()
	{
		m_good = false;
		mono_jit_cleanup(m_domain);
		m_domain = nullptr;
	}


	void PopulateClassInfo(MonoClass* klass, const char* cls, const char* ns)
	{
		ClassDescription desc;
		desc.ns = ns;
		desc.name = cls;
		void* iter = nullptr;
		MonoMethod* method;
		while((method = mono_class_get_methods(klass, &iter)))
			desc.methods.push_back(method);

		MonoClassField* field;
		iter = nullptr;
		while((field = mono_class_get_fields(klass, &iter)))
			desc.fields.push_back(field);

		MonoProperty* props;
		iter = nullptr;
		while((props = mono_class_get_properties(klass, &iter)))
			desc.properties.push_back(props);

		desc.properties.shrink_to_fit();
		desc.methods.shrink_to_fit();
		desc.fields.shrink_to_fit();
		m_classes.insert({desc.name, desc});
	}

public:

	bool LoadAssembly(const char* file)
	{
		MonoAssembly * ass = mono_domain_assembly_open(m_domain, file);
		if(!ass) {
			return false;
		}
		MonoImage* img = mono_assembly_get_image(ass);
		if(!ass) {
			mono_assembly_close(ass);
			return false;
		}
		m_assemblies.push_back({file, ass, img});
		return true;
	}

	bool UnloadAssembly(const char* file)
	{
		for(auto x = m_assemblies.begin(); x != m_assemblies.end(); x++)
		{
			if(strcmp(file, x->file.c_str()) == 0)
			{
				mono_image_close(x->image);
				mono_assembly_close(x->assembly);
				m_assemblies.erase(x);
				return true;
			}
		}
		return false;
	}

	void PopulateReflectionInfo()
	{
		for(auto x : m_assemblies)
		{
			/* TRY to list classes */
			const MonoTableInfo *tabInfo = mono_image_get_table_info(x.image, MONO_TABLE_TYPEDEF);
			int rows = mono_table_info_get_rows(tabInfo);
			for (int i = 0; i < rows; i++)
			{
				unsigned int cols[MONO_TYPEDEF_SIZE];
				mono_metadata_decode_row(tabInfo, i, cols, MONO_TYPEDEF_SIZE);
				const char* _namespace = mono_metadata_string_heap(x.image, cols[MONO_TYPEDEF_NAMESPACE]);
				const char* _class = mono_metadata_string_heap(x.image, cols[MONO_TYPEDEF_NAME]);
				MonoClass* cls = mono_class_from_name(x.image, _namespace, _class);
				if(!cls) {
					printf("Class lookup for %s.%s failed.\n", _namespace, _class);
					continue;
				}
				PopulateClassInfo(cls, _class, _namespace);
			}
		}
	}

	ClassDescription FindClass(const char* ns, const char* name)
	{
		auto it = m_classes.equal_range(name);
		for(auto _it = it.first; _it != it.second; ++_it)
		{
			if(strcmp(ns, _it->second.ns.c_str()) == 0)
				return _it->second;
		}
		return ClassDescription();
	}

	MonoMethod* FindMethod(const ClassDescription& desc, const char* m)
	{
		for(auto x : desc.methods)
		{
			if(strcmp(m, mono_method_get_name(x)) == 0)
				return x;
		}
		return nullptr;
	}

	std::vector<MonoMethod*> FindMethodsByPredicate(const ClassDescription& desc, bool(*pred)(ExecutionContext* ctx, MonoMethod* method))
	{
		std::vector<MonoMethod*> methods;
		if(!pred) return methods;
		for(auto x : desc.methods)
		{
			if(pred(this, x))
				methods.push_back(x);
		}
		return methods;
	}

	MonoObject* MethodGetAttribute(MonoMethod* method, const ClassDescription& cls)
	{
		if(!method || !cls.klass) return nullptr;
		MonoCustomAttrInfo* attr = mono_custom_attrs_from_method(method);

		if(!attr || !cls.klass) return nullptr;

		MonoObject* obj = mono_custom_attrs_get_attr(attr, cls.klass);
		mono_custom_attrs_free(attr);
		return obj;
	}



	template<class R, class P1>
	void InvokeStaticMethod(const ClassDescription& desc, const char* name, R& outRet, P1 p)
	{
		MonoMethod* meth = FindMethod(desc, name);
		if(!meth) { return; }
		void* args[] = {
			&p
		};
		mono_runtime_invoke(meth, NULL, args, NULL);
	}

	template<class R, class P1, class P2>
	void InvokeStaticMethod(const ClassDescription& desc, const char* name, R& outRet, P1 p, P2 p2)
	{
		MonoMethod* meth = FindMethod(desc, name);
		if(!meth) { return; }
		void* args[] = {
			&p,
			&p2
		};
		mono_runtime_invoke(meth, NULL, args, NULL);
	}


	template<class R, class P1>
	void InvokeStaticMethod(MonoMethod* meth, R& outRet, P1 p)
	{
		if(!meth) { return; }
		void* args[] = {
			&p
		};
		mono_runtime_invoke(meth, NULL, args, NULL);
	}

	template<class R, class P1, class P2>
	void InvokeStaticMethod(MonoMethod* meth, R& outRet, P1 p, P2 p2)
	{
		if(!meth) { return; }
		void* args[] = {
			&p,
			&p2
		};
		mono_runtime_invoke(meth, NULL, args, NULL);
	}

};

class MonoScriptSystem
{
private:

public:
	MonoScriptSystem()
	{
		mono_config_parse("mono-config");
	}

	ExecutionContext* CreateExecutionContext(const char* assembly)
	{
		return new ExecutionContext(assembly);
	}

	void DestroyExecutionContext(ExecutionContext* ctx)
	{
		delete ctx;
	}


};

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		printf("Please pass a library name to load!\n");
		exit(1);
	}
	gLibraryName = argv[1];

	static ManagedScriptSystem scriptSystem;

	ManagedScriptContext* ctx = scriptSystem.CreateContext(gLibraryName.c_str());

	ManagedClass* cls = ctx->FindClass("ScriptSystem", "Script");

	if(!cls) { 
		printf("Unable to find class!\n");
	}

}