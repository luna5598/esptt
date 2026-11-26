#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_http_server.h"
#include "runtime.h"
#include "slot_observation.h"
#include "agent_network.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

esp_err_t agent_web_ask(httpd_req_t *req);

extern const char page_start[] asm("_binary_index_html_start");
static const char *const slot_names[] = {"左手","右手","左脚","右脚"};
static const char *const phases[] = {"idle","running","waiting","done"};

static esp_err_t index_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, page_start);
}

static esp_err_t send_state(httpd_req_t *req)
{
    device_snapshot s;
    device_snapshot_read(&s);
    char part[320];
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    snprintf(part,sizeof(part),"{\"boot\":\"%016" PRIx64 "\",\"bus_ready\":%s,\"agent_enabled\":%s,\"slots\":[",
             s.boot,s.bus_ready?"true":"false",agent_network_configured()?"true":"false");
    if (httpd_resp_sendstr_chunk(req,part)!=ESP_OK) return ESP_FAIL;
    for (unsigned i=0;i<MO_SLOT_COUNT;i++) {
        const mo_slot_view *v=&s.slots[i];
        bool fresh=mo_sample_fresh(v, s.now);
        char values[96]="null,\"humidity_centi\":null";
        if (v->has_sample) snprintf(values,sizeof(values),"%" PRId32 ",\"humidity_centi\":%" PRIu32,
                                    v->temp_centi,v->humidity_centi);
        snprintf(part,sizeof(part),
                 "%s{\"name\":\"%s\",\"generation\":%" PRIu32 ",\"status\":\"%s\","
                 "\"present\":%s,\"fresh\":%s,\"temp_centi\":%s}",
                 i?",":"",slot_names[i],v->generation,mo_link_name(s.status[i]),
                 v->present?"true":"false",fresh?"true":"false",values);
        if (httpd_resp_sendstr_chunk(req,part)!=ESP_OK) return ESP_FAIL;
    }
    const mo_course_view *c=&s.course;
    snprintf(part,sizeof(part),"],\"course\":{\"run\":%" PRIu32 ",\"phase\":\"%s\","
             "\"slot\":%" PRIu32 ",\"goal\":%u,\"samples\":[",c->run_id,phases[c->phase],c->slot,MO_GOAL);
    if (httpd_resp_sendstr_chunk(req,part)!=ESP_OK) return ESP_FAIL;
    for (uint32_t i=0;i<c->count;i++) {
        snprintf(part,sizeof(part),"%s{\"ms\":%" PRIu64 ",\"temp_centi\":%" PRId32 "}",
                 i?",":"",c->time_ms[i],c->temp_centi[i]);
        if (httpd_resp_sendstr_chunk(req,part)!=ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(req,"]}}")!=ESP_OK) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(req,NULL);
}

static bool number(const char *query,const char *key,unsigned base,uint64_t limit,uint64_t *out)
{
    char value[24];
    if (httpd_query_key_value(query,key,value,sizeof(value))!=ESP_OK || !value[0]) return false;
    if (base==16 && strlen(value)!=16) return false;
    for (const char *p=value;*p;p++) {
        if (base==16 ? !isxdigit((unsigned char)*p) : !isdigit((unsigned char)*p)) return false;
    }
    errno=0;
    unsigned long long n=strtoull(value,NULL,base);
    if (errno==ERANGE || n>limit) return false;
    *out=n;
    return true;
}

static esp_err_t course_action(httpd_req_t *req)
{
    char query[112], header[5];
    uint64_t boot,slot,generation,run;
    /* Same-origin UI sends a custom header. Cross-origin forms cannot send it. */
    if (httpd_req_get_hdr_value_str(req,"X-MOBan-UI",header,sizeof(header))!=ESP_OK ||
        strcmp(header,"1")!=0) return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"UI header required");
    if (httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
        !number(query,"boot",16,UINT64_MAX,&boot) || !number(query,"slot",10,MO_SLOT_COUNT-1,&slot) ||
        !number(query,"gen",10,UINT32_MAX,&generation) || !number(query,"run",10,UINT32_MAX,&run))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid target");
    int rc=device_course_action(req->user_ctx!=NULL,boot,(unsigned)slot,(uint32_t)generation,(uint32_t)run);
    httpd_resp_set_type(req,"application/json");
    if (rc==MO_OK) return httpd_resp_sendstr(req,"{\"ok\":true}");
    char body[36];
    snprintf(body,sizeof(body),"{\"error\":%d}",rc);
    httpd_resp_set_status(req,"409 Conflict");
    return httpd_resp_sendstr(req,body);
}

esp_err_t web_start(void)
{
    static const char resume_tag[]="resume";
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_page},
        {.uri="/api/state", .method=HTTP_GET, .handler=send_state},
        {.uri="/api/ask", .method=HTTP_POST, .handler=agent_web_ask},
        {.uri="/api/start", .method=HTTP_POST, .handler=course_action},
        {.uri="/api/resume", .method=HTTP_POST, .handler=course_action, .user_ctx=(void*)resume_tag}
    };
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    static struct ifreq ap_interface;
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap || esp_netif_get_netif_impl_name(ap, ap_interface.ifr_name) != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    cfg.if_name = &ap_interface;
    cfg.stack_size=6144;
    cfg.lru_purge_enable=true;
    httpd_handle_t server;
    esp_err_t e=httpd_start(&server,&cfg);
    if(e!=ESP_OK) return e;
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++){
        e=httpd_register_uri_handler(server,&routes[i]);
        if(e!=ESP_OK){httpd_stop(server);return e;}
    }
    return ESP_OK;
}
