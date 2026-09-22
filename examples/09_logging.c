// Logging: levels, locations, the built-in handlers and a custom one.
#define CB_IMPLEMENTATION
#include "cb.h"

void shout_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    const char* name = "?";
    switch (level) {
    case CB_INFO: name = "info"; break;
    case CB_WARN: name = "warn"; break;
    case CB_ERROR: name = "error"; break;
    case CB_NO_LOGS: return;
    default: return;
    }
    fprintf(stderr, "<%s>", name);
    if (file != NULL) fprintf(stderr, " %s:%d", file, line);
    fprintf(stderr, " ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

int main(void)
{
    cb_minimal_log_level = CB_INFO;

    cb_set_log_handler(cb_default_log_handler);
    cb_log(CB_INFO, "plain info");
    CB_LOG_AT(CB_WARN, "with a location");

    cb_minimal_log_level = CB_ERROR; // INFO and WARN are dropped below this level
    cb_log(CB_INFO, "not printed");
    CB_LOG_AT(CB_ERROR, "errors still are");
    cb_minimal_log_level = CB_INFO;

    cb_set_log_handler(cb_cancer_log_handler);
    cb_log(CB_INFO, "cancer handler: emoji only when stderr is a terminal");

    cb_set_log_handler(cb_null_log_handler);
    cb_log(CB_ERROR, "swallowed");

    cb_set_log_handler(shout_handler);
    CB_LOG_AT(CB_WARN, "custom handler, level = %d", (int)CB_WARN);

    cb_set_log_handler(cb_default_log_handler);
    cb_log(CB_INFO, "handler restored: %s", cb_get_log_handler() == cb_default_log_handler ? "yes" : "no");
    return 0;
}
