#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <unordered_map>
#include <list>

class ManagedAssembly
{
private:
	MonoAssembly* m_assembly;
	MonoImage* m_image;
	std::string m_path;
	std::unordered_multimap<std::string, class ManagedClass*> m_classes;
	bool m_populated;
	class ManagedScriptContext* m_ctx;

public:
	ManagedAssembly() = delete;

protected:
	explicit ManagedAssembly(class ManagedScriptContext* ctx, const std::string& path, MonoImage* img, MonoAssembly* ass);

	friend class ManagedScriptContext;
	friend class ManagedClass;
	friend class ManagedMethod;

	void PopulateReflectionInfo();

public:
	void GetReferencedTypes(std::vector<std::string>& refList);

	bool ValidateAgainstWhitelist(const std::vector<std::string>& whiteList);
};

class ManagedType
{
private:
	MonoType* m_type;
	bool m_isStruct : 1;
	bool m_isVoid : 1;
	bool m_isRef : 1;
	bool m_isPtr : 1;
	std::string m_name;
protected:
	ManagedType(MonoType* type);

	friend class ManagedMethod;
	friend class ManagedObject;
public:
	bool IsStruct() const { return m_isStruct; };

	bool IsVoid() const { return m_isVoid; };

	bool IsRef() const { return m_isRef; };

	bool IsPtr() const { return m_isPtr; };
	
	bool Equals(const ManagedType* other) const;

	[[nodiscard]] const std::string& Name() const; 
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
	MonoMethodSignature* m_signature;
	bool m_populated;
	uint32_t m_token;
	std::string m_name;
	std::string m_fullyQualifiedName;
	int m_paramCount;

	ManagedType* m_returnType;
	std::vector<ManagedType*> m_params;

public:
	ManagedMethod() = delete;

protected:


	explicit ManagedMethod(MonoMethod* method, ManagedClass* cls);

	~ManagedMethod();

	friend class ExecutionContext;
	friend class ManagedClass;
	friend class ManagedObject;


public:
	[[nodiscard]] ManagedAssembly* Assembly() const;

	[[nodiscard]] ManagedClass* Class() const;

	[[nodiscard]] const std::vector<ManagedObject*>& Attributes() const { return m_attributes; }

	[[nodiscard]] const std::string& Name() const { return m_name; };

	[[nodiscard]] int ParamCount() const { return m_paramCount; };
};

class ManagedField
{
private:
	MonoClassField* m_field;
	class ManagedClass* m_class;
protected:

	explicit ManagedField(MonoClassField* fld, class ManagedClass* cls);
	~ManagedField();

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
	bool m_populated;
	ManagedAssembly* m_assembly;

	friend class ManagedScriptContext;
	friend class ManagedMethod;

protected:

	ManagedClass(ManagedAssembly* assembly, const std::string& ns, const std::string& cls);
	ManagedClass(ManagedAssembly* assembly, MonoClass* _cls, const std::string& ns, const std::string& cls);
	~ManagedClass();

	void PopulateReflectionInfo();

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

	std::list<ManagedAssembly*> m_loadedAssemblies;
	MonoDomain* m_domain;
	std::string m_baseImage;

public:
	ManagedScriptContext() = delete;

protected:
	friend class ManagedScriptSystem;


	explicit ManagedScriptContext(const std::string& baseImage);
	~ManagedScriptContext();

	void PopulateReflectionInfo();

public:

	bool LoadAssembly(const char* path);

	bool UnloadAssembly(const std::string& name);

	/* Performs a class search in all loaded assemblies */
	/* If you have the assembly name, please use the alternative version of this function */
	ManagedClass* FindClass(const std::string& ns, const std::string& cls);

	ManagedClass* FindClass(ManagedAssembly* assembly, const std::string& ns, const std::string& cls);

	ManagedAssembly* FindAssembly(const std::string &path);

	/* Clears all reflection info stored in each assembly description */
	/* WARNING: this will invalidate your handles! */
	void ClearReflectionInfo();

	bool ValidateAgainstWhitelist(const std::vector<std::string> whitelist);
};

class ManagedScriptSystem
{
private:
	std::vector<ManagedScriptContext*> m_contexts;
public:
	ManagedScriptSystem();
	~ManagedScriptSystem();

	ManagedScriptContext* CreateContext(const char* image);

	void DestroyContext(ManagedScriptContext* ctx);

	[[nodiscard]] int NumActiveContexts() const { return m_contexts.size(); };


};