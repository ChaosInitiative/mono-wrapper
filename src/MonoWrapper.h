#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <unordered_map>
#include <list>

template<class T>
class ManagedBase;

template<class T>
class ManagedHandle
{
private:
	T* m_object;
	bool m_valid;

protected:
	void Invalidate() { m_valid = false; }
	void Validate() { m_valid = true; }
	friend T;
	friend ManagedBase<T>;

public:
	ManagedHandle(T* obj) :
		m_object(obj),
		m_valid(false)
	{
		m_object->AttachHandle(this);
	}

	~ManagedHandle()
	{
		if(m_object && m_valid)
			m_object->DetachHandle(this);
	}

	ManagedHandle() = delete;

	bool Valid() const { return m_valid; }

	T& operator*() { return *m_object; };

	T& operator->() { return *m_object; }
};

//==============================================================================================//
// ManagedBase
//      base class for all Managed types
//==============================================================================================//
template<class T>
class ManagedBase
{
protected:
	friend ManagedHandle<T>;
protected:
	typedef ManagedHandle<T>* HandleT;

	HandleT m_handle;
	bool m_valid;

	ManagedBase() :
		m_handle(nullptr),
		m_valid(false)
	{

	}

	void AttachHandle(HandleT handle)
	{
		handle->Validate();
		m_handle = handle;
		if(m_valid)
			m_handle->Validate();
		else
			m_handle->Invalidate();
	}

	void DetachHandle(HandleT handle)
	{
		handle->Invalidate();
		m_handle = nullptr;
	}

	virtual void InvalidateHandle()
	{
		if(m_handle)
			m_handle->Invalidate();
		m_valid = false;
	}

	virtual void ValidateHandle()
	{
		if(m_handle)
			m_handle->Validate();
		m_valid = true;
	}

};

//==============================================================================================//
// ManagedAssembly
//      Represents an Assembly object
//==============================================================================================//
class ManagedAssembly : public ManagedBase<ManagedAssembly>
{
private:
	typedef ManagedHandle<ManagedAssembly>* HandleT;
private:
	MonoAssembly* m_assembly;
	MonoImage* m_image;
	std::string m_path;
	std::unordered_multimap<std::string, class ManagedClass*> m_classes;
	bool m_populated;
	class ManagedScriptContext* m_ctx;

public:
	ManagedAssembly() = delete;
	ManagedAssembly(ManagedAssembly&) = delete;
	ManagedAssembly(ManagedAssembly&&) = delete;

protected:
	explicit ManagedAssembly(class ManagedScriptContext* ctx, const std::string& path, MonoImage* img, MonoAssembly* ass);

	friend class ManagedScriptContext;
	friend class ManagedClass;
	friend class ManagedMethod;

	void PopulateReflectionInfo();
	void DisposeReflectionInfo();

public:
	void GetReferencedTypes(std::vector<std::string>& refList);

	bool ValidateAgainstWhitelist(const std::vector<std::string>& whiteList);

	/* Invalidates all internal data and unloads the assembly */
	/* Delete the object after this */
	void Unload();
	void InvalidateHandle() override;
};


//==============================================================================================//
// ManagedType
//      Represents a simple mono type
//==============================================================================================//
class ManagedType : public ManagedBase<ManagedType>
{
private:
	MonoType* m_type;
	bool m_isStruct : 1;
	bool m_isVoid : 1;
	bool m_isRef : 1;
	bool m_isPtr : 1;
	std::string m_name;
public:
	ManagedType() = delete;
	ManagedType(ManagedType&) = delete;
	ManagedType(ManagedType&&) = delete;

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


//==============================================================================================//
// ManagedObject
//      Wrapper around a mono object
//==============================================================================================//
class ManagedObject : public ManagedBase<ManagedObject>
{
private:
	MonoObject* m_obj;
	class ManagedClass* m_class;

	friend class ManagedClass;
	friend class ManagedMethod;

public:
	ManagedObject() = delete;
	ManagedObject(ManagedObject&) = delete;
	ManagedObject(ManagedObject&&) = delete;

	ManagedObject(MonoObject* obj, class ManagedClass* cls)
	{
		m_obj = obj;
		m_class = cls;
	}

	[[nodiscard]] const ManagedClass& Class() const { return *m_class; }

	[[nodiscard]] const MonoObject* InternalObject() const { return m_obj; }

};

//==============================================================================================//
// ManagedMethod
//      Represents a MonoMethod object, must be a part of a class
//==============================================================================================//
class ManagedMethod : public ManagedBase<ManagedMethod>
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

