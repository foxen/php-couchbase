/**
 *     Copyright 2016-2017 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "couchbase.h"

#define LOGARGS(instance, lvl) LCB_LOG_##lvl, instance, "pcbc/remove", __FILE__, __LINE__

void remove_callback(lcb_t instance, int cbtype, const lcb_RESPREMOVE *resp)
{
    opcookie_store_res *result = ecalloc(1, sizeof(opcookie_store_res));
    const lcb_MUTATION_TOKEN *mutinfo;
    TSRMLS_FETCH();

    result->header.err = resp->rc;
    result->key_len = resp->nkey;
    if (resp->nkey) {
        result->key = estrndup(resp->key, resp->nkey);
    }
    result->cas = resp->cas;
    mutinfo = lcb_resp_get_mutation_token(cbtype, resp);
    if (mutinfo) {
        memcpy(&result->token, mutinfo, sizeof(result->token));
    }

    opcookie_push((opcookie *)resp->cookie, &result->header);
}

// remove($id {, $cas, $groupid}) : MetaDoc
PHP_METHOD(Bucket, remove)
{
    pcbc_bucket_t *obj = Z_BUCKET_OBJ_P(getThis());
    int ii, ncmds, nscheduled;
    pcbc_pp_state pp_state;
    pcbc_pp_id id;
    opcookie *cookie;
    zval *zcas, *zgroupid;
    lcb_error_t err;

    // Note that groupid is experimental here and should not be used.
    if (pcbc_pp_begin(ZEND_NUM_ARGS() TSRMLS_CC, &pp_state, "id||cas,groupid", &id, &zcas, &zgroupid) != SUCCESS) {
        throw_pcbc_exception("Invalid arguments.", LCB_EINVAL);
        RETURN_NULL();
    }

    ncmds = pcbc_pp_keycount(&pp_state);
    cookie = opcookie_init();

    nscheduled = 0;
    for (ii = 0; pcbc_pp_next(&pp_state); ++ii) {
        lcb_CMDREMOVE cmd = {0};

        PCBC_CHECK_ZVAL_STRING(zcas, "cas must be a string");
        PCBC_CHECK_ZVAL_STRING(zgroupid, "groupid must be a string");

        LCB_CMD_SET_KEY(&cmd, id.str, id.len);
        if (zcas) {
            cmd.cas = pcbc_cas_decode(zcas TSRMLS_CC);
        }
        if (zgroupid) {
            LCB_KREQ_SIMPLE(&cmd._hashkey, Z_STRVAL_P(zgroupid), Z_STRLEN_P(zgroupid));
        }

        err = lcb_remove3(obj->conn->lcb, cookie, &cmd);
        if (err != LCB_SUCCESS) {
            break;
        }
        nscheduled++;
    }
    pcbc_assert_number_of_commands(obj->conn->lcb, "remove", nscheduled, ncmds);

    if (nscheduled) {
        lcb_wait(obj->conn->lcb);

        err = proc_remove_results(obj, return_value, cookie, pcbc_pp_ismapped(&pp_state) TSRMLS_CC);
    }

    opcookie_destroy(cookie);

    if (err != LCB_SUCCESS) {
        throw_lcb_exception(err);
    }
}
