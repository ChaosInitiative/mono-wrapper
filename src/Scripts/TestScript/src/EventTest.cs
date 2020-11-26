using System;
using System.IO;

namespace TestScript
{
	class EventTest
	{
		[Event("MyEvent")]
		public static void MyEvent()
		{
			Console.WriteLine("Invoked");
		}
	}
}