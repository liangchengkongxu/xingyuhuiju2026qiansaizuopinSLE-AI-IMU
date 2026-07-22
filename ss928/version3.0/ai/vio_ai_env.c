#include "vio_ai_internal.h"

int vio_ai_env_get_int_default(const char *key, int defv)
{
    const char *e = getenv(key);
    if (e == TD_NULL || e[0] == '\0') {
        return defv;
    }
    return atoi(e);
}

float vio_ai_env_get_float_default(const char *key, float defv)
{
    const char *e = getenv(key);
    if (e == TD_NULL || e[0] == '\0') {
        return defv;
    }
    return (float)atof(e);
}
