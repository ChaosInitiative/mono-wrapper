/* Mono includes */
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/class.h>
#include <mono/metadata/reflection.h>
#include <mono/metadata/mono-debug.h>

#include <string>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>

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

class ManagedObject
{
private:
	MonoObject* m_obj;
	class ManagedClass* m_class;

public:

	ManagedObject(MonoObject* obj, class ManagedClass* cls)
	{
		m_obj = obj;
		m_class = cls;
	}


	[[nodiscard]] const ManagedClass& Class() const { return *m_class; }

	[[nodiscard]] const MonoObject* InternalObject() const { return m_obj; }

};

class ManagedMethod
{
private:
	MonoMethod* m_method;
	class ManagedClass* m_class;
	std::vector<class ManagedObject*> m_attributes;
	MonoCustomAttrInfo* m_attrInfo;
	bool m_populated;

public:
	ManagedMethod() = delete;

protected:


	explicit ManagedMethod(MonoMethod* method, ManagedClass* cls = nullptr) :
		m_populated(false)
	{
		if(!method) return;
		m_method = method;
		m_attrInfo = mono_custom_attrs_from_method(method);
		m_class = cls;
		if(!m_attrInfo) {
			return;
		}

	}

	~ManagedMethod()
	{
		if(m_attrInfo)
			mono_custom_attrs_free(m_attrInfo);
	}

	friend class ExecutionContext;
	friend class ManagedClass;
	friend class ManagedObject;

	void PopulateReflectionInfo()
	{

	}




};

class ManagedField
{
private:
	MonoClassField* m_field;
	class ManagedClass* m_class;
protected:

	explicit ManagedField(MonoClassField* fld, class ManagedClass* cls) :
		m_class(cls),
		m_field(fld)
	{

	}

	friend class ManagedClass;
	friend class ManagedProperty;
};

class ManagedProperty
{
private:
	MonoProperty* m_property;
	class ManagedClass* m_class;

protected:

	ManagedProperty(MonoProperty* prop, ManagedClass* cls) :
		m_class(cls),
		m_property(prop)
	{

	}

	friend class ManagedClass;
	friend class ManagedMethod;
	friend class ManagedObject;

public:
	[[nodiscard]] const MonoProperty* InternalProperty() const { return m_property; };

	[[nodiscard]] const ManagedClass& Class() const { return *m_class; }
};

class ManagedClass
{
private:
	std::vector<class ManagedMethod*> m_methods;
	std::vector<class ManagedField*> m_fields;
	std::vector<class ManagedObject*> m_attributes;
	MonoCustomAttrInfo* m_attrInfo;
	std::vector<class ManagedProperty*> m_properties;
	std::string m_namespaceName;
	std::string m_className;
	MonoClass* m_class;
	MonoImage* m_img;
	bool m_populated;

protected:

	ManagedClass(MonoImage* img, const std::string& ns, const std::string& cls) :
		m_img(img),
		m_className(cls),
		m_namespaceName(ns),
		m_populated(false)
	{
		m_class = mono_class_from_name(img, ns.c_str(), cls.c_str());
		if(!m_class) {
			return;
		}
		m_attrInfo = mono_custom_attrs_from_class(m_class);

		/* If there is no class name or namespace, something is fucky */
		if(!cls.empty()) {
			m_attributes.push_back(new ManagedObject(mono_custom_attrs_get_attr(m_attrInfo, m_class), this));
		}

		PopulateReflectionInfo();
	}

	~ManagedClass()
	{
		if(m_attrInfo)
			mono_custom_attrs_free(m_attrInfo);

	}

	void PopulateReflectionInfo()
	{
		if(m_populated) return;
		void* iter = nullptr;
		MonoMethod* method;
		while((method = mono_class_get_methods(m_class, &iter))) {
			m_methods.push_back(new ManagedMethod(method, this));
		}

		MonoClassField* field;
		iter = nullptr;
		while((field = mono_class_get_fields(m_class, &iter))) {
			m_fields.push_back(new ManagedField(field, this));
		}

		MonoProperty* props;
		iter = nullptr;
		while((props = mono_class_get_properties(m_class, &iter))) {
			m_properties.push_back(new ManagedProperty(props, this));
		}

		m_populated = true;
	}

public:
	ManagedClass() = delete;

	[[nodiscard]] std::string_view NamespaceName() const { return m_namespaceName; };

	[[nodiscard]] std::string_view ClassName() const { return m_className; };

	[[nodiscard]] const std::vector<class ManagedMethod*> Methods() const { return m_methods; };

