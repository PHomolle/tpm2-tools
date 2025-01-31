/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "log.h"
#include "tpm2_policy.h"
#include "tpm2_tool.h"

typedef struct tpm2_policysecret_ctx tpm2_policysecret_ctx;
struct tpm2_policysecret_ctx {
    struct {
        const char *ctx_path; //auth_entity.ctx_path
        const char *auth_str; //auth_str
        tpm2_loaded_object object; //context_object && pwd_session
    } auth_entity;

    //File path for storing the policy digest output
    const char *out_policy_dgst_path;
    TPM2B_DIGEST *policy_digest;
    //File path for the session context data
    const char *extended_session_path;
    tpm2_session *extended_session;

    INT32 expiration;

    char *policy_ticket_path;

    char *policy_timeout_path;

    const char *qualifier_data_path;

    TPM2B_NONCE nonce_tpm;

    struct {
        UINT8 c :1;
    } flags;
};

static tpm2_policysecret_ctx ctx;

static bool on_option(char key, char *value) {

    bool result = true;
    char *input_file;

    switch (key) {
    case 'L':
        ctx.out_policy_dgst_path = value;
        break;
    case 'S':
        ctx.extended_session_path = value;
        break;
    case 'c':
        ctx.auth_entity.ctx_path = value;
        ctx.flags.c = 1;
        break;
    case 0:
        ctx.policy_ticket_path = value;
        break;
    case 1:
        ctx.policy_timeout_path = value;
        break;
    case 't':
        result = tpm2_util_string_to_uint32(value, (UINT32 *)&ctx.expiration);
        if (!result) {
            LOG_ERR("Failed reading expiration duration from value, got:\"%s\"",
                    value);
            return false;
        }
        break;
    case 'x':
        input_file = strcmp("-", value) ? value : NULL;
        if (input_file) {
            result = files_get_file_size_path(value,
            (long unsigned *)&ctx.nonce_tpm.size);
        }
        if (input_file && !result) {
            return false;
        }
        result = files_load_bytes_from_buffer_or_file_or_stdin(NULL, input_file,
                &ctx.nonce_tpm.size, ctx.nonce_tpm.buffer);
        if (!result) {
            return false;
        }
        break;
    case 'q':
        ctx.qualifier_data_path = value;
        break;
    }

    return result;
}

bool on_arg(int argc, char **argv) {

    if (argc > 1) {
        LOG_ERR("Specify a single auth value");
        return false;
    }

    if (!argc) {
        //empty auth
        return true;
    }

    ctx.auth_entity.auth_str = argv[0];

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    static struct option topts[] = {
        { "policy",         required_argument, NULL, 'L' },
        { "session",        required_argument, NULL, 'S' },
        { "object-context", required_argument, NULL, 'c' },
        { "expiration",     required_argument, NULL, 't' },
        { "nonce-tpm",      required_argument, NULL, 'x' },
        { "ticket",         required_argument, NULL,  0  },
        { "timeout",        required_argument, NULL,  1  },
        { "qualification",  required_argument, NULL, 'q' },
    };

    *opts = tpm2_options_new("L:S:c:t:x:q:", ARRAY_LEN(topts), topts, on_option,
            on_arg, 0);

    return *opts != NULL;
}

bool is_input_option_args_valid(void) {

    if (!ctx.extended_session_path) {
        LOG_ERR("Must specify -S session file.");
        return false;
    }

    if (!ctx.flags.c) {
        LOG_ERR("Must specify -c handle-id/ context file path.");
        return false;
    }

    return true;
}

tool_rc tpm2_tool_onrun(ESYS_CONTEXT *ectx, tpm2_option_flags flags) {

    UNUSED(flags);

    bool result = is_input_option_args_valid();
    if (!result) {
        return tool_rc_option_error;
    }

    tool_rc rc = tpm2_session_restore(ectx, ctx.extended_session_path, false,
            &ctx.extended_session);
    if (rc != tool_rc_success) {
        return rc;
    }

    rc = tpm2_util_object_load_auth(ectx, ctx.auth_entity.ctx_path,
            ctx.auth_entity.auth_str, &ctx.auth_entity.object, true,
            TPM2_HANDLE_ALL_W_NV);
    if (rc != tool_rc_success) {
        return rc;
    }

    /*
     * Build a policysecret using the pwd session. If the event of
     * a failure:
     * 1. always close the pwd session.
     * 2. log the policy secret failure and return tool_rc_general_error.
     * 3. if the error was closing the policy secret session, return that rc.
     */
    TPMT_TK_AUTH *policy_ticket = NULL;
    TPM2B_TIMEOUT *timeout = NULL;
    rc = tpm2_policy_build_policysecret(ectx, ctx.extended_session,
            &ctx.auth_entity.object, ctx.expiration, &policy_ticket, &timeout,
            &ctx.nonce_tpm, ctx.qualifier_data_path);
    tool_rc rc2 = tpm2_session_close(&ctx.auth_entity.object.session);
    if (rc != tool_rc_success) {
        goto tpm2_tool_onrun_out;
    }
    if (rc2 != tool_rc_success) {
        rc = rc2;
        goto tpm2_tool_onrun_out;
    }

    rc = tpm2_session_close(&ctx.auth_entity.object.session);
    if (rc != tool_rc_success) {
        LOG_ERR("Could not build policysecret");
        goto tpm2_tool_onrun_out;
    }

    rc = tpm2_policy_tool_finish(ectx, ctx.extended_session,
            ctx.out_policy_dgst_path);
    if (rc != tool_rc_success) {
        goto tpm2_tool_onrun_out;
    }

    if (ctx.policy_timeout_path) {
        if(!timeout->size) {
            LOG_WARN("Policy assertion did not produce timeout");
        } else {
            result = files_save_bytes_to_file(ctx.policy_timeout_path,
            timeout->buffer, timeout->size);
        }
    }
    if (!result) {
        LOG_ERR("Failed to save timeout to file.");
        rc = tool_rc_general_error;
        goto tpm2_tool_onrun_out;
    }

    if (ctx.policy_ticket_path) {
        if (!policy_ticket->digest.size) {
            LOG_WARN("Policy assertion did not produce auth ticket.");
        } else {
            result = files_save_authorization_ticket(policy_ticket,
            ctx.policy_ticket_path);
        }
    }
    if (!result) {
        LOG_ERR("Failed to save auth ticket");
        rc = tool_rc_general_error;
    }

tpm2_tool_onrun_out:
    free(policy_ticket);
    free(timeout);
    return rc;
}

tool_rc tpm2_tool_onstop(ESYS_CONTEXT *ectx) {
    UNUSED(ectx);
    free(ctx.policy_digest);
    return tpm2_session_close(&ctx.extended_session);
}
