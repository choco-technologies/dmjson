/* Native sanitizer harness only; production uses DMOD-generated registration. */
#define dmod_dmjson_api(version, result, name, args) result dmjson##name args
#define dmod_dmjson_api_declaration(version, result, name, args) result dmjson##name args
