extern "C" {
    #include <ngx_config.h>
    #include <ngx_core.h>
    #include <ngx_http.h>
}
#include <uap-cpp/UaParser>
#include <iostream>


struct ngx_http_ua_parser_main_conf_t {
    uap_cpp::UserAgentParser  *parser;
};


struct ngx_http_ua_parser_loc_conf_t {
    ngx_flag_t                  enabled;

    ngx_http_complex_value_t   *source;
};


struct ngx_http_ua_parser_ctx_t {
    uap_cpp::UserAgent         *parser;
    bool                        parsed_device;
    bool                        parsed_os;
    bool                        parsed_browser;
};


static ngx_int_t ngx_http_ua_parser_add_variables(ngx_conf_t *cf);
static void *ngx_http_ua_parser_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_ua_parser_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ua_parser_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_ua_parser_regexes_file(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);


static ngx_command_t ngx_http_ua_parser_commands[] = {

    { ngx_string("ua_parser_regexes_file"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_ua_parser_regexes_file,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ua_parser"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ua_parser_loc_conf_t, enabled),
      NULL },

    { ngx_string("ua_parser_source"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ua_parser_loc_conf_t, source),
      NULL },

      ngx_null_command
};


static ngx_http_module_t ngx_http_ua_parser_module_ctx = {
    ngx_http_ua_parser_add_variables,     /* preconfiguration */
    NULL,                                 /* postconfiguration */

    ngx_http_ua_parser_create_main_conf,  /* create main configuration */
    NULL,                                 /* init main configuration */

    NULL,                                 /* create server configuration */
    NULL,                                 /* merge server configuration */

    ngx_http_ua_parser_create_loc_conf,   /* create location configuration */
    ngx_http_ua_parser_merge_loc_conf,    /* merge location configuration */
};


extern "C" {
    ngx_module_t ngx_http_ua_parser_module = {
        NGX_MODULE_V1,
        &ngx_http_ua_parser_module_ctx,   /* module context */
        ngx_http_ua_parser_commands,      /* module directives */
        NGX_HTTP_MODULE,                  /* module type */
        NULL,                             /* init master */
        NULL,                             /* init module */
        NULL,                             /* init process */
        NULL,                             /* init thread */
        NULL,                             /* exit thread */
        NULL,                             /* exit process */
        NULL,                             /* exit master */
        NGX_MODULE_V1_PADDING
    };
}


enum class ngx_http_ua_parser_data_t : uintptr_t {
    device = 1,
    os = 2,
    browser = 3,
};


enum class ngx_http_ua_parser_var_t : uintptr_t {
    device_family = 0x11,
    device_model = 0x21,
    device_brand = 0x31,
    os_family = 0x12,
    os_version_major = 0x22,
    os_version_minor = 0x32,
    os_version_patch = 0x42,
    os_version_patch_minor = 0x52,
    browser_family = 0x13,
    browser_version_major = 0x23,
    browser_version_minor = 0x33,
    browser_version_patch = 0x43,
    browser_version_patch_minor = 0x53,
};


/* schema for defining a variable */
struct ngx_http_ua_parser_var_schema_t {
    ngx_str_t                 name;
    ngx_http_ua_parser_var_t  data;
};

/* note ahead of time */
#define UAP_NUM_VARS 13


/* cleanup context */
static void ngx_http_ua_parser_ctx_cleanup(void *data) {
    auto ctx = static_cast<ngx_http_ua_parser_ctx_t *>(data);
    if (ctx->parser != NULL) {
        delete ctx->parser;
    }
}


/* parse any variable */
static ngx_int_t ngx_http_ua_parser(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    auto upmf = static_cast<ngx_http_ua_parser_main_conf_t *>(
        ngx_http_get_module_main_conf(r, ngx_http_ua_parser_module)
    );

    auto uplf = static_cast<ngx_http_ua_parser_loc_conf_t *>(
        ngx_http_get_module_loc_conf(r, ngx_http_ua_parser_module)
    );

    ngx_str_t     source;
    std::string   ua;
    std::string   value;
    auto          parser = upmf->parser;

    if (!uplf->enabled || upmf->parser == NULL) {
        /* parser disabled */
        v->not_found = 1;
        return NGX_OK;
    }

    if (uplf->source == NULL) {
        /* convert request user agent to string */
        auto ua_elt = r->headers_in.user_agent;
        ua = ua_elt->hash
            ? std::string(reinterpret_cast<char *>(ua_elt->value.data), ua_elt->value.len)
            : std::string();

    } else {

        if (ngx_http_complex_value(r, uplf->source, &source) != NGX_OK) {
            return NGX_ERROR;
        }

        if (source.len > 0) {
            ua = std::string(reinterpret_cast<char *>(source.data), source.len);

        } else {
            auto ua_elt = r->headers_in.user_agent;
            ua = ua_elt->hash 
                ? std::string(reinterpret_cast<char *>(ua_elt->value.data), ua_elt->value.len) 
                : std::string();
        }
    }

    /* only bother parsing if we have a user agent */
    if (!ua.empty()) {
        /* load parsed user agent from context */
        auto ctx = static_cast<ngx_http_ua_parser_ctx_t *>(
            ngx_http_get_module_ctx(r, ngx_http_ua_parser_module)
        );

        if (ctx == NULL) {
            auto cln = ngx_pool_cleanup_add(r->pool, sizeof(ngx_http_ua_parser_ctx_t));
            if (cln == NULL) {
                return NGX_ERROR;
            }

            cln->handler = ngx_http_ua_parser_ctx_cleanup;
            ctx = static_cast<ngx_http_ua_parser_ctx_t *>(cln->data);
            ctx->parser = new uap_cpp::UserAgent{
                uap_cpp::Device(), uap_cpp::Agent(), uap_cpp::Agent(), std::string()
            };
            ctx->parsed_device = false;
            ctx->parsed_os = false;
            ctx->parsed_browser = false;
        }

        auto parsed = ctx->parser;

        /* parse relevant part of user agent */
        switch (data & 0xF) {

        case (uintptr_t) ngx_http_ua_parser_data_t::device:
            if (!ctx->parsed_device) {
                parsed->device = parser->parse_device(ua);
                ctx->parsed_device = true;
            }
            break;

        case (uintptr_t) ngx_http_ua_parser_data_t::os:
            if (!ctx->parsed_os) {
                parsed->os = parser->parse_os(ua);
                ctx->parsed_os = true;
            }
            break;

        case (uintptr_t) ngx_http_ua_parser_data_t::browser:
            if (!ctx->parsed_browser) {
                parsed->browser = parser->parse_browser(ua);
                ctx->parsed_browser = true;
            }
            break;

        default:
            return NGX_ERROR;
        }

        /* pull out parsed variable */
        switch (data) {

        case (uintptr_t) ngx_http_ua_parser_var_t::device_family:
            value = parsed->device.family;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::device_model:
            value = parsed->device.model;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::device_brand:
            value = parsed->device.brand;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::os_family:
            value = parsed->os.family;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::os_version_major:
            value = parsed->os.major;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::os_version_minor:
            value = parsed->os.minor;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::os_version_patch:
            value = parsed->os.patch;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::os_version_patch_minor:
            value = parsed->os.patch_minor;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::browser_family:
            value = parsed->browser.family;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::browser_version_major:
            value = parsed->browser.major;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::browser_version_minor:
            value = parsed->browser.minor;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::browser_version_patch:
            value = parsed->browser.patch;
            break;

        case (uintptr_t) ngx_http_ua_parser_var_t::browser_version_patch_minor:
            value = parsed->browser.patch_minor;
            break;

        default:
            return NGX_ERROR;
        }
    }

    if (value.empty()) {
        /* mark as missing */
        v->data = NULL;
        v->len = 0;
        v->no_cacheable = 0;
        v->not_found = 1;
        v->valid = 1;

    } else {
        /* allocate string */
        unsigned char *str = reinterpret_cast<unsigned char *>(ngx_palloc(r->pool, value.size()));
        if (str == NULL) {
            return NGX_ERROR;
        }
        std::copy(value.begin(), value.end(), str);

        /* set variable */
        v->data = str;
        v->len = value.size();
        v->no_cacheable = 0;
        v->not_found = 0;
        v->valid = 1;
    }

    /* we're done */
    return NGX_OK;
}


/* shorthand for defining a variable */
/* note: we can't use ngx_string here because it's invalid C++, even though it's valid C */
#define UAP_VAR(x, y) { \
    { \
        sizeof("ua_parser_" #x "_" #y) - 1, \
        const_cast<unsigned char *>( \
            reinterpret_cast<const unsigned char *>("ua_parser_" #x "_" #y) \
        ) \
    }, \
    ngx_http_ua_parser_var_t::x ## _ ## y \
}


/* all the variables we'll have */
static ngx_http_ua_parser_var_schema_t ngx_http_ua_parser_vars[UAP_NUM_VARS] = {
    UAP_VAR(device, family),
    UAP_VAR(device, model),
    UAP_VAR(device, brand),
    UAP_VAR(os, family),
    UAP_VAR(os, version_major),
    UAP_VAR(os, version_minor),
    UAP_VAR(os, version_patch),
    UAP_VAR(os, version_patch_minor),
    UAP_VAR(browser, family),
    UAP_VAR(browser, version_major),
    UAP_VAR(browser, version_minor),
    UAP_VAR(browser, version_patch),
    UAP_VAR(browser, version_patch_minor)
};


/* add the variables to the context */
static ngx_int_t
ngx_http_ua_parser_add_variables(ngx_conf_t *cf)
{
    for (size_t i = 0; i < UAP_NUM_VARS; ++i) {
        auto &schema = ngx_http_ua_parser_vars[i];

        auto var = ngx_http_add_variable(cf, &schema.name, 0);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = ngx_http_ua_parser;
        var->data = (uintptr_t) schema.data;
    }

    return NGX_OK;
}


/* cleanup instance */
static void
ngx_http_ua_parser_cleanup(void *data)
{
    auto conf = static_cast<ngx_http_ua_parser_main_conf_t *>(data);
    delete conf->parser;
}


static void *
ngx_http_ua_parser_create_main_conf(ngx_conf_t *cf)
{
    auto cln = ngx_pool_cleanup_add(cf->pool, sizeof(ngx_http_ua_parser_main_conf_t));
    if (cln == NULL) {
        return NULL;
    }

    cln->handler = ngx_http_ua_parser_cleanup;

    auto conf = static_cast<ngx_http_ua_parser_main_conf_t *>(cln->data);
    conf->parser = NULL;

    return cln->data;
}


static char *
ngx_http_ua_parser_regexes_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    auto umcf = static_cast<ngx_http_ua_parser_main_conf_t *>(conf);

    ngx_str_t      *value;

    if (umcf->parser != NULL) {
        return const_cast<char *>("is duplicate");
    }

    value = static_cast<ngx_str_t *>(cf->args->elts);

    if (value[1].data && value[1].data[0] != '/') {
        if (ngx_conf_full_name(cf->cycle, &value[1], 0) != NGX_OK) {
            return (char *) NGX_CONF_ERROR;
        }
    }

    try {
        umcf->parser = new uap_cpp::UserAgentParser(
            std::string(reinterpret_cast<char *>(value[1].data), value[1].len)
        );

        if (!umcf->parser) {
            return (char *) NGX_CONF_ERROR;
        }

    } catch (const std::exception& e) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, 
                           "ua_parser: UserAgentParser init fail: %s",
                           e.what());
        return (char *) NGX_CONF_ERROR;

    } catch (...) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "ua_parser: UserAgentParser init fail: "
                           "unknown exception");
        return (char *) NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_ua_parser_create_loc_conf(ngx_conf_t *cf)
{
    auto conf = static_cast<ngx_http_ua_parser_loc_conf_t *>(
        ngx_pcalloc(cf->pool, sizeof(ngx_http_ua_parser_loc_conf_t))
    );
    if (conf == NULL) {
        return NULL;
    }

    conf->enabled = NGX_CONF_UNSET;
    conf->source = (ngx_http_complex_value_t *) NGX_CONF_UNSET_PTR;

    return conf;
}


static char *
ngx_http_ua_parser_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    auto prev = static_cast<ngx_http_ua_parser_loc_conf_t *>(parent);
    auto conf = static_cast<ngx_http_ua_parser_loc_conf_t *>(child);

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_ptr_value(conf->source, prev->source, NULL);

    return NGX_CONF_OK;
}