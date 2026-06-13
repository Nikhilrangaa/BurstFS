#include "errors.h"
#include "journal.h"

const char *ckptfs_state_str(int state) {
    switch ((ckpt_state_t)state) {
        case CKPT_PENDING: return "PENDING";
        case CKPT_LOCAL_DURABLE: return "LOCAL_DURABLE";
        case CKPT_REPLICATING: return "REPLICATING";
        case CKPT_REMOTE_DURABLE: return "REMOTE_DURABLE";
        case CKPT_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}