	[[nodiscard]] const std::vector<class ManagedField*> Fields() const { return m_fields; };

	[[nodiscard]] const std::vector<class ManagedObject*> Attributes() const { return m_attributes; };

	[[nodiscard]] const std::vector<class ManagedProperty*> Properties() const { return m_properties; };

};

class ManagedScriptContext
{
private:

	struct Assembly_t
	{
		MonoAssembly* m_assembly;
		MonoImage* m_image;
		std::string m_path;
	};

	std::vector<Assembly_t> m_loadedAssemblies;
	MonoDomain* m_domain;
	std::string m_baseImage;

public:
	ManagedScriptContext() = delete;

protected:
	friend class ManagedScriptSystem;


	explicit ManagedScriptContext(const std::string& baseImage) :
		m_baseImage(baseImage)
	{
		m_domain = mono_jit_init(baseImage.c_str());
	}

	~ManagedScriptContext()
	{
		if(m_domain)
			mono_domain_unload(m_domain);
	}

	bool LoadAssembly(const char* path)
	{
		if(!m_domain) return false;
		MonoAssembly* ass = mono_domain_assembly_open(m_domain, path);
		if(!ass) return false;

		MonoImage* img = mono_assembly_get_image(ass);
		if(!img) {
			return false;
		}
		Assembly_t assemblyDesc;
		assemblyDesc.m_assembly = ass;
		assemblyDesc.m_image = img;
		assemblyDesc.m_path = path;
		m_loadedAssemblies.push_back(assemblyDesc);
		return true;
	}

	bool UnloadAssembly(const std::string& name)
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


};

class ManagedScriptSystem
{
private:

public:
	ManagedScriptSystem(){ };


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

	static MonoScriptSystem scriptSystem;

	ExecutionContext* ctx = scriptSystem.CreateExecutionContext(argv[1]);

	ctx->PopulateReflectionInfo();

	/* Attributes */
	ClassDescription eventAttr = ctx->FindClass("ChaosScript", "Event");

	/* Hook up the events */
	ClassDescription desc = ctx->FindClass("ChaosScriptPOC_NETFRAMEWORK", "AnotherTest");
	std::vector<MonoMethod*> events = ctx->FindMethodsByPredicate(desc, [](ExecutionContext* ctx, MonoMethod* m) -> bool {
		if(ctx->MethodGetAttribute(m, ctx->FindClass("ChaosScript", "Event")))
			return true;
		return false;
	});

	for(auto x : events)
	{
		if(!x) printf("FUCK\n");
		bool outBool;
		ctx->InvokeStaticMethod(x, outBool, 10, true);
	}



#if 0
	mono_config_parse("mono-config");

	printf("Loading %s\n", gLibraryName.c_str());
	printf("JIT init\n");
	MonoDomain* domain = mono_jit_init(gLibraryName.c_str());

	if(!domain)
	{
		printf("Jit init failed\n");
		exit(1);
	}

	printf("Assembly load start\n");
	MonoAssembly* assembly = mono_domain_assembly_open(domain, gLibraryName.c_str());

	if(!assembly)
	{
		printf("Assembly load failed.\n");
		exit(1);
	}

	const char* _argv[] = {
		"A", "B"
	};
	printf("Exec attempt start\n");
	int ret = mono_jit_exec(domain, assembly, 1, &argv[1]);
	printf("Execution complete with code %u\n", ret);


	MonoImage* img = mono_assembly_get_image(assembly);
	if(!img)
	{
		printf("NOT able to get image from assembly\n");
		exit(1);
	}
	printf("Loaded assembly\n");
	MonoClass* cls = mono_class_from_name(img, "ChaosScriptPOC_NETFRAMEWORK", "AnotherTest");

	if(!cls)
	{
		printf("NOT able to find AnotherTest class\n");
		exit(1);
	}
	printf("Loaded class\n");
	MonoMethodDesc * desc = mono_method_desc_new("ChaosScriptPOC_NETFRAMEWORK.AnotherTest:Test", true);
	if(!desc)
	{
		printf("Failed to create desc.\n");
		exit(1);
	}
	printf("Created method desc\n");
	MonoMethod* ourMethod = mono_method_desc_search_in_class(desc, cls);

	if(!ourMethod)
	{
		printf("Not able to find Test!\n");
		exit(1);
	}

	printf("Trying to invoke...\n");
	int tst_num = 69;
	void* args[1] = {&tst_num};
	mono_runtime_invoke(ourMethod, NULL, args, NULL);


	printf("Domain cleanup\n");
	mono_jit_cleanup(domain);
#endif

}