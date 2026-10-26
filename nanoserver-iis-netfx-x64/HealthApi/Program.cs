using System.Web;

namespace HealthApi;

public sealed class HealthHandler : IHttpHandler
{
    public bool IsReusable => true;

    public void ProcessRequest(HttpContext context)
    {
        context.Response.StatusCode = 200;
        context.Response.ContentType = "application/json; charset=utf-8";
        context.Response.Write("{\"status\":\"Healthy\"}");
    }
}