	friend class ManagedClass;
	friend ManagedHandle<ManagedMethod>;

public:
	ManagedMethod() = delete;
	ManagedMethod(ManagedMethod&) = delete;
	ManagedMethod(ManagedMethod&&) = delete;

protected:
	explicit ManagedMethod(MonoMethod* method, ManagedClass* cls);

	~ManagedMethod();

	friend class ExecutionContext;
	friend class ManagedClass;
	friend class ManagedObject;

	void InvalidateHandle() override;

public:
	[[nodiscard]] ManagedAssembly* Assembly() const;

	[[nodiscard]] ManagedClass* Class() const;

	[[nodiscard]] const std::vector<ManagedObject*>& Attributes() const { return m_attributes; }

	[[nodiscard]] const std::string& Name() const { return m_name; };

	[[nodiscard]] int ParamCount() const { return m_paramCount; };

	MonoMethod* RawMethod() { return m_method; };
};

//==============================================================================================//
// ManagedField
//      Represents a MonoField, or a field in a class
//==============================================================================================//
class ManagedField : public ManagedBase<ManagedField>
{
private:
	MonoClassField* m_field;
	class ManagedClass* m_class;

public:
	ManagedField() = delete;
	ManagedField(ManagedField&) = delete;
	ManagedField(ManagedField&&) = delete;

protected:

	explicit ManagedField(MonoClassField* fld, class ManagedClass* cls);
	~ManagedField();

	friend class ManagedClass;
	friend class ManagedProperty;
};

//==============================================================================================//
// ManagedProperty
//      Represents a MonoProperty
//==============================================================================================//
class ManagedProperty : public ManagedBase<ManagedProperty>
{
private:
	MonoProperty* m_property;
	class ManagedClass* m_class;

public:
	ManagedProperty() = delete;
	ManagedProperty(ManagedProperty&) = delete;
	ManagedProperty(ManagedProperty&&) = delete;
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

//==============================================================================================//
// ManagedClass
//      Represents a MonoClass object and stores cached info about it
//==============================================================================================//
class ManagedClass : public ManagedBase<ManagedClass>
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
	friend class ManagedAssembly;

protected:
	ManagedClass(ManagedAssembly* assembly, const std::string& ns, const std::string& cls);
	ManagedClass(ManagedAssembly* assembly, MonoClass* _cls, const std::string& ns, const std::string& cls);
	~ManagedClass();

	void PopulateReflectionInfo();

	void InvalidateHandle() override;

public:
	ManagedClass() = delete;
	ManagedClass(ManagedClass&& c) = delete;
	ManagedClass(ManagedClass&) = delete;

	[[nodiscard]] std::string_view NamespaceName() const { return m_namespaceName; };

	[[nodiscard]] std::string_view ClassName() const { return m_className; };

	[[nodiscard]] const std::vector<class ManagedMethod*> Methods() const { return m_methods; };

	[[nodiscard]] const std::vector<class ManagedField*> Fields() const { return m_fields; };

	[[nodiscard]] const std::vector<class ManagedObject*> Attributes() const { return m_attributes; };

	[[nodiscard]] const std::vector<class ManagedProperty*> Properties() const { return m_properties; };

	ManagedMethod* FindMethod(const std::string& name);

};

/* NOTE: this class cannot have a handle pointed at it */
//==============================================================================================//
// ManagedScriptContext
//      Handles execution of a "script"
//==============================================================================================//
class ManagedScriptContext
{
private:

	std::list<ManagedAssembly*> m_loadedAssemblies;
	MonoDomain* m_domain;
	std::string m_baseImage;

public:
	ManagedScriptContext() = delete;
	ManagedScriptContext(ManagedScriptContext&) = delete;
	ManagedScriptContext(ManagedScriptContext&&) = delete;

	friend class ManagedCompiler;

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

	bool ValidateAgainstWhitelist(const std::vector<std::string> &whitelist);
};

//==============================================================================================//
// ManagedScriptSystem
//      Handles execution of a "script"
//==============================================================================================//
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

	[[nodiscard]] uint64_t HeapSize() const;

	[[nodiscard]] uint64_t UsedHeapSize() const;

	class ManagedCompiler* CreateCompiler(const std::string& pathToCompilerBinary);
	void DestoryCompiler(ManagedCompiler* c);
};


//==============================================================================================//
// ManagedCompiler
//      Handles the compilation of new code
//==============================================================================================//
class ManagedCompiler
{
private:
	ManagedScriptContext* m_ctx;
	ManagedScriptSystem* m_sys;
	ManagedClass* m_compilerClass;
	ManagedMethod* m_compileMethod;
	friend ManagedScriptSystem;
protected:
	ManagedCompiler();
	~ManagedCompiler();
	void Setup();
public:
	bool Compile(const std::string& buildDir, const std::string& outDir, int langVer);

};