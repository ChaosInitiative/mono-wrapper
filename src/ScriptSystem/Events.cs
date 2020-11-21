using System;

namespace ScriptSystem.Events
{
    [AttributeUsage(AttributeTargets.Method)]
    public class Event : System.Attribute
    {
        internal string EventName;

        Event(string ev)
        {
            EventName = ev;
        }
    }
}