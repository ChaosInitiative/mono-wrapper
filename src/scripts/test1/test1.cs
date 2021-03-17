using System;

namespace WrapperTests
{
	public class TestClass
	{
		public string value;
		public bool boolean;
		public int integer;
	}
	
	public class WrapperTestClass
	{
		public WrapperTestClass()
		{
			Console.WriteLine("WrapperTestClass Constructor Called");
		}

		public static bool Test1()
		{
			Console.WriteLine("Test1 method called");
			return true;
		}

		public bool Test2()
		{
			Console.WriteLine("Test2 method called");
			return true;
		}
		
		public TestClass NonTrivialTypeTest(string value, bool b, int i)
		{
			TestClass c = new TestClass();
			c.value = value;
			c.boolean = b;
			c.integer = i;
			return c;
		}
		
		public void ExeptionTest()
		{
			throw new Exception("AAAAAAAAAAAAAA");
		}
	}
}