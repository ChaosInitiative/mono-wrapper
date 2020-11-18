using System;

namespace ChaosScript
{
    [System.AttributeUsage(System.AttributeTargets.Method)]
    public class Event : System.Attribute
    {
        public string EventName;

        public Event(String ev)
        {
            EventName = ev;
        }
    }
}