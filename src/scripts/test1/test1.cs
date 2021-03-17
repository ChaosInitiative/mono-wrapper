using System;

namespace WrapperTests
{
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
	}
}