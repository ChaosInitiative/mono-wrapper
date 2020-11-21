using System;
using System.CodeDom;
using System.CodeDom.Compiler;
using Microsoft.CSharp;

namespace ChaosScriptPOC_NETFRAMEWORK
{
    public class ScriptLoader
    {
        public void Compile(String code)
        {
            CSharpCodeProvider provider = new CSharpCodeProvider();
            provider.CompileAssemblyFromSource(new CompilerParameters(, ))
        }
    }
}