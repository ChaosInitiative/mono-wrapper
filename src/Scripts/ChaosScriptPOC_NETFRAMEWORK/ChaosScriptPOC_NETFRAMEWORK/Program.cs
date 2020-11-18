using System;
using ChaosScript;

namespace ChaosScriptPOC_NETFRAMEWORK
{
    internal class Program
    {
        public static void Main(string[] args)
        {
            Console.WriteLine("Hello, world!");
        }
    }

    class AnotherTest
    {
        public static bool Test(int i)
        {
            Console.WriteLine("Another test, seems to be working!");
            return true;
        }

        [Event("TestEvent")]
        public static void MyEvent(int x, bool b)
        {
            Console.WriteLine("Method invoked with " + x + " and " + b);
        }
    }
}