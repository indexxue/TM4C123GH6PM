#include "type.h"

const char *status_to_str(status_t s)
{
    switch (s) {
        case STATUS_OK:
            return "OK";
        case STATUS_FAIL:
            return "FAIL";
        case STATUS_INVALID_ARG:
            return "INVALID_ARG";
        case STATUS_NO_MEM:
            return "NO_MEM";
        case STATUS_TIMEOUT:
            return "TIMEOUT";
        case STATUS_NOT_SUPPORTED:
            return "NOT_SUPPORTED";
        case STATUS_INVALID_STATE:
            return "INVALID_STATE";
        default:
            return "UNKNOWN";
    }
}
