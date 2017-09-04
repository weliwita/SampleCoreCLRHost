using System;
using Newtonsoft.Json.Linq;
namespace SampleAppCore
{
    class Program
    {
        static void Main(string[] args)
        {
            JArray array = new JArray();
            array.Add("Manual text");
            array.Add(new DateTime(2000, 5, 23));

            JObject o = new JObject();
            o["MyArray"] = array;

            string json = o.ToString();
            Console.WriteLine(json);
        }
    }
}
