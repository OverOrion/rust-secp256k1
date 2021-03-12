/***********************************************************************
 * Copyright (c) 2013, 2014, 2015 Pieter Wuille, Gregory Maxwell       *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#if defined HAVE_CONFIG_H
#include "libsecp256k1-config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include "secp256k1.c"
#include "include/secp256k1.h"
#include "include/secp256k1_preallocated.h"
#include "testrand_impl.h"

#ifdef ENABLE_OPENSSL_TESTS
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/ecdsa.h"
#include "openssl/obj_mac.h"
# if OPENSSL_VERSION_NUMBER < 0x10100000L
void ECDSA_SIG_get0(const ECDSA_SIG *sig, const BIGNUM **pr, const BIGNUM **ps) {*pr = sig->r; *ps = sig->s;}
# endif
#endif

#include "contrib/lax_der_parsing.c"
#include "contrib/lax_der_privatekey_parsing.c"

static int count = 64;
static rustsecp256k1_v0_4_0_context *ctx = NULL;

static void counting_illegal_callback_fn(const char* str, void* data) {
    /* Dummy callback function that just counts. */
    int32_t *p;
    (void)str;
    p = data;
    (*p)++;
}

static void uncounting_illegal_callback_fn(const char* str, void* data) {
    /* Dummy callback function that just counts (backwards). */
    int32_t *p;
    (void)str;
    p = data;
    (*p)--;
}

void random_field_element_test(rustsecp256k1_v0_4_0_fe *fe) {
    do {
        unsigned char b32[32];
        rustsecp256k1_v0_4_0_testrand256_test(b32);
        if (rustsecp256k1_v0_4_0_fe_set_b32(fe, b32)) {
            break;
        }
    } while(1);
}

void random_field_element_magnitude(rustsecp256k1_v0_4_0_fe *fe) {
    rustsecp256k1_v0_4_0_fe zero;
    int n = rustsecp256k1_v0_4_0_testrand_int(9);
    rustsecp256k1_v0_4_0_fe_normalize(fe);
    if (n == 0) {
        return;
    }
    rustsecp256k1_v0_4_0_fe_clear(&zero);
    rustsecp256k1_v0_4_0_fe_negate(&zero, &zero, 0);
    rustsecp256k1_v0_4_0_fe_mul_int(&zero, n - 1);
    rustsecp256k1_v0_4_0_fe_add(fe, &zero);
#ifdef VERIFY
    CHECK(fe->magnitude == n);
#endif
}

void random_group_element_test(rustsecp256k1_v0_4_0_ge *ge) {
    rustsecp256k1_v0_4_0_fe fe;
    do {
        random_field_element_test(&fe);
        if (rustsecp256k1_v0_4_0_ge_set_xo_var(ge, &fe, rustsecp256k1_v0_4_0_testrand_bits(1))) {
            rustsecp256k1_v0_4_0_fe_normalize(&ge->y);
            break;
        }
    } while(1);
    ge->infinity = 0;
}

void random_group_element_jacobian_test(rustsecp256k1_v0_4_0_gej *gej, const rustsecp256k1_v0_4_0_ge *ge) {
    rustsecp256k1_v0_4_0_fe z2, z3;
    do {
        random_field_element_test(&gej->z);
        if (!rustsecp256k1_v0_4_0_fe_is_zero(&gej->z)) {
            break;
        }
    } while(1);
    rustsecp256k1_v0_4_0_fe_sqr(&z2, &gej->z);
    rustsecp256k1_v0_4_0_fe_mul(&z3, &z2, &gej->z);
    rustsecp256k1_v0_4_0_fe_mul(&gej->x, &ge->x, &z2);
    rustsecp256k1_v0_4_0_fe_mul(&gej->y, &ge->y, &z3);
    gej->infinity = ge->infinity;
}

void random_scalar_order_test(rustsecp256k1_v0_4_0_scalar *num) {
    do {
        unsigned char b32[32];
        int overflow = 0;
        rustsecp256k1_v0_4_0_testrand256_test(b32);
        rustsecp256k1_v0_4_0_scalar_set_b32(num, b32, &overflow);
        if (overflow || rustsecp256k1_v0_4_0_scalar_is_zero(num)) {
            continue;
        }
        break;
    } while(1);
}

void random_scalar_order(rustsecp256k1_v0_4_0_scalar *num) {
    do {
        unsigned char b32[32];
        int overflow = 0;
        rustsecp256k1_v0_4_0_testrand256(b32);
        rustsecp256k1_v0_4_0_scalar_set_b32(num, b32, &overflow);
        if (overflow || rustsecp256k1_v0_4_0_scalar_is_zero(num)) {
            continue;
        }
        break;
    } while(1);
}

void random_scalar_order_b32(unsigned char *b32) {
    rustsecp256k1_v0_4_0_scalar num;
    random_scalar_order(&num);
    rustsecp256k1_v0_4_0_scalar_get_b32(b32, &num);
}

void run_context_tests(int use_prealloc) {
    rustsecp256k1_v0_4_0_pubkey pubkey;
    rustsecp256k1_v0_4_0_pubkey zero_pubkey;
    rustsecp256k1_v0_4_0_ecdsa_signature sig;
    unsigned char ctmp[32];
    int32_t ecount;
    int32_t ecount2;
    rustsecp256k1_v0_4_0_context *none;
    rustsecp256k1_v0_4_0_context *sign;
    rustsecp256k1_v0_4_0_context *vrfy;
    rustsecp256k1_v0_4_0_context *both;
    void *none_prealloc = NULL;
    void *sign_prealloc = NULL;
    void *vrfy_prealloc = NULL;
    void *both_prealloc = NULL;

    rustsecp256k1_v0_4_0_gej pubj;
    rustsecp256k1_v0_4_0_ge pub;
    rustsecp256k1_v0_4_0_scalar msg, key, nonce;
    rustsecp256k1_v0_4_0_scalar sigr, sigs;

    if (use_prealloc) {
        none_prealloc = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_NONE));
        sign_prealloc = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_SIGN));
        vrfy_prealloc = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_VERIFY));
        both_prealloc = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY));
        CHECK(none_prealloc != NULL);
        CHECK(sign_prealloc != NULL);
        CHECK(vrfy_prealloc != NULL);
        CHECK(both_prealloc != NULL);
        none = rustsecp256k1_v0_4_0_context_preallocated_create(none_prealloc, SECP256K1_CONTEXT_NONE);
        sign = rustsecp256k1_v0_4_0_context_preallocated_create(sign_prealloc, SECP256K1_CONTEXT_SIGN);
        vrfy = rustsecp256k1_v0_4_0_context_preallocated_create(vrfy_prealloc, SECP256K1_CONTEXT_VERIFY);
        both = rustsecp256k1_v0_4_0_context_preallocated_create(both_prealloc, SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    } else {
        none = rustsecp256k1_v0_4_0_context_create(SECP256K1_CONTEXT_NONE);
        sign = rustsecp256k1_v0_4_0_context_create(SECP256K1_CONTEXT_SIGN);
        vrfy = rustsecp256k1_v0_4_0_context_create(SECP256K1_CONTEXT_VERIFY);
        both = rustsecp256k1_v0_4_0_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }

    memset(&zero_pubkey, 0, sizeof(zero_pubkey));

    ecount = 0;
    ecount2 = 10;
    rustsecp256k1_v0_4_0_context_set_illegal_callback(vrfy, counting_illegal_callback_fn, &ecount);
    rustsecp256k1_v0_4_0_context_set_illegal_callback(sign, counting_illegal_callback_fn, &ecount2);
    /* set error callback (to a function that still aborts in case malloc() fails in rustsecp256k1_v0_4_0_context_clone() below) */
    rustsecp256k1_v0_4_0_context_set_error_callback(sign, rustsecp256k1_v0_4_0_default_illegal_callback_fn, NULL);
    CHECK(sign->error_callback.fn != vrfy->error_callback.fn);
    CHECK(sign->error_callback.fn == rustsecp256k1_v0_4_0_default_illegal_callback_fn);

    /* check if sizes for cloning are consistent */
    CHECK(rustsecp256k1_v0_4_0_context_preallocated_clone_size(none) == rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_NONE));
    CHECK(rustsecp256k1_v0_4_0_context_preallocated_clone_size(sign) == rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_SIGN));
    CHECK(rustsecp256k1_v0_4_0_context_preallocated_clone_size(vrfy) == rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_VERIFY));
    CHECK(rustsecp256k1_v0_4_0_context_preallocated_clone_size(both) == rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY));

    /*** clone and destroy all of them to make sure cloning was complete ***/
    {
        rustsecp256k1_v0_4_0_context *ctx_tmp;

        if (use_prealloc) {
            /* clone into a non-preallocated context and then again into a new preallocated one. */
            ctx_tmp = none; none = rustsecp256k1_v0_4_0_context_clone(none); rustsecp256k1_v0_4_0_context_preallocated_destroy(ctx_tmp);
            free(none_prealloc); none_prealloc = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_NONE)); CHECK(none_prealloc != NULL);
            ctx_tmp = none; none = rustsecp256k1_v0_4_0_context_preallocated_clone(none, none_prealloc); rustsecp256k1_v0_4_0_context_destroy(ctx_tmp);

            ctx_tmp = sign; sign = rustsecp256k1_v0_4_0_context_clone(sign); rustsecp256k1_v0_4_0_context_preallocated_destroy(ctx_tmp);
            free(sign_prealloc); sign_prealloc = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_SIGN)); CHECK(sign_prealloc != NULL);
            ctx_tmp = sign; sign = rustsecp256k1_v0_4_0_context_preallocated_clone(sign, sign_prealloc); rustsecp256k1_v0_4_0_context_destroy(ctx_tmp);

            ctx_tmp = vrfy; vrfy = rustsecp256k1_v0_4_0_context_clone(vrfy); rustsecp256k1_v0_4_0_context_preallocated_destroy(ctx_tmp);
            free(vrfy_prealloc); vrfy_prealloc = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_VERIFY)); CHECK(vrfy_prealloc != NULL);
            ctx_tmp = vrfy; vrfy = rustsecp256k1_v0_4_0_context_preallocated_clone(vrfy, vrfy_prealloc); rustsecp256k1_v0_4_0_context_destroy(ctx_tmp);

            ctx_tmp = both; both = rustsecp256k1_v0_4_0_context_clone(both); rustsecp256k1_v0_4_0_context_preallocated_destroy(ctx_tmp);
            free(both_prealloc); both_prealloc = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY)); CHECK(both_prealloc != NULL);
            ctx_tmp = both; both = rustsecp256k1_v0_4_0_context_preallocated_clone(both, both_prealloc); rustsecp256k1_v0_4_0_context_destroy(ctx_tmp);
        } else {
            /* clone into a preallocated context and then again into a new non-preallocated one. */
            void *prealloc_tmp;

            prealloc_tmp = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_NONE)); CHECK(prealloc_tmp != NULL);
            ctx_tmp = none; none = rustsecp256k1_v0_4_0_context_preallocated_clone(none, prealloc_tmp); rustsecp256k1_v0_4_0_context_destroy(ctx_tmp);
            ctx_tmp = none; none = rustsecp256k1_v0_4_0_context_clone(none); rustsecp256k1_v0_4_0_context_preallocated_destroy(ctx_tmp);
            free(prealloc_tmp);

            prealloc_tmp = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_SIGN)); CHECK(prealloc_tmp != NULL);
            ctx_tmp = sign; sign = rustsecp256k1_v0_4_0_context_preallocated_clone(sign, prealloc_tmp); rustsecp256k1_v0_4_0_context_destroy(ctx_tmp);
            ctx_tmp = sign; sign = rustsecp256k1_v0_4_0_context_clone(sign); rustsecp256k1_v0_4_0_context_preallocated_destroy(ctx_tmp);
            free(prealloc_tmp);

            prealloc_tmp = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_VERIFY)); CHECK(prealloc_tmp != NULL);
            ctx_tmp = vrfy; vrfy = rustsecp256k1_v0_4_0_context_preallocated_clone(vrfy, prealloc_tmp); rustsecp256k1_v0_4_0_context_destroy(ctx_tmp);
            ctx_tmp = vrfy; vrfy = rustsecp256k1_v0_4_0_context_clone(vrfy); rustsecp256k1_v0_4_0_context_preallocated_destroy(ctx_tmp);
            free(prealloc_tmp);

            prealloc_tmp = malloc(rustsecp256k1_v0_4_0_context_preallocated_size(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY)); CHECK(prealloc_tmp != NULL);
            ctx_tmp = both; both = rustsecp256k1_v0_4_0_context_preallocated_clone(both, prealloc_tmp); rustsecp256k1_v0_4_0_context_destroy(ctx_tmp);
            ctx_tmp = both; both = rustsecp256k1_v0_4_0_context_clone(both); rustsecp256k1_v0_4_0_context_preallocated_destroy(ctx_tmp);
            free(prealloc_tmp);
        }
    }

    /* Verify that the error callback makes it across the clone. */
    CHECK(sign->error_callback.fn != vrfy->error_callback.fn);
    CHECK(sign->error_callback.fn == rustsecp256k1_v0_4_0_default_illegal_callback_fn);
    /* And that it resets back to default. */
    rustsecp256k1_v0_4_0_context_set_error_callback(sign, NULL, NULL);
    CHECK(vrfy->error_callback.fn == sign->error_callback.fn);

    /*** attempt to use them ***/
    random_scalar_order_test(&msg);
    random_scalar_order_test(&key);
    rustsecp256k1_v0_4_0_ecmult_gen(&both->ecmult_gen_ctx, &pubj, &key);
    rustsecp256k1_v0_4_0_ge_set_gej(&pub, &pubj);

    /* Verify context-type checking illegal-argument errors. */
    memset(ctmp, 1, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(vrfy, &pubkey, ctmp) == 0);
    CHECK(ecount == 1);
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(sign, &pubkey, ctmp) == 1);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(vrfy, &sig, ctmp, ctmp, NULL, NULL) == 0);
    CHECK(ecount == 2);
    VG_UNDEF(&sig, sizeof(sig));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(sign, &sig, ctmp, ctmp, NULL, NULL) == 1);
    VG_CHECK(&sig, sizeof(sig));
    CHECK(ecount2 == 10);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(sign, &sig, ctmp, &pubkey) == 0);
    CHECK(ecount2 == 11);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(vrfy, &sig, ctmp, &pubkey) == 1);
    CHECK(ecount == 2);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(sign, &pubkey, ctmp) == 0);
    CHECK(ecount2 == 12);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(vrfy, &pubkey, ctmp) == 1);
    CHECK(ecount == 2);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(sign, &pubkey, ctmp) == 0);
    CHECK(ecount2 == 13);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_negate(vrfy, &pubkey) == 1);
    CHECK(ecount == 2);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_negate(sign, &pubkey) == 1);
    CHECK(ecount == 2);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_negate(sign, NULL) == 0);
    CHECK(ecount2 == 14);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_negate(vrfy, &zero_pubkey) == 0);
    CHECK(ecount == 3);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(vrfy, &pubkey, ctmp) == 1);
    CHECK(ecount == 3);
    CHECK(rustsecp256k1_v0_4_0_context_randomize(vrfy, ctmp) == 1);
    CHECK(ecount == 3);
    CHECK(rustsecp256k1_v0_4_0_context_randomize(vrfy, NULL) == 1);
    CHECK(ecount == 3);
    CHECK(rustsecp256k1_v0_4_0_context_randomize(sign, ctmp) == 1);
    CHECK(ecount2 == 14);
    CHECK(rustsecp256k1_v0_4_0_context_randomize(sign, NULL) == 1);
    CHECK(ecount2 == 14);
    rustsecp256k1_v0_4_0_context_set_illegal_callback(vrfy, NULL, NULL);
    rustsecp256k1_v0_4_0_context_set_illegal_callback(sign, NULL, NULL);

    /* obtain a working nonce */
    do {
        random_scalar_order_test(&nonce);
    } while(!rustsecp256k1_v0_4_0_ecdsa_sig_sign(&both->ecmult_gen_ctx, &sigr, &sigs, &key, &msg, &nonce, NULL));

    /* try signing */
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_sign(&sign->ecmult_gen_ctx, &sigr, &sigs, &key, &msg, &nonce, NULL));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_sign(&both->ecmult_gen_ctx, &sigr, &sigs, &key, &msg, &nonce, NULL));

    /* try verifying */
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&vrfy->ecmult_ctx, &sigr, &sigs, &pub, &msg));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&both->ecmult_ctx, &sigr, &sigs, &pub, &msg));

    /* cleanup */
    if (use_prealloc) {
        rustsecp256k1_v0_4_0_context_preallocated_destroy(none);
        rustsecp256k1_v0_4_0_context_preallocated_destroy(sign);
        rustsecp256k1_v0_4_0_context_preallocated_destroy(vrfy);
        rustsecp256k1_v0_4_0_context_preallocated_destroy(both);
        free(none_prealloc);
        free(sign_prealloc);
        free(vrfy_prealloc);
        free(both_prealloc);
    } else {
        rustsecp256k1_v0_4_0_context_destroy(none);
        rustsecp256k1_v0_4_0_context_destroy(sign);
        rustsecp256k1_v0_4_0_context_destroy(vrfy);
        rustsecp256k1_v0_4_0_context_destroy(both);
    }
    /* Defined as no-op. */
    rustsecp256k1_v0_4_0_context_destroy(NULL);
    rustsecp256k1_v0_4_0_context_preallocated_destroy(NULL);

}

void run_scratch_tests(void) {
    const size_t adj_alloc = ((500 + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;

    int32_t ecount = 0;
    size_t checkpoint;
    size_t checkpoint_2;
    rustsecp256k1_v0_4_0_context *none = rustsecp256k1_v0_4_0_context_create(SECP256K1_CONTEXT_NONE);
    rustsecp256k1_v0_4_0_scratch_space *scratch;
    rustsecp256k1_v0_4_0_scratch_space local_scratch;

    /* Test public API */
    rustsecp256k1_v0_4_0_context_set_illegal_callback(none, counting_illegal_callback_fn, &ecount);
    rustsecp256k1_v0_4_0_context_set_error_callback(none, counting_illegal_callback_fn, &ecount);

    scratch = rustsecp256k1_v0_4_0_scratch_space_create(none, 1000);
    CHECK(scratch != NULL);
    CHECK(ecount == 0);

    /* Test internal API */
    CHECK(rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, 0) == 1000);
    CHECK(rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, 1) == 1000 - (ALIGNMENT - 1));
    CHECK(scratch->alloc_size == 0);
    CHECK(scratch->alloc_size % ALIGNMENT == 0);

    /* Allocating 500 bytes succeeds */
    checkpoint = rustsecp256k1_v0_4_0_scratch_checkpoint(&none->error_callback, scratch);
    CHECK(rustsecp256k1_v0_4_0_scratch_alloc(&none->error_callback, scratch, 500) != NULL);
    CHECK(rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, 0) == 1000 - adj_alloc);
    CHECK(rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, 1) == 1000 - adj_alloc - (ALIGNMENT - 1));
    CHECK(scratch->alloc_size != 0);
    CHECK(scratch->alloc_size % ALIGNMENT == 0);

    /* Allocating another 501 bytes fails */
    CHECK(rustsecp256k1_v0_4_0_scratch_alloc(&none->error_callback, scratch, 501) == NULL);
    CHECK(rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, 0) == 1000 - adj_alloc);
    CHECK(rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, 1) == 1000 - adj_alloc - (ALIGNMENT - 1));
    CHECK(scratch->alloc_size != 0);
    CHECK(scratch->alloc_size % ALIGNMENT == 0);

    /* ...but it succeeds once we apply the checkpoint to undo it */
    rustsecp256k1_v0_4_0_scratch_apply_checkpoint(&none->error_callback, scratch, checkpoint);
    CHECK(scratch->alloc_size == 0);
    CHECK(rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, 0) == 1000);
    CHECK(rustsecp256k1_v0_4_0_scratch_alloc(&none->error_callback, scratch, 500) != NULL);
    CHECK(scratch->alloc_size != 0);

    /* try to apply a bad checkpoint */
    checkpoint_2 = rustsecp256k1_v0_4_0_scratch_checkpoint(&none->error_callback, scratch);
    rustsecp256k1_v0_4_0_scratch_apply_checkpoint(&none->error_callback, scratch, checkpoint);
    CHECK(ecount == 0);
    rustsecp256k1_v0_4_0_scratch_apply_checkpoint(&none->error_callback, scratch, checkpoint_2); /* checkpoint_2 is after checkpoint */
    CHECK(ecount == 1);
    rustsecp256k1_v0_4_0_scratch_apply_checkpoint(&none->error_callback, scratch, (size_t) -1); /* this is just wildly invalid */
    CHECK(ecount == 2);

    /* try to use badly initialized scratch space */
    rustsecp256k1_v0_4_0_scratch_space_destroy(none, scratch);
    memset(&local_scratch, 0, sizeof(local_scratch));
    scratch = &local_scratch;
    CHECK(!rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, 0));
    CHECK(ecount == 3);
    CHECK(rustsecp256k1_v0_4_0_scratch_alloc(&none->error_callback, scratch, 500) == NULL);
    CHECK(ecount == 4);
    rustsecp256k1_v0_4_0_scratch_space_destroy(none, scratch);
    CHECK(ecount == 5);

    /* Test that large integers do not wrap around in a bad way */
    scratch = rustsecp256k1_v0_4_0_scratch_space_create(none, 1000);
    /* Try max allocation with a large number of objects. Only makes sense if
     * ALIGNMENT is greater than 1 because otherwise the objects take no extra
     * space. */
    CHECK(ALIGNMENT <= 1 || !rustsecp256k1_v0_4_0_scratch_max_allocation(&none->error_callback, scratch, (SIZE_MAX / (ALIGNMENT - 1)) + 1));
    /* Try allocating SIZE_MAX to test wrap around which only happens if
     * ALIGNMENT > 1, otherwise it returns NULL anyway because the scratch
     * space is too small. */
    CHECK(rustsecp256k1_v0_4_0_scratch_alloc(&none->error_callback, scratch, SIZE_MAX) == NULL);
    rustsecp256k1_v0_4_0_scratch_space_destroy(none, scratch);

    /* cleanup */
    rustsecp256k1_v0_4_0_scratch_space_destroy(none, NULL); /* no-op */
    rustsecp256k1_v0_4_0_context_destroy(none);
}

/***** HASH TESTS *****/

void run_sha256_tests(void) {
    static const char *inputs[8] = {
        "", "abc", "message digest", "secure hash algorithm", "SHA256 is considered to be safe",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "For this sample, this 63-byte string will be used as input data",
        "This is exactly 64 bytes long, not counting the terminating byte"
    };
    static const unsigned char outputs[8][32] = {
        {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55},
        {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad},
        {0xf7, 0x84, 0x6f, 0x55, 0xcf, 0x23, 0xe1, 0x4e, 0xeb, 0xea, 0xb5, 0xb4, 0xe1, 0x55, 0x0c, 0xad, 0x5b, 0x50, 0x9e, 0x33, 0x48, 0xfb, 0xc4, 0xef, 0xa3, 0xa1, 0x41, 0x3d, 0x39, 0x3c, 0xb6, 0x50},
        {0xf3, 0x0c, 0xeb, 0x2b, 0xb2, 0x82, 0x9e, 0x79, 0xe4, 0xca, 0x97, 0x53, 0xd3, 0x5a, 0x8e, 0xcc, 0x00, 0x26, 0x2d, 0x16, 0x4c, 0xc0, 0x77, 0x08, 0x02, 0x95, 0x38, 0x1c, 0xbd, 0x64, 0x3f, 0x0d},
        {0x68, 0x19, 0xd9, 0x15, 0xc7, 0x3f, 0x4d, 0x1e, 0x77, 0xe4, 0xe1, 0xb5, 0x2d, 0x1f, 0xa0, 0xf9, 0xcf, 0x9b, 0xea, 0xea, 0xd3, 0x93, 0x9f, 0x15, 0x87, 0x4b, 0xd9, 0x88, 0xe2, 0xa2, 0x36, 0x30},
        {0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39, 0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1},
        {0xf0, 0x8a, 0x78, 0xcb, 0xba, 0xee, 0x08, 0x2b, 0x05, 0x2a, 0xe0, 0x70, 0x8f, 0x32, 0xfa, 0x1e, 0x50, 0xc5, 0xc4, 0x21, 0xaa, 0x77, 0x2b, 0xa5, 0xdb, 0xb4, 0x06, 0xa2, 0xea, 0x6b, 0xe3, 0x42},
        {0xab, 0x64, 0xef, 0xf7, 0xe8, 0x8e, 0x2e, 0x46, 0x16, 0x5e, 0x29, 0xf2, 0xbc, 0xe4, 0x18, 0x26, 0xbd, 0x4c, 0x7b, 0x35, 0x52, 0xf6, 0xb3, 0x82, 0xa9, 0xe7, 0xd3, 0xaf, 0x47, 0xc2, 0x45, 0xf8}
    };
    int i;
    for (i = 0; i < 8; i++) {
        unsigned char out[32];
        rustsecp256k1_v0_4_0_sha256 hasher;
        rustsecp256k1_v0_4_0_sha256_initialize(&hasher);
        rustsecp256k1_v0_4_0_sha256_write(&hasher, (const unsigned char*)(inputs[i]), strlen(inputs[i]));
        rustsecp256k1_v0_4_0_sha256_finalize(&hasher, out);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(out, outputs[i], 32) == 0);
        if (strlen(inputs[i]) > 0) {
            int split = rustsecp256k1_v0_4_0_testrand_int(strlen(inputs[i]));
            rustsecp256k1_v0_4_0_sha256_initialize(&hasher);
            rustsecp256k1_v0_4_0_sha256_write(&hasher, (const unsigned char*)(inputs[i]), split);
            rustsecp256k1_v0_4_0_sha256_write(&hasher, (const unsigned char*)(inputs[i] + split), strlen(inputs[i]) - split);
            rustsecp256k1_v0_4_0_sha256_finalize(&hasher, out);
            CHECK(rustsecp256k1_v0_4_0_memcmp_var(out, outputs[i], 32) == 0);
        }
    }
}

void run_hmac_sha256_tests(void) {
    static const char *keys[6] = {
        "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b",
        "\x4a\x65\x66\x65",
        "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa",
        "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19",
        "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa",
        "\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa\xaa"
    };
    static const char *inputs[6] = {
        "\x48\x69\x20\x54\x68\x65\x72\x65",
        "\x77\x68\x61\x74\x20\x64\x6f\x20\x79\x61\x20\x77\x61\x6e\x74\x20\x66\x6f\x72\x20\x6e\x6f\x74\x68\x69\x6e\x67\x3f",
        "\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd\xdd",
        "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd",
        "\x54\x65\x73\x74\x20\x55\x73\x69\x6e\x67\x20\x4c\x61\x72\x67\x65\x72\x20\x54\x68\x61\x6e\x20\x42\x6c\x6f\x63\x6b\x2d\x53\x69\x7a\x65\x20\x4b\x65\x79\x20\x2d\x20\x48\x61\x73\x68\x20\x4b\x65\x79\x20\x46\x69\x72\x73\x74",
        "\x54\x68\x69\x73\x20\x69\x73\x20\x61\x20\x74\x65\x73\x74\x20\x75\x73\x69\x6e\x67\x20\x61\x20\x6c\x61\x72\x67\x65\x72\x20\x74\x68\x61\x6e\x20\x62\x6c\x6f\x63\x6b\x2d\x73\x69\x7a\x65\x20\x6b\x65\x79\x20\x61\x6e\x64\x20\x61\x20\x6c\x61\x72\x67\x65\x72\x20\x74\x68\x61\x6e\x20\x62\x6c\x6f\x63\x6b\x2d\x73\x69\x7a\x65\x20\x64\x61\x74\x61\x2e\x20\x54\x68\x65\x20\x6b\x65\x79\x20\x6e\x65\x65\x64\x73\x20\x74\x6f\x20\x62\x65\x20\x68\x61\x73\x68\x65\x64\x20\x62\x65\x66\x6f\x72\x65\x20\x62\x65\x69\x6e\x67\x20\x75\x73\x65\x64\x20\x62\x79\x20\x74\x68\x65\x20\x48\x4d\x41\x43\x20\x61\x6c\x67\x6f\x72\x69\x74\x68\x6d\x2e"
    };
    static const unsigned char outputs[6][32] = {
        {0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7},
        {0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43},
        {0x77, 0x3e, 0xa9, 0x1e, 0x36, 0x80, 0x0e, 0x46, 0x85, 0x4d, 0xb8, 0xeb, 0xd0, 0x91, 0x81, 0xa7, 0x29, 0x59, 0x09, 0x8b, 0x3e, 0xf8, 0xc1, 0x22, 0xd9, 0x63, 0x55, 0x14, 0xce, 0xd5, 0x65, 0xfe},
        {0x82, 0x55, 0x8a, 0x38, 0x9a, 0x44, 0x3c, 0x0e, 0xa4, 0xcc, 0x81, 0x98, 0x99, 0xf2, 0x08, 0x3a, 0x85, 0xf0, 0xfa, 0xa3, 0xe5, 0x78, 0xf8, 0x07, 0x7a, 0x2e, 0x3f, 0xf4, 0x67, 0x29, 0x66, 0x5b},
        {0x60, 0xe4, 0x31, 0x59, 0x1e, 0xe0, 0xb6, 0x7f, 0x0d, 0x8a, 0x26, 0xaa, 0xcb, 0xf5, 0xb7, 0x7f, 0x8e, 0x0b, 0xc6, 0x21, 0x37, 0x28, 0xc5, 0x14, 0x05, 0x46, 0x04, 0x0f, 0x0e, 0xe3, 0x7f, 0x54},
        {0x9b, 0x09, 0xff, 0xa7, 0x1b, 0x94, 0x2f, 0xcb, 0x27, 0x63, 0x5f, 0xbc, 0xd5, 0xb0, 0xe9, 0x44, 0xbf, 0xdc, 0x63, 0x64, 0x4f, 0x07, 0x13, 0x93, 0x8a, 0x7f, 0x51, 0x53, 0x5c, 0x3a, 0x35, 0xe2}
    };
    int i;
    for (i = 0; i < 6; i++) {
        rustsecp256k1_v0_4_0_hmac_sha256 hasher;
        unsigned char out[32];
        rustsecp256k1_v0_4_0_hmac_sha256_initialize(&hasher, (const unsigned char*)(keys[i]), strlen(keys[i]));
        rustsecp256k1_v0_4_0_hmac_sha256_write(&hasher, (const unsigned char*)(inputs[i]), strlen(inputs[i]));
        rustsecp256k1_v0_4_0_hmac_sha256_finalize(&hasher, out);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(out, outputs[i], 32) == 0);
        if (strlen(inputs[i]) > 0) {
            int split = rustsecp256k1_v0_4_0_testrand_int(strlen(inputs[i]));
            rustsecp256k1_v0_4_0_hmac_sha256_initialize(&hasher, (const unsigned char*)(keys[i]), strlen(keys[i]));
            rustsecp256k1_v0_4_0_hmac_sha256_write(&hasher, (const unsigned char*)(inputs[i]), split);
            rustsecp256k1_v0_4_0_hmac_sha256_write(&hasher, (const unsigned char*)(inputs[i] + split), strlen(inputs[i]) - split);
            rustsecp256k1_v0_4_0_hmac_sha256_finalize(&hasher, out);
            CHECK(rustsecp256k1_v0_4_0_memcmp_var(out, outputs[i], 32) == 0);
        }
    }
}

void run_rfc6979_hmac_sha256_tests(void) {
    static const unsigned char key1[65] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x00, 0x4b, 0xf5, 0x12, 0x2f, 0x34, 0x45, 0x54, 0xc5, 0x3b, 0xde, 0x2e, 0xbb, 0x8c, 0xd2, 0xb7, 0xe3, 0xd1, 0x60, 0x0a, 0xd6, 0x31, 0xc3, 0x85, 0xa5, 0xd7, 0xcc, 0xe2, 0x3c, 0x77, 0x85, 0x45, 0x9a, 0};
    static const unsigned char out1[3][32] = {
        {0x4f, 0xe2, 0x95, 0x25, 0xb2, 0x08, 0x68, 0x09, 0x15, 0x9a, 0xcd, 0xf0, 0x50, 0x6e, 0xfb, 0x86, 0xb0, 0xec, 0x93, 0x2c, 0x7b, 0xa4, 0x42, 0x56, 0xab, 0x32, 0x1e, 0x42, 0x1e, 0x67, 0xe9, 0xfb},
        {0x2b, 0xf0, 0xff, 0xf1, 0xd3, 0xc3, 0x78, 0xa2, 0x2d, 0xc5, 0xde, 0x1d, 0x85, 0x65, 0x22, 0x32, 0x5c, 0x65, 0xb5, 0x04, 0x49, 0x1a, 0x0c, 0xbd, 0x01, 0xcb, 0x8f, 0x3a, 0xa6, 0x7f, 0xfd, 0x4a},
        {0xf5, 0x28, 0xb4, 0x10, 0xcb, 0x54, 0x1f, 0x77, 0x00, 0x0d, 0x7a, 0xfb, 0x6c, 0x5b, 0x53, 0xc5, 0xc4, 0x71, 0xea, 0xb4, 0x3e, 0x46, 0x6d, 0x9a, 0xc5, 0x19, 0x0c, 0x39, 0xc8, 0x2f, 0xd8, 0x2e}
    };

    static const unsigned char key2[64] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
    static const unsigned char out2[3][32] = {
        {0x9c, 0x23, 0x6c, 0x16, 0x5b, 0x82, 0xae, 0x0c, 0xd5, 0x90, 0x65, 0x9e, 0x10, 0x0b, 0x6b, 0xab, 0x30, 0x36, 0xe7, 0xba, 0x8b, 0x06, 0x74, 0x9b, 0xaf, 0x69, 0x81, 0xe1, 0x6f, 0x1a, 0x2b, 0x95},
        {0xdf, 0x47, 0x10, 0x61, 0x62, 0x5b, 0xc0, 0xea, 0x14, 0xb6, 0x82, 0xfe, 0xee, 0x2c, 0x9c, 0x02, 0xf2, 0x35, 0xda, 0x04, 0x20, 0x4c, 0x1d, 0x62, 0xa1, 0x53, 0x6c, 0x6e, 0x17, 0xae, 0xd7, 0xa9},
        {0x75, 0x97, 0x88, 0x7c, 0xbd, 0x76, 0x32, 0x1f, 0x32, 0xe3, 0x04, 0x40, 0x67, 0x9a, 0x22, 0xcf, 0x7f, 0x8d, 0x9d, 0x2e, 0xac, 0x39, 0x0e, 0x58, 0x1f, 0xea, 0x09, 0x1c, 0xe2, 0x02, 0xba, 0x94}
    };

    rustsecp256k1_v0_4_0_rfc6979_hmac_sha256 rng;
    unsigned char out[32];
    int i;

    rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_initialize(&rng, key1, 64);
    for (i = 0; i < 3; i++) {
        rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_generate(&rng, out, 32);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(out, out1[i], 32) == 0);
    }
    rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_finalize(&rng);

    rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_initialize(&rng, key1, 65);
    for (i = 0; i < 3; i++) {
        rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_generate(&rng, out, 32);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(out, out1[i], 32) != 0);
    }
    rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_finalize(&rng);

    rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_initialize(&rng, key2, 64);
    for (i = 0; i < 3; i++) {
        rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_generate(&rng, out, 32);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(out, out2[i], 32) == 0);
    }
    rustsecp256k1_v0_4_0_rfc6979_hmac_sha256_finalize(&rng);
}

/***** RANDOM TESTS *****/

void test_rand_bits(int rand32, int bits) {
    /* (1-1/2^B)^rounds[B] < 1/10^9, so rounds is the number of iterations to
     * get a false negative chance below once in a billion */
    static const unsigned int rounds[7] = {1, 30, 73, 156, 322, 653, 1316};
    /* We try multiplying the results with various odd numbers, which shouldn't
     * influence the uniform distribution modulo a power of 2. */
    static const uint32_t mults[6] = {1, 3, 21, 289, 0x9999, 0x80402011};
    /* We only select up to 6 bits from the output to analyse */
    unsigned int usebits = bits > 6 ? 6 : bits;
    unsigned int maxshift = bits - usebits;
    /* For each of the maxshift+1 usebits-bit sequences inside a bits-bit
       number, track all observed outcomes, one per bit in a uint64_t. */
    uint64_t x[6][27] = {{0}};
    unsigned int i, shift, m;
    /* Multiply the output of all rand calls with the odd number m, which
       should not change the uniformity of its distribution. */
    for (i = 0; i < rounds[usebits]; i++) {
        uint32_t r = (rand32 ? rustsecp256k1_v0_4_0_testrand32() : rustsecp256k1_v0_4_0_testrand_bits(bits));
        CHECK((((uint64_t)r) >> bits) == 0);
        for (m = 0; m < sizeof(mults) / sizeof(mults[0]); m++) {
            uint32_t rm = r * mults[m];
            for (shift = 0; shift <= maxshift; shift++) {
                x[m][shift] |= (((uint64_t)1) << ((rm >> shift) & ((1 << usebits) - 1)));
            }
        }
    }
    for (m = 0; m < sizeof(mults) / sizeof(mults[0]); m++) {
        for (shift = 0; shift <= maxshift; shift++) {
            /* Test that the lower usebits bits of x[shift] are 1 */
            CHECK(((~x[m][shift]) << (64 - (1 << usebits))) == 0);
        }
    }
}

/* Subrange must be a whole divisor of range, and at most 64 */
void test_rand_int(uint32_t range, uint32_t subrange) {
    /* (1-1/subrange)^rounds < 1/10^9 */
    int rounds = (subrange * 2073) / 100;
    int i;
    uint64_t x = 0;
    CHECK((range % subrange) == 0);
    for (i = 0; i < rounds; i++) {
        uint32_t r = rustsecp256k1_v0_4_0_testrand_int(range);
        CHECK(r < range);
        r = r % subrange;
        x |= (((uint64_t)1) << r);
    }
    /* Test that the lower subrange bits of x are 1. */
    CHECK(((~x) << (64 - subrange)) == 0);
}

void run_rand_bits(void) {
    size_t b;
    test_rand_bits(1, 32);
    for (b = 1; b <= 32; b++) {
        test_rand_bits(0, b);
    }
}

void run_rand_int(void) {
    static const uint32_t ms[] = {1, 3, 17, 1000, 13771, 999999, 33554432};
    static const uint32_t ss[] = {1, 3, 6, 9, 13, 31, 64};
    unsigned int m, s;
    for (m = 0; m < sizeof(ms) / sizeof(ms[0]); m++) {
        for (s = 0; s < sizeof(ss) / sizeof(ss[0]); s++) {
            test_rand_int(ms[m] * ss[s], ss[s]);
        }
    }
}

/***** NUM TESTS *****/

#ifndef USE_NUM_NONE
void random_num_negate(rustsecp256k1_v0_4_0_num *num) {
    if (rustsecp256k1_v0_4_0_testrand_bits(1)) {
        rustsecp256k1_v0_4_0_num_negate(num);
    }
}

void random_num_order_test(rustsecp256k1_v0_4_0_num *num) {
    rustsecp256k1_v0_4_0_scalar sc;
    random_scalar_order_test(&sc);
    rustsecp256k1_v0_4_0_scalar_get_num(num, &sc);
}

void random_num_order(rustsecp256k1_v0_4_0_num *num) {
    rustsecp256k1_v0_4_0_scalar sc;
    random_scalar_order(&sc);
    rustsecp256k1_v0_4_0_scalar_get_num(num, &sc);
}

void test_num_negate(void) {
    rustsecp256k1_v0_4_0_num n1;
    rustsecp256k1_v0_4_0_num n2;
    random_num_order_test(&n1); /* n1 = R */
    random_num_negate(&n1);
    rustsecp256k1_v0_4_0_num_copy(&n2, &n1); /* n2 = R */
    rustsecp256k1_v0_4_0_num_sub(&n1, &n2, &n1); /* n1 = n2-n1 = 0 */
    CHECK(rustsecp256k1_v0_4_0_num_is_zero(&n1));
    rustsecp256k1_v0_4_0_num_copy(&n1, &n2); /* n1 = R */
    rustsecp256k1_v0_4_0_num_negate(&n1); /* n1 = -R */
    CHECK(!rustsecp256k1_v0_4_0_num_is_zero(&n1));
    rustsecp256k1_v0_4_0_num_add(&n1, &n2, &n1); /* n1 = n2+n1 = 0 */
    CHECK(rustsecp256k1_v0_4_0_num_is_zero(&n1));
    rustsecp256k1_v0_4_0_num_copy(&n1, &n2); /* n1 = R */
    rustsecp256k1_v0_4_0_num_negate(&n1); /* n1 = -R */
    CHECK(rustsecp256k1_v0_4_0_num_is_neg(&n1) != rustsecp256k1_v0_4_0_num_is_neg(&n2));
    rustsecp256k1_v0_4_0_num_negate(&n1); /* n1 = R */
    CHECK(rustsecp256k1_v0_4_0_num_eq(&n1, &n2));
}

void test_num_add_sub(void) {
    int i;
    rustsecp256k1_v0_4_0_scalar s;
    rustsecp256k1_v0_4_0_num n1;
    rustsecp256k1_v0_4_0_num n2;
    rustsecp256k1_v0_4_0_num n1p2, n2p1, n1m2, n2m1;
    random_num_order_test(&n1); /* n1 = R1 */
    if (rustsecp256k1_v0_4_0_testrand_bits(1)) {
        random_num_negate(&n1);
    }
    random_num_order_test(&n2); /* n2 = R2 */
    if (rustsecp256k1_v0_4_0_testrand_bits(1)) {
        random_num_negate(&n2);
    }
    rustsecp256k1_v0_4_0_num_add(&n1p2, &n1, &n2); /* n1p2 = R1 + R2 */
    rustsecp256k1_v0_4_0_num_add(&n2p1, &n2, &n1); /* n2p1 = R2 + R1 */
    rustsecp256k1_v0_4_0_num_sub(&n1m2, &n1, &n2); /* n1m2 = R1 - R2 */
    rustsecp256k1_v0_4_0_num_sub(&n2m1, &n2, &n1); /* n2m1 = R2 - R1 */
    CHECK(rustsecp256k1_v0_4_0_num_eq(&n1p2, &n2p1));
    CHECK(!rustsecp256k1_v0_4_0_num_eq(&n1p2, &n1m2));
    rustsecp256k1_v0_4_0_num_negate(&n2m1); /* n2m1 = -R2 + R1 */
    CHECK(rustsecp256k1_v0_4_0_num_eq(&n2m1, &n1m2));
    CHECK(!rustsecp256k1_v0_4_0_num_eq(&n2m1, &n1));
    rustsecp256k1_v0_4_0_num_add(&n2m1, &n2m1, &n2); /* n2m1 = -R2 + R1 + R2 = R1 */
    CHECK(rustsecp256k1_v0_4_0_num_eq(&n2m1, &n1));
    CHECK(!rustsecp256k1_v0_4_0_num_eq(&n2p1, &n1));
    rustsecp256k1_v0_4_0_num_sub(&n2p1, &n2p1, &n2); /* n2p1 = R2 + R1 - R2 = R1 */
    CHECK(rustsecp256k1_v0_4_0_num_eq(&n2p1, &n1));

    /* check is_one */
    rustsecp256k1_v0_4_0_scalar_set_int(&s, 1);
    rustsecp256k1_v0_4_0_scalar_get_num(&n1, &s);
    CHECK(rustsecp256k1_v0_4_0_num_is_one(&n1));
    /* check that 2^n + 1 is never 1 */
    rustsecp256k1_v0_4_0_scalar_get_num(&n2, &s);
    for (i = 0; i < 250; ++i) {
        rustsecp256k1_v0_4_0_num_add(&n1, &n1, &n1);    /* n1 *= 2 */
        rustsecp256k1_v0_4_0_num_add(&n1p2, &n1, &n2);  /* n1p2 = n1 + 1 */
        CHECK(!rustsecp256k1_v0_4_0_num_is_one(&n1p2));
    }
}

void test_num_mod(void) {
    int i;
    rustsecp256k1_v0_4_0_scalar s;
    rustsecp256k1_v0_4_0_num order, n;

    /* check that 0 mod anything is 0 */
    random_scalar_order_test(&s);
    rustsecp256k1_v0_4_0_scalar_get_num(&order, &s);
    rustsecp256k1_v0_4_0_scalar_set_int(&s, 0);
    rustsecp256k1_v0_4_0_scalar_get_num(&n, &s);
    rustsecp256k1_v0_4_0_num_mod(&n, &order);
    CHECK(rustsecp256k1_v0_4_0_num_is_zero(&n));

    /* check that anything mod 1 is 0 */
    rustsecp256k1_v0_4_0_scalar_set_int(&s, 1);
    rustsecp256k1_v0_4_0_scalar_get_num(&order, &s);
    rustsecp256k1_v0_4_0_scalar_get_num(&n, &s);
    rustsecp256k1_v0_4_0_num_mod(&n, &order);
    CHECK(rustsecp256k1_v0_4_0_num_is_zero(&n));

    /* check that increasing the number past 2^256 does not break this */
    random_scalar_order_test(&s);
    rustsecp256k1_v0_4_0_scalar_get_num(&n, &s);
    /* multiply by 2^8, which'll test this case with high probability */
    for (i = 0; i < 8; ++i) {
        rustsecp256k1_v0_4_0_num_add(&n, &n, &n);
    }
    rustsecp256k1_v0_4_0_num_mod(&n, &order);
    CHECK(rustsecp256k1_v0_4_0_num_is_zero(&n));
}

void test_num_jacobi(void) {
    rustsecp256k1_v0_4_0_scalar sqr;
    rustsecp256k1_v0_4_0_scalar small;
    rustsecp256k1_v0_4_0_scalar five;  /* five is not a quadratic residue */
    rustsecp256k1_v0_4_0_num order, n;
    int i;
    /* squares mod 5 are 1, 4 */
    const int jacobi5[10] = { 0, 1, -1, -1, 1, 0, 1, -1, -1, 1 };

    /* check some small values with 5 as the order */
    rustsecp256k1_v0_4_0_scalar_set_int(&five, 5);
    rustsecp256k1_v0_4_0_scalar_get_num(&order, &five);
    for (i = 0; i < 10; ++i) {
        rustsecp256k1_v0_4_0_scalar_set_int(&small, i);
        rustsecp256k1_v0_4_0_scalar_get_num(&n, &small);
        CHECK(rustsecp256k1_v0_4_0_num_jacobi(&n, &order) == jacobi5[i]);
    }

    /** test large values with 5 as group order */
    rustsecp256k1_v0_4_0_scalar_get_num(&order, &five);
    /* we first need a scalar which is not a multiple of 5 */
    do {
        rustsecp256k1_v0_4_0_num fiven;
        random_scalar_order_test(&sqr);
        rustsecp256k1_v0_4_0_scalar_get_num(&fiven, &five);
        rustsecp256k1_v0_4_0_scalar_get_num(&n, &sqr);
        rustsecp256k1_v0_4_0_num_mod(&n, &fiven);
    } while (rustsecp256k1_v0_4_0_num_is_zero(&n));
    /* next force it to be a residue. 2 is a nonresidue mod 5 so we can
     * just multiply by two, i.e. add the number to itself */
    if (rustsecp256k1_v0_4_0_num_jacobi(&n, &order) == -1) {
        rustsecp256k1_v0_4_0_num_add(&n, &n, &n);
    }

    /* test residue */
    CHECK(rustsecp256k1_v0_4_0_num_jacobi(&n, &order) == 1);
    /* test nonresidue */
    rustsecp256k1_v0_4_0_num_add(&n, &n, &n);
    CHECK(rustsecp256k1_v0_4_0_num_jacobi(&n, &order) == -1);

    /** test with secp group order as order */
    rustsecp256k1_v0_4_0_scalar_order_get_num(&order);
    random_scalar_order_test(&sqr);
    rustsecp256k1_v0_4_0_scalar_sqr(&sqr, &sqr);
    /* test residue */
    rustsecp256k1_v0_4_0_scalar_get_num(&n, &sqr);
    CHECK(rustsecp256k1_v0_4_0_num_jacobi(&n, &order) == 1);
    /* test nonresidue */
    rustsecp256k1_v0_4_0_scalar_mul(&sqr, &sqr, &five);
    rustsecp256k1_v0_4_0_scalar_get_num(&n, &sqr);
    CHECK(rustsecp256k1_v0_4_0_num_jacobi(&n, &order) == -1);
    /* test multiple of the order*/
    CHECK(rustsecp256k1_v0_4_0_num_jacobi(&order, &order) == 0);

    /* check one less than the order */
    rustsecp256k1_v0_4_0_scalar_set_int(&small, 1);
    rustsecp256k1_v0_4_0_scalar_get_num(&n, &small);
    rustsecp256k1_v0_4_0_num_sub(&n, &order, &n);
    CHECK(rustsecp256k1_v0_4_0_num_jacobi(&n, &order) == 1);  /* sage confirms this is 1 */
}

void run_num_smalltests(void) {
    int i;
    for (i = 0; i < 100*count; i++) {
        test_num_negate();
        test_num_add_sub();
        test_num_mod();
        test_num_jacobi();
    }
}
#endif

/***** SCALAR TESTS *****/

void scalar_test(void) {
    rustsecp256k1_v0_4_0_scalar s;
    rustsecp256k1_v0_4_0_scalar s1;
    rustsecp256k1_v0_4_0_scalar s2;
#ifndef USE_NUM_NONE
    rustsecp256k1_v0_4_0_num snum, s1num, s2num;
    rustsecp256k1_v0_4_0_num order, half_order;
#endif
    unsigned char c[32];

    /* Set 's' to a random scalar, with value 'snum'. */
    random_scalar_order_test(&s);

    /* Set 's1' to a random scalar, with value 's1num'. */
    random_scalar_order_test(&s1);

    /* Set 's2' to a random scalar, with value 'snum2', and byte array representation 'c'. */
    random_scalar_order_test(&s2);
    rustsecp256k1_v0_4_0_scalar_get_b32(c, &s2);

#ifndef USE_NUM_NONE
    rustsecp256k1_v0_4_0_scalar_get_num(&snum, &s);
    rustsecp256k1_v0_4_0_scalar_get_num(&s1num, &s1);
    rustsecp256k1_v0_4_0_scalar_get_num(&s2num, &s2);

    rustsecp256k1_v0_4_0_scalar_order_get_num(&order);
    half_order = order;
    rustsecp256k1_v0_4_0_num_shift(&half_order, 1);
#endif

    {
        int i;
        /* Test that fetching groups of 4 bits from a scalar and recursing n(i)=16*n(i-1)+p(i) reconstructs it. */
        rustsecp256k1_v0_4_0_scalar n;
        rustsecp256k1_v0_4_0_scalar_set_int(&n, 0);
        for (i = 0; i < 256; i += 4) {
            rustsecp256k1_v0_4_0_scalar t;
            int j;
            rustsecp256k1_v0_4_0_scalar_set_int(&t, rustsecp256k1_v0_4_0_scalar_get_bits(&s, 256 - 4 - i, 4));
            for (j = 0; j < 4; j++) {
                rustsecp256k1_v0_4_0_scalar_add(&n, &n, &n);
            }
            rustsecp256k1_v0_4_0_scalar_add(&n, &n, &t);
        }
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&n, &s));
    }

    {
        /* Test that fetching groups of randomly-sized bits from a scalar and recursing n(i)=b*n(i-1)+p(i) reconstructs it. */
        rustsecp256k1_v0_4_0_scalar n;
        int i = 0;
        rustsecp256k1_v0_4_0_scalar_set_int(&n, 0);
        while (i < 256) {
            rustsecp256k1_v0_4_0_scalar t;
            int j;
            int now = rustsecp256k1_v0_4_0_testrand_int(15) + 1;
            if (now + i > 256) {
                now = 256 - i;
            }
            rustsecp256k1_v0_4_0_scalar_set_int(&t, rustsecp256k1_v0_4_0_scalar_get_bits_var(&s, 256 - now - i, now));
            for (j = 0; j < now; j++) {
                rustsecp256k1_v0_4_0_scalar_add(&n, &n, &n);
            }
            rustsecp256k1_v0_4_0_scalar_add(&n, &n, &t);
            i += now;
        }
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&n, &s));
    }

#ifndef USE_NUM_NONE
    {
        /* Test that adding the scalars together is equal to adding their numbers together modulo the order. */
        rustsecp256k1_v0_4_0_num rnum;
        rustsecp256k1_v0_4_0_num r2num;
        rustsecp256k1_v0_4_0_scalar r;
        rustsecp256k1_v0_4_0_num_add(&rnum, &snum, &s2num);
        rustsecp256k1_v0_4_0_num_mod(&rnum, &order);
        rustsecp256k1_v0_4_0_scalar_add(&r, &s, &s2);
        rustsecp256k1_v0_4_0_scalar_get_num(&r2num, &r);
        CHECK(rustsecp256k1_v0_4_0_num_eq(&rnum, &r2num));
    }

    {
        /* Test that multiplying the scalars is equal to multiplying their numbers modulo the order. */
        rustsecp256k1_v0_4_0_scalar r;
        rustsecp256k1_v0_4_0_num r2num;
        rustsecp256k1_v0_4_0_num rnum;
        rustsecp256k1_v0_4_0_num_mul(&rnum, &snum, &s2num);
        rustsecp256k1_v0_4_0_num_mod(&rnum, &order);
        rustsecp256k1_v0_4_0_scalar_mul(&r, &s, &s2);
        rustsecp256k1_v0_4_0_scalar_get_num(&r2num, &r);
        CHECK(rustsecp256k1_v0_4_0_num_eq(&rnum, &r2num));
        /* The result can only be zero if at least one of the factors was zero. */
        CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&r) == (rustsecp256k1_v0_4_0_scalar_is_zero(&s) || rustsecp256k1_v0_4_0_scalar_is_zero(&s2)));
        /* The results can only be equal to one of the factors if that factor was zero, or the other factor was one. */
        CHECK(rustsecp256k1_v0_4_0_num_eq(&rnum, &snum) == (rustsecp256k1_v0_4_0_scalar_is_zero(&s) || rustsecp256k1_v0_4_0_scalar_is_one(&s2)));
        CHECK(rustsecp256k1_v0_4_0_num_eq(&rnum, &s2num) == (rustsecp256k1_v0_4_0_scalar_is_zero(&s2) || rustsecp256k1_v0_4_0_scalar_is_one(&s)));
    }

    {
        rustsecp256k1_v0_4_0_scalar neg;
        rustsecp256k1_v0_4_0_num negnum;
        rustsecp256k1_v0_4_0_num negnum2;
        /* Check that comparison with zero matches comparison with zero on the number. */
        CHECK(rustsecp256k1_v0_4_0_num_is_zero(&snum) == rustsecp256k1_v0_4_0_scalar_is_zero(&s));
        /* Check that comparison with the half order is equal to testing for high scalar. */
        CHECK(rustsecp256k1_v0_4_0_scalar_is_high(&s) == (rustsecp256k1_v0_4_0_num_cmp(&snum, &half_order) > 0));
        rustsecp256k1_v0_4_0_scalar_negate(&neg, &s);
        rustsecp256k1_v0_4_0_num_sub(&negnum, &order, &snum);
        rustsecp256k1_v0_4_0_num_mod(&negnum, &order);
        /* Check that comparison with the half order is equal to testing for high scalar after negation. */
        CHECK(rustsecp256k1_v0_4_0_scalar_is_high(&neg) == (rustsecp256k1_v0_4_0_num_cmp(&negnum, &half_order) > 0));
        /* Negating should change the high property, unless the value was already zero. */
        CHECK((rustsecp256k1_v0_4_0_scalar_is_high(&s) == rustsecp256k1_v0_4_0_scalar_is_high(&neg)) == rustsecp256k1_v0_4_0_scalar_is_zero(&s));
        rustsecp256k1_v0_4_0_scalar_get_num(&negnum2, &neg);
        /* Negating a scalar should be equal to (order - n) mod order on the number. */
        CHECK(rustsecp256k1_v0_4_0_num_eq(&negnum, &negnum2));
        rustsecp256k1_v0_4_0_scalar_add(&neg, &neg, &s);
        /* Adding a number to its negation should result in zero. */
        CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&neg));
        rustsecp256k1_v0_4_0_scalar_negate(&neg, &neg);
        /* Negating zero should still result in zero. */
        CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&neg));
    }

    {
        /* Test rustsecp256k1_v0_4_0_scalar_mul_shift_var. */
        rustsecp256k1_v0_4_0_scalar r;
        rustsecp256k1_v0_4_0_num one;
        rustsecp256k1_v0_4_0_num rnum;
        rustsecp256k1_v0_4_0_num rnum2;
        unsigned char cone[1] = {0x01};
        unsigned int shift = 256 + rustsecp256k1_v0_4_0_testrand_int(257);
        rustsecp256k1_v0_4_0_scalar_mul_shift_var(&r, &s1, &s2, shift);
        rustsecp256k1_v0_4_0_num_mul(&rnum, &s1num, &s2num);
        rustsecp256k1_v0_4_0_num_shift(&rnum, shift - 1);
        rustsecp256k1_v0_4_0_num_set_bin(&one, cone, 1);
        rustsecp256k1_v0_4_0_num_add(&rnum, &rnum, &one);
        rustsecp256k1_v0_4_0_num_shift(&rnum, 1);
        rustsecp256k1_v0_4_0_scalar_get_num(&rnum2, &r);
        CHECK(rustsecp256k1_v0_4_0_num_eq(&rnum, &rnum2));
    }

    {
        /* test rustsecp256k1_v0_4_0_scalar_shr_int */
        rustsecp256k1_v0_4_0_scalar r;
        int i;
        random_scalar_order_test(&r);
        for (i = 0; i < 100; ++i) {
            int low;
            int shift = 1 + rustsecp256k1_v0_4_0_testrand_int(15);
            int expected = r.d[0] % (1 << shift);
            low = rustsecp256k1_v0_4_0_scalar_shr_int(&r, shift);
            CHECK(expected == low);
        }
    }
#endif

    {
        /* Test that scalar inverses are equal to the inverse of their number modulo the order. */
        if (!rustsecp256k1_v0_4_0_scalar_is_zero(&s)) {
            rustsecp256k1_v0_4_0_scalar inv;
#ifndef USE_NUM_NONE
            rustsecp256k1_v0_4_0_num invnum;
            rustsecp256k1_v0_4_0_num invnum2;
#endif
            rustsecp256k1_v0_4_0_scalar_inverse(&inv, &s);
#ifndef USE_NUM_NONE
            rustsecp256k1_v0_4_0_num_mod_inverse(&invnum, &snum, &order);
            rustsecp256k1_v0_4_0_scalar_get_num(&invnum2, &inv);
            CHECK(rustsecp256k1_v0_4_0_num_eq(&invnum, &invnum2));
#endif
            rustsecp256k1_v0_4_0_scalar_mul(&inv, &inv, &s);
            /* Multiplying a scalar with its inverse must result in one. */
            CHECK(rustsecp256k1_v0_4_0_scalar_is_one(&inv));
            rustsecp256k1_v0_4_0_scalar_inverse(&inv, &inv);
            /* Inverting one must result in one. */
            CHECK(rustsecp256k1_v0_4_0_scalar_is_one(&inv));
#ifndef USE_NUM_NONE
            rustsecp256k1_v0_4_0_scalar_get_num(&invnum, &inv);
            CHECK(rustsecp256k1_v0_4_0_num_is_one(&invnum));
#endif
        }
    }

    {
        /* Test commutativity of add. */
        rustsecp256k1_v0_4_0_scalar r1, r2;
        rustsecp256k1_v0_4_0_scalar_add(&r1, &s1, &s2);
        rustsecp256k1_v0_4_0_scalar_add(&r2, &s2, &s1);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &r2));
    }

    {
        rustsecp256k1_v0_4_0_scalar r1, r2;
        rustsecp256k1_v0_4_0_scalar b;
        int i;
        /* Test add_bit. */
        int bit = rustsecp256k1_v0_4_0_testrand_bits(8);
        rustsecp256k1_v0_4_0_scalar_set_int(&b, 1);
        CHECK(rustsecp256k1_v0_4_0_scalar_is_one(&b));
        for (i = 0; i < bit; i++) {
            rustsecp256k1_v0_4_0_scalar_add(&b, &b, &b);
        }
        r1 = s1;
        r2 = s1;
        if (!rustsecp256k1_v0_4_0_scalar_add(&r1, &r1, &b)) {
            /* No overflow happened. */
            rustsecp256k1_v0_4_0_scalar_cadd_bit(&r2, bit, 1);
            CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &r2));
            /* cadd is a noop when flag is zero */
            rustsecp256k1_v0_4_0_scalar_cadd_bit(&r2, bit, 0);
            CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &r2));
        }
    }

    {
        /* Test commutativity of mul. */
        rustsecp256k1_v0_4_0_scalar r1, r2;
        rustsecp256k1_v0_4_0_scalar_mul(&r1, &s1, &s2);
        rustsecp256k1_v0_4_0_scalar_mul(&r2, &s2, &s1);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &r2));
    }

    {
        /* Test associativity of add. */
        rustsecp256k1_v0_4_0_scalar r1, r2;
        rustsecp256k1_v0_4_0_scalar_add(&r1, &s1, &s2);
        rustsecp256k1_v0_4_0_scalar_add(&r1, &r1, &s);
        rustsecp256k1_v0_4_0_scalar_add(&r2, &s2, &s);
        rustsecp256k1_v0_4_0_scalar_add(&r2, &s1, &r2);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &r2));
    }

    {
        /* Test associativity of mul. */
        rustsecp256k1_v0_4_0_scalar r1, r2;
        rustsecp256k1_v0_4_0_scalar_mul(&r1, &s1, &s2);
        rustsecp256k1_v0_4_0_scalar_mul(&r1, &r1, &s);
        rustsecp256k1_v0_4_0_scalar_mul(&r2, &s2, &s);
        rustsecp256k1_v0_4_0_scalar_mul(&r2, &s1, &r2);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &r2));
    }

    {
        /* Test distributitivity of mul over add. */
        rustsecp256k1_v0_4_0_scalar r1, r2, t;
        rustsecp256k1_v0_4_0_scalar_add(&r1, &s1, &s2);
        rustsecp256k1_v0_4_0_scalar_mul(&r1, &r1, &s);
        rustsecp256k1_v0_4_0_scalar_mul(&r2, &s1, &s);
        rustsecp256k1_v0_4_0_scalar_mul(&t, &s2, &s);
        rustsecp256k1_v0_4_0_scalar_add(&r2, &r2, &t);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &r2));
    }

    {
        /* Test square. */
        rustsecp256k1_v0_4_0_scalar r1, r2;
        rustsecp256k1_v0_4_0_scalar_sqr(&r1, &s1);
        rustsecp256k1_v0_4_0_scalar_mul(&r2, &s1, &s1);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &r2));
    }

    {
        /* Test multiplicative identity. */
        rustsecp256k1_v0_4_0_scalar r1, v1;
        rustsecp256k1_v0_4_0_scalar_set_int(&v1,1);
        rustsecp256k1_v0_4_0_scalar_mul(&r1, &s1, &v1);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &s1));
    }

    {
        /* Test additive identity. */
        rustsecp256k1_v0_4_0_scalar r1, v0;
        rustsecp256k1_v0_4_0_scalar_set_int(&v0,0);
        rustsecp256k1_v0_4_0_scalar_add(&r1, &s1, &v0);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &s1));
    }

    {
        /* Test zero product property. */
        rustsecp256k1_v0_4_0_scalar r1, v0;
        rustsecp256k1_v0_4_0_scalar_set_int(&v0,0);
        rustsecp256k1_v0_4_0_scalar_mul(&r1, &s1, &v0);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &v0));
    }

}

void run_scalar_set_b32_seckey_tests(void) {
    unsigned char b32[32];
    rustsecp256k1_v0_4_0_scalar s1;
    rustsecp256k1_v0_4_0_scalar s2;

    /* Usually set_b32 and set_b32_seckey give the same result */
    random_scalar_order_b32(b32);
    rustsecp256k1_v0_4_0_scalar_set_b32(&s1, b32, NULL);
    CHECK(rustsecp256k1_v0_4_0_scalar_set_b32_seckey(&s2, b32) == 1);
    CHECK(rustsecp256k1_v0_4_0_scalar_eq(&s1, &s2) == 1);

    memset(b32, 0, sizeof(b32));
    CHECK(rustsecp256k1_v0_4_0_scalar_set_b32_seckey(&s2, b32) == 0);
    memset(b32, 0xFF, sizeof(b32));
    CHECK(rustsecp256k1_v0_4_0_scalar_set_b32_seckey(&s2, b32) == 0);
}

void run_scalar_tests(void) {
    int i;
    for (i = 0; i < 128 * count; i++) {
        scalar_test();
    }
    for (i = 0; i < count; i++) {
        run_scalar_set_b32_seckey_tests();
    }

    {
        /* (-1)+1 should be zero. */
        rustsecp256k1_v0_4_0_scalar s, o;
        rustsecp256k1_v0_4_0_scalar_set_int(&s, 1);
        CHECK(rustsecp256k1_v0_4_0_scalar_is_one(&s));
        rustsecp256k1_v0_4_0_scalar_negate(&o, &s);
        rustsecp256k1_v0_4_0_scalar_add(&o, &o, &s);
        CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&o));
        rustsecp256k1_v0_4_0_scalar_negate(&o, &o);
        CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&o));
    }

#ifndef USE_NUM_NONE
    {
        /* Test rustsecp256k1_v0_4_0_scalar_set_b32 boundary conditions */
        rustsecp256k1_v0_4_0_num order;
        rustsecp256k1_v0_4_0_scalar scalar;
        unsigned char bin[32];
        unsigned char bin_tmp[32];
        int overflow = 0;
        /* 2^256-1 - order */
        static const rustsecp256k1_v0_4_0_scalar all_ones_minus_order = SECP256K1_SCALAR_CONST(
            0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000001UL,
            0x45512319UL, 0x50B75FC4UL, 0x402DA173UL, 0x2FC9BEBEUL
        );

        /* A scalar set to 0s should be 0. */
        memset(bin, 0, 32);
        rustsecp256k1_v0_4_0_scalar_set_b32(&scalar, bin, &overflow);
        CHECK(overflow == 0);
        CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&scalar));

        /* A scalar with value of the curve order should be 0. */
        rustsecp256k1_v0_4_0_scalar_order_get_num(&order);
        rustsecp256k1_v0_4_0_num_get_bin(bin, 32, &order);
        rustsecp256k1_v0_4_0_scalar_set_b32(&scalar, bin, &overflow);
        CHECK(overflow == 1);
        CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&scalar));

        /* A scalar with value of the curve order minus one should not overflow. */
        bin[31] -= 1;
        rustsecp256k1_v0_4_0_scalar_set_b32(&scalar, bin, &overflow);
        CHECK(overflow == 0);
        rustsecp256k1_v0_4_0_scalar_get_b32(bin_tmp, &scalar);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(bin, bin_tmp, 32) == 0);

        /* A scalar set to all 1s should overflow. */
        memset(bin, 0xFF, 32);
        rustsecp256k1_v0_4_0_scalar_set_b32(&scalar, bin, &overflow);
        CHECK(overflow == 1);
        CHECK(rustsecp256k1_v0_4_0_scalar_eq(&scalar, &all_ones_minus_order));
    }
#endif

    {
        /* Does check_overflow check catch all ones? */
        static const rustsecp256k1_v0_4_0_scalar overflowed = SECP256K1_SCALAR_CONST(
            0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
            0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL
        );
        CHECK(rustsecp256k1_v0_4_0_scalar_check_overflow(&overflowed));
    }

    {
        /* Static test vectors.
         * These were reduced from ~10^12 random vectors based on comparison-decision
         *  and edge-case coverage on 32-bit and 64-bit implementations.
         * The responses were generated with Sage 5.9.
         */
        rustsecp256k1_v0_4_0_scalar x;
        rustsecp256k1_v0_4_0_scalar y;
        rustsecp256k1_v0_4_0_scalar z;
        rustsecp256k1_v0_4_0_scalar zz;
        rustsecp256k1_v0_4_0_scalar one;
        rustsecp256k1_v0_4_0_scalar r1;
        rustsecp256k1_v0_4_0_scalar r2;
#if defined(USE_SCALAR_INV_NUM)
        rustsecp256k1_v0_4_0_scalar zzv;
#endif
        int overflow;
        unsigned char chal[33][2][32] = {
            {{0xff, 0xff, 0x03, 0x07, 0x00, 0x00, 0x00, 0x00,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03,
              0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0xff,
              0xff, 0xff, 0x03, 0x00, 0xc0, 0xff, 0xff, 0xff},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xff}},
            {{0xef, 0xff, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00,
              0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
             {0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0,
              0xff, 0xff, 0xff, 0xff, 0xfc, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x80, 0xff}},
            {{0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00,
              0x80, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00,
              0x00, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0x00},
             {0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff, 0x80,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0xe0,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff}},
            {{0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x00, 0x1e, 0xf8, 0xff, 0xff, 0xff, 0xfd, 0xff},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f,
              0x00, 0x00, 0x00, 0xf8, 0xff, 0x03, 0x00, 0xe0,
              0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xff,
              0xf3, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {{0x80, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0x00,
              0x00, 0x1c, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xe0, 0xff, 0xff, 0xff, 0x00,
              0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xff, 0xff},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00,
              0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0x1f, 0x00, 0x00, 0x80, 0xff, 0xff, 0x3f,
              0x00, 0xfe, 0xff, 0xff, 0xff, 0xdf, 0xff, 0xff}},
            {{0xff, 0xff, 0xff, 0xff, 0x00, 0x0f, 0xfc, 0x9f,
              0xff, 0xff, 0xff, 0x00, 0x80, 0x00, 0x00, 0x80,
              0xff, 0x0f, 0xfc, 0xff, 0x7f, 0x00, 0x00, 0x00,
              0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00},
             {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
              0x00, 0x00, 0xf8, 0xff, 0x0f, 0xc0, 0xff, 0xff,
              0xff, 0x1f, 0x00, 0x00, 0x00, 0xc0, 0xff, 0xff,
              0xff, 0xff, 0xff, 0x07, 0x80, 0xff, 0xff, 0xff}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00,
              0x80, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff,
              0xf7, 0xff, 0xff, 0xef, 0xff, 0xff, 0xff, 0x00,
              0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xf0},
             {0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
            {{0x00, 0xf8, 0xff, 0x03, 0xff, 0xff, 0xff, 0x00,
              0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x80, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0x03, 0xc0, 0xff, 0x0f, 0xfc, 0xff},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0xff, 0xff,
              0xff, 0x01, 0x00, 0x00, 0x00, 0x3f, 0x00, 0xc0,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
            {{0x8f, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0x7f, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {{0x00, 0x00, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0x03, 0x00, 0x80, 0x00, 0x00, 0x80,
              0xff, 0xff, 0xff, 0x00, 0x00, 0x80, 0xff, 0x7f},
             {0xff, 0xcf, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00,
              0x00, 0xc0, 0xff, 0xcf, 0xff, 0xff, 0xff, 0xff,
              0xbf, 0xff, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x80, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00}},
            {{0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff,
              0xff, 0xff, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0x00, 0x80, 0x00, 0x00, 0x80,
              0xff, 0x01, 0xfc, 0xff, 0x01, 0x00, 0xfe, 0xff},
             {0xff, 0xff, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00}},
            {{0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
              0xe0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0x7f, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0xf8, 0xff, 0x01, 0x00, 0xf0, 0xff, 0xff,
              0xe0, 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0x00},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
              0xfc, 0xff, 0xff, 0x3f, 0xf0, 0xff, 0xff, 0x3f,
              0x00, 0x00, 0xf8, 0x07, 0x00, 0x00, 0x00, 0xff,
              0xff, 0xff, 0xff, 0xff, 0x0f, 0x7e, 0x00, 0x00}},
            {{0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0x1f, 0x00, 0x00, 0xfe, 0x07, 0x00},
             {0x00, 0x00, 0x00, 0xf0, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xfb, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60}},
            {{0xff, 0x01, 0x00, 0xff, 0xff, 0xff, 0x0f, 0x00,
              0x80, 0x7f, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x03,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
             {0xff, 0xff, 0x1f, 0x00, 0xf0, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00}},
            {{0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf1, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03,
              0x00, 0x00, 0x00, 0xe0, 0xff, 0xff, 0xff, 0xff}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0xc0, 0xff, 0xff, 0xcf, 0xff, 0x1f, 0x00, 0x00,
              0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x7e,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x00},
             {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
              0xff, 0xff, 0x7f, 0x00, 0x80, 0x00, 0x00, 0x00,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x00, 0x00, 0xe0, 0xff, 0xff, 0xff, 0xff, 0xff}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x80,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00},
             {0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x80,
              0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
              0xff, 0x7f, 0xf8, 0xff, 0xff, 0x1f, 0x00, 0xfe}},
            {{0xff, 0xff, 0xff, 0x3f, 0xf8, 0xff, 0xff, 0xff,
              0xff, 0x03, 0xfe, 0x01, 0x00, 0x00, 0x00, 0x00,
              0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
              0xff, 0xff, 0xff, 0xff, 0x01, 0x80, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00}},
            {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
              0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
              0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x40}},
            {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {{0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
             {0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0xc0,
              0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00,
              0xf0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00,
              0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0x01, 0xff, 0xff, 0xff}},
            {{0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
              0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
              0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x40},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0x7e, 0x00, 0x00, 0xc0, 0xff, 0xff, 0x07, 0x00,
              0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
              0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
             {0xff, 0x01, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x80,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x00, 0x00,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
            {{0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x00,
              0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x00, 0xe0, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
              0x80, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xff,
              0xff, 0xff, 0x3f, 0x00, 0xf8, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0x3f, 0x00, 0x00, 0xc0, 0xf1, 0x7f, 0x00}},
            {{0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0xc0, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x80, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0x00},
             {0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff,
              0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x80, 0x1f,
              0x00, 0x00, 0xfc, 0xff, 0xff, 0x01, 0xff, 0xff}},
            {{0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x80, 0x00, 0x00, 0x80, 0xff, 0x03, 0xe0, 0x01,
              0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xfc, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00},
             {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
              0xfe, 0xff, 0xff, 0xf0, 0x07, 0x00, 0x3c, 0x80,
              0xff, 0xff, 0xff, 0xff, 0xfc, 0xff, 0xff, 0xff,
              0xff, 0xff, 0x07, 0xe0, 0xff, 0x00, 0x00, 0x00}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
              0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0xf8,
              0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80},
             {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x80, 0x00,
              0x00, 0x00, 0x00, 0xc0, 0x7f, 0xfe, 0xff, 0x1f,
              0x00, 0xfe, 0xff, 0x03, 0x00, 0x00, 0xfe, 0xff}},
            {{0xff, 0xff, 0x81, 0xff, 0xff, 0xff, 0xff, 0x00,
              0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x83,
              0xff, 0xff, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80,
              0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0xf0},
             {0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x00,
              0xf8, 0x07, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xc7, 0xff, 0xff, 0xe0, 0xff, 0xff, 0xff}},
            {{0x82, 0xc9, 0xfa, 0xb0, 0x68, 0x04, 0xa0, 0x00,
              0x82, 0xc9, 0xfa, 0xb0, 0x68, 0x04, 0xa0, 0x00,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x6f, 0x03, 0xfb,
              0xfa, 0x8a, 0x7d, 0xdf, 0x13, 0x86, 0xe2, 0x03},
             {0x82, 0xc9, 0xfa, 0xb0, 0x68, 0x04, 0xa0, 0x00,
              0x82, 0xc9, 0xfa, 0xb0, 0x68, 0x04, 0xa0, 0x00,
              0xff, 0xff, 0xff, 0xff, 0xff, 0x6f, 0x03, 0xfb,
              0xfa, 0x8a, 0x7d, 0xdf, 0x13, 0x86, 0xe2, 0x03}}
        };
        unsigned char res[33][2][32] = {
            {{0x0c, 0x3b, 0x0a, 0xca, 0x8d, 0x1a, 0x2f, 0xb9,
              0x8a, 0x7b, 0x53, 0x5a, 0x1f, 0xc5, 0x22, 0xa1,
              0x07, 0x2a, 0x48, 0xea, 0x02, 0xeb, 0xb3, 0xd6,
              0x20, 0x1e, 0x86, 0xd0, 0x95, 0xf6, 0x92, 0x35},
             {0xdc, 0x90, 0x7a, 0x07, 0x2e, 0x1e, 0x44, 0x6d,
              0xf8, 0x15, 0x24, 0x5b, 0x5a, 0x96, 0x37, 0x9c,
              0x37, 0x7b, 0x0d, 0xac, 0x1b, 0x65, 0x58, 0x49,
              0x43, 0xb7, 0x31, 0xbb, 0xa7, 0xf4, 0x97, 0x15}},
            {{0xf1, 0xf7, 0x3a, 0x50, 0xe6, 0x10, 0xba, 0x22,
              0x43, 0x4d, 0x1f, 0x1f, 0x7c, 0x27, 0xca, 0x9c,
              0xb8, 0xb6, 0xa0, 0xfc, 0xd8, 0xc0, 0x05, 0x2f,
              0xf7, 0x08, 0xe1, 0x76, 0xdd, 0xd0, 0x80, 0xc8},
             {0xe3, 0x80, 0x80, 0xb8, 0xdb, 0xe3, 0xa9, 0x77,
              0x00, 0xb0, 0xf5, 0x2e, 0x27, 0xe2, 0x68, 0xc4,
              0x88, 0xe8, 0x04, 0xc1, 0x12, 0xbf, 0x78, 0x59,
              0xe6, 0xa9, 0x7c, 0xe1, 0x81, 0xdd, 0xb9, 0xd5}},
            {{0x96, 0xe2, 0xee, 0x01, 0xa6, 0x80, 0x31, 0xef,
              0x5c, 0xd0, 0x19, 0xb4, 0x7d, 0x5f, 0x79, 0xab,
              0xa1, 0x97, 0xd3, 0x7e, 0x33, 0xbb, 0x86, 0x55,
              0x60, 0x20, 0x10, 0x0d, 0x94, 0x2d, 0x11, 0x7c},
             {0xcc, 0xab, 0xe0, 0xe8, 0x98, 0x65, 0x12, 0x96,
              0x38, 0x5a, 0x1a, 0xf2, 0x85, 0x23, 0x59, 0x5f,
              0xf9, 0xf3, 0xc2, 0x81, 0x70, 0x92, 0x65, 0x12,
              0x9c, 0x65, 0x1e, 0x96, 0x00, 0xef, 0xe7, 0x63}},
            {{0xac, 0x1e, 0x62, 0xc2, 0x59, 0xfc, 0x4e, 0x5c,
              0x83, 0xb0, 0xd0, 0x6f, 0xce, 0x19, 0xf6, 0xbf,
              0xa4, 0xb0, 0xe0, 0x53, 0x66, 0x1f, 0xbf, 0xc9,
              0x33, 0x47, 0x37, 0xa9, 0x3d, 0x5d, 0xb0, 0x48},
             {0x86, 0xb9, 0x2a, 0x7f, 0x8e, 0xa8, 0x60, 0x42,
              0x26, 0x6d, 0x6e, 0x1c, 0xa2, 0xec, 0xe0, 0xe5,
              0x3e, 0x0a, 0x33, 0xbb, 0x61, 0x4c, 0x9f, 0x3c,
              0xd1, 0xdf, 0x49, 0x33, 0xcd, 0x72, 0x78, 0x18}},
            {{0xf7, 0xd3, 0xcd, 0x49, 0x5c, 0x13, 0x22, 0xfb,
              0x2e, 0xb2, 0x2f, 0x27, 0xf5, 0x8a, 0x5d, 0x74,
              0xc1, 0x58, 0xc5, 0xc2, 0x2d, 0x9f, 0x52, 0xc6,
              0x63, 0x9f, 0xba, 0x05, 0x76, 0x45, 0x7a, 0x63},
             {0x8a, 0xfa, 0x55, 0x4d, 0xdd, 0xa3, 0xb2, 0xc3,
              0x44, 0xfd, 0xec, 0x72, 0xde, 0xef, 0xc0, 0x99,
              0xf5, 0x9f, 0xe2, 0x52, 0xb4, 0x05, 0x32, 0x58,
              0x57, 0xc1, 0x8f, 0xea, 0xc3, 0x24, 0x5b, 0x94}},
            {{0x05, 0x83, 0xee, 0xdd, 0x64, 0xf0, 0x14, 0x3b,
              0xa0, 0x14, 0x4a, 0x3a, 0x41, 0x82, 0x7c, 0xa7,
              0x2c, 0xaa, 0xb1, 0x76, 0xbb, 0x59, 0x64, 0x5f,
              0x52, 0xad, 0x25, 0x29, 0x9d, 0x8f, 0x0b, 0xb0},
             {0x7e, 0xe3, 0x7c, 0xca, 0xcd, 0x4f, 0xb0, 0x6d,
              0x7a, 0xb2, 0x3e, 0xa0, 0x08, 0xb9, 0xa8, 0x2d,
              0xc2, 0xf4, 0x99, 0x66, 0xcc, 0xac, 0xd8, 0xb9,
              0x72, 0x2a, 0x4a, 0x3e, 0x0f, 0x7b, 0xbf, 0xf4}},
            {{0x8c, 0x9c, 0x78, 0x2b, 0x39, 0x61, 0x7e, 0xf7,
              0x65, 0x37, 0x66, 0x09, 0x38, 0xb9, 0x6f, 0x70,
              0x78, 0x87, 0xff, 0xcf, 0x93, 0xca, 0x85, 0x06,
              0x44, 0x84, 0xa7, 0xfe, 0xd3, 0xa4, 0xe3, 0x7e},
             {0xa2, 0x56, 0x49, 0x23, 0x54, 0xa5, 0x50, 0xe9,
              0x5f, 0xf0, 0x4d, 0xe7, 0xdc, 0x38, 0x32, 0x79,
              0x4f, 0x1c, 0xb7, 0xe4, 0xbb, 0xf8, 0xbb, 0x2e,
              0x40, 0x41, 0x4b, 0xcc, 0xe3, 0x1e, 0x16, 0x36}},
            {{0x0c, 0x1e, 0xd7, 0x09, 0x25, 0x40, 0x97, 0xcb,
              0x5c, 0x46, 0xa8, 0xda, 0xef, 0x25, 0xd5, 0xe5,
              0x92, 0x4d, 0xcf, 0xa3, 0xc4, 0x5d, 0x35, 0x4a,
              0xe4, 0x61, 0x92, 0xf3, 0xbf, 0x0e, 0xcd, 0xbe},
             {0xe4, 0xaf, 0x0a, 0xb3, 0x30, 0x8b, 0x9b, 0x48,
              0x49, 0x43, 0xc7, 0x64, 0x60, 0x4a, 0x2b, 0x9e,
              0x95, 0x5f, 0x56, 0xe8, 0x35, 0xdc, 0xeb, 0xdc,
              0xc7, 0xc4, 0xfe, 0x30, 0x40, 0xc7, 0xbf, 0xa4}},
            {{0xd4, 0xa0, 0xf5, 0x81, 0x49, 0x6b, 0xb6, 0x8b,
              0x0a, 0x69, 0xf9, 0xfe, 0xa8, 0x32, 0xe5, 0xe0,
              0xa5, 0xcd, 0x02, 0x53, 0xf9, 0x2c, 0xe3, 0x53,
              0x83, 0x36, 0xc6, 0x02, 0xb5, 0xeb, 0x64, 0xb8},
             {0x1d, 0x42, 0xb9, 0xf9, 0xe9, 0xe3, 0x93, 0x2c,
              0x4c, 0xee, 0x6c, 0x5a, 0x47, 0x9e, 0x62, 0x01,
              0x6b, 0x04, 0xfe, 0xa4, 0x30, 0x2b, 0x0d, 0x4f,
              0x71, 0x10, 0xd3, 0x55, 0xca, 0xf3, 0x5e, 0x80}},
            {{0x77, 0x05, 0xf6, 0x0c, 0x15, 0x9b, 0x45, 0xe7,
              0xb9, 0x11, 0xb8, 0xf5, 0xd6, 0xda, 0x73, 0x0c,
              0xda, 0x92, 0xea, 0xd0, 0x9d, 0xd0, 0x18, 0x92,
              0xce, 0x9a, 0xaa, 0xee, 0x0f, 0xef, 0xde, 0x30},
             {0xf1, 0xf1, 0xd6, 0x9b, 0x51, 0xd7, 0x77, 0x62,
              0x52, 0x10, 0xb8, 0x7a, 0x84, 0x9d, 0x15, 0x4e,
              0x07, 0xdc, 0x1e, 0x75, 0x0d, 0x0c, 0x3b, 0xdb,
              0x74, 0x58, 0x62, 0x02, 0x90, 0x54, 0x8b, 0x43}},
            {{0xa6, 0xfe, 0x0b, 0x87, 0x80, 0x43, 0x67, 0x25,
              0x57, 0x5d, 0xec, 0x40, 0x50, 0x08, 0xd5, 0x5d,
              0x43, 0xd7, 0xe0, 0xaa, 0xe0, 0x13, 0xb6, 0xb0,
              0xc0, 0xd4, 0xe5, 0x0d, 0x45, 0x83, 0xd6, 0x13},
             {0x40, 0x45, 0x0a, 0x92, 0x31, 0xea, 0x8c, 0x60,
              0x8c, 0x1f, 0xd8, 0x76, 0x45, 0xb9, 0x29, 0x00,
              0x26, 0x32, 0xd8, 0xa6, 0x96, 0x88, 0xe2, 0xc4,
              0x8b, 0xdb, 0x7f, 0x17, 0x87, 0xcc, 0xc8, 0xf2}},
            {{0xc2, 0x56, 0xe2, 0xb6, 0x1a, 0x81, 0xe7, 0x31,
              0x63, 0x2e, 0xbb, 0x0d, 0x2f, 0x81, 0x67, 0xd4,
              0x22, 0xe2, 0x38, 0x02, 0x25, 0x97, 0xc7, 0x88,
              0x6e, 0xdf, 0xbe, 0x2a, 0xa5, 0x73, 0x63, 0xaa},
             {0x50, 0x45, 0xe2, 0xc3, 0xbd, 0x89, 0xfc, 0x57,
              0xbd, 0x3c, 0xa3, 0x98, 0x7e, 0x7f, 0x36, 0x38,
              0x92, 0x39, 0x1f, 0x0f, 0x81, 0x1a, 0x06, 0x51,
              0x1f, 0x8d, 0x6a, 0xff, 0x47, 0x16, 0x06, 0x9c}},
            {{0x33, 0x95, 0xa2, 0x6f, 0x27, 0x5f, 0x9c, 0x9c,
              0x64, 0x45, 0xcb, 0xd1, 0x3c, 0xee, 0x5e, 0x5f,
              0x48, 0xa6, 0xaf, 0xe3, 0x79, 0xcf, 0xb1, 0xe2,
              0xbf, 0x55, 0x0e, 0xa2, 0x3b, 0x62, 0xf0, 0xe4},
             {0x14, 0xe8, 0x06, 0xe3, 0xbe, 0x7e, 0x67, 0x01,
              0xc5, 0x21, 0x67, 0xd8, 0x54, 0xb5, 0x7f, 0xa4,
              0xf9, 0x75, 0x70, 0x1c, 0xfd, 0x79, 0xdb, 0x86,
              0xad, 0x37, 0x85, 0x83, 0x56, 0x4e, 0xf0, 0xbf}},
            {{0xbc, 0xa6, 0xe0, 0x56, 0x4e, 0xef, 0xfa, 0xf5,
              0x1d, 0x5d, 0x3f, 0x2a, 0x5b, 0x19, 0xab, 0x51,
              0xc5, 0x8b, 0xdd, 0x98, 0x28, 0x35, 0x2f, 0xc3,
              0x81, 0x4f, 0x5c, 0xe5, 0x70, 0xb9, 0xeb, 0x62},
             {0xc4, 0x6d, 0x26, 0xb0, 0x17, 0x6b, 0xfe, 0x6c,
              0x12, 0xf8, 0xe7, 0xc1, 0xf5, 0x2f, 0xfa, 0x91,
              0x13, 0x27, 0xbd, 0x73, 0xcc, 0x33, 0x31, 0x1c,
              0x39, 0xe3, 0x27, 0x6a, 0x95, 0xcf, 0xc5, 0xfb}},
            {{0x30, 0xb2, 0x99, 0x84, 0xf0, 0x18, 0x2a, 0x6e,
              0x1e, 0x27, 0xed, 0xa2, 0x29, 0x99, 0x41, 0x56,
              0xe8, 0xd4, 0x0d, 0xef, 0x99, 0x9c, 0xf3, 0x58,
              0x29, 0x55, 0x1a, 0xc0, 0x68, 0xd6, 0x74, 0xa4},
             {0x07, 0x9c, 0xe7, 0xec, 0xf5, 0x36, 0x73, 0x41,
              0xa3, 0x1c, 0xe5, 0x93, 0x97, 0x6a, 0xfd, 0xf7,
              0x53, 0x18, 0xab, 0xaf, 0xeb, 0x85, 0xbd, 0x92,
              0x90, 0xab, 0x3c, 0xbf, 0x30, 0x82, 0xad, 0xf6}},
            {{0xc6, 0x87, 0x8a, 0x2a, 0xea, 0xc0, 0xa9, 0xec,
              0x6d, 0xd3, 0xdc, 0x32, 0x23, 0xce, 0x62, 0x19,
              0xa4, 0x7e, 0xa8, 0xdd, 0x1c, 0x33, 0xae, 0xd3,
              0x4f, 0x62, 0x9f, 0x52, 0xe7, 0x65, 0x46, 0xf4},
             {0x97, 0x51, 0x27, 0x67, 0x2d, 0xa2, 0x82, 0x87,
              0x98, 0xd3, 0xb6, 0x14, 0x7f, 0x51, 0xd3, 0x9a,
              0x0b, 0xd0, 0x76, 0x81, 0xb2, 0x4f, 0x58, 0x92,
              0xa4, 0x86, 0xa1, 0xa7, 0x09, 0x1d, 0xef, 0x9b}},
            {{0xb3, 0x0f, 0x2b, 0x69, 0x0d, 0x06, 0x90, 0x64,
              0xbd, 0x43, 0x4c, 0x10, 0xe8, 0x98, 0x1c, 0xa3,
              0xe1, 0x68, 0xe9, 0x79, 0x6c, 0x29, 0x51, 0x3f,
              0x41, 0xdc, 0xdf, 0x1f, 0xf3, 0x60, 0xbe, 0x33},
             {0xa1, 0x5f, 0xf7, 0x1d, 0xb4, 0x3e, 0x9b, 0x3c,
              0xe7, 0xbd, 0xb6, 0x06, 0xd5, 0x60, 0x06, 0x6d,
              0x50, 0xd2, 0xf4, 0x1a, 0x31, 0x08, 0xf2, 0xea,
              0x8e, 0xef, 0x5f, 0x7d, 0xb6, 0xd0, 0xc0, 0x27}},
            {{0x62, 0x9a, 0xd9, 0xbb, 0x38, 0x36, 0xce, 0xf7,
              0x5d, 0x2f, 0x13, 0xec, 0xc8, 0x2d, 0x02, 0x8a,
              0x2e, 0x72, 0xf0, 0xe5, 0x15, 0x9d, 0x72, 0xae,
              0xfc, 0xb3, 0x4f, 0x02, 0xea, 0xe1, 0x09, 0xfe},
             {0x00, 0x00, 0x00, 0x00, 0xfa, 0x0a, 0x3d, 0xbc,
              0xad, 0x16, 0x0c, 0xb6, 0xe7, 0x7c, 0x8b, 0x39,
              0x9a, 0x43, 0xbb, 0xe3, 0xc2, 0x55, 0x15, 0x14,
              0x75, 0xac, 0x90, 0x9b, 0x7f, 0x9a, 0x92, 0x00}},
            {{0x8b, 0xac, 0x70, 0x86, 0x29, 0x8f, 0x00, 0x23,
              0x7b, 0x45, 0x30, 0xaa, 0xb8, 0x4c, 0xc7, 0x8d,
              0x4e, 0x47, 0x85, 0xc6, 0x19, 0xe3, 0x96, 0xc2,
              0x9a, 0xa0, 0x12, 0xed, 0x6f, 0xd7, 0x76, 0x16},
             {0x45, 0xaf, 0x7e, 0x33, 0xc7, 0x7f, 0x10, 0x6c,
              0x7c, 0x9f, 0x29, 0xc1, 0xa8, 0x7e, 0x15, 0x84,
              0xe7, 0x7d, 0xc0, 0x6d, 0xab, 0x71, 0x5d, 0xd0,
              0x6b, 0x9f, 0x97, 0xab, 0xcb, 0x51, 0x0c, 0x9f}},
            {{0x9e, 0xc3, 0x92, 0xb4, 0x04, 0x9f, 0xc8, 0xbb,
              0xdd, 0x9e, 0xc6, 0x05, 0xfd, 0x65, 0xec, 0x94,
              0x7f, 0x2c, 0x16, 0xc4, 0x40, 0xac, 0x63, 0x7b,
              0x7d, 0xb8, 0x0c, 0xe4, 0x5b, 0xe3, 0xa7, 0x0e},
             {0x43, 0xf4, 0x44, 0xe8, 0xcc, 0xc8, 0xd4, 0x54,
              0x33, 0x37, 0x50, 0xf2, 0x87, 0x42, 0x2e, 0x00,
              0x49, 0x60, 0x62, 0x02, 0xfd, 0x1a, 0x7c, 0xdb,
              0x29, 0x6c, 0x6d, 0x54, 0x53, 0x08, 0xd1, 0xc8}},
            {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
            {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}},
            {{0x27, 0x59, 0xc7, 0x35, 0x60, 0x71, 0xa6, 0xf1,
              0x79, 0xa5, 0xfd, 0x79, 0x16, 0xf3, 0x41, 0xf0,
              0x57, 0xb4, 0x02, 0x97, 0x32, 0xe7, 0xde, 0x59,
              0xe2, 0x2d, 0x9b, 0x11, 0xea, 0x2c, 0x35, 0x92},
             {0x27, 0x59, 0xc7, 0x35, 0x60, 0x71, 0xa6, 0xf1,
              0x79, 0xa5, 0xfd, 0x79, 0x16, 0xf3, 0x41, 0xf0,
              0x57, 0xb4, 0x02, 0x97, 0x32, 0xe7, 0xde, 0x59,
              0xe2, 0x2d, 0x9b, 0x11, 0xea, 0x2c, 0x35, 0x92}},
            {{0x28, 0x56, 0xac, 0x0e, 0x4f, 0x98, 0x09, 0xf0,
              0x49, 0xfa, 0x7f, 0x84, 0xac, 0x7e, 0x50, 0x5b,
              0x17, 0x43, 0x14, 0x89, 0x9c, 0x53, 0xa8, 0x94,
              0x30, 0xf2, 0x11, 0x4d, 0x92, 0x14, 0x27, 0xe8},
             {0x39, 0x7a, 0x84, 0x56, 0x79, 0x9d, 0xec, 0x26,
              0x2c, 0x53, 0xc1, 0x94, 0xc9, 0x8d, 0x9e, 0x9d,
              0x32, 0x1f, 0xdd, 0x84, 0x04, 0xe8, 0xe2, 0x0a,
              0x6b, 0xbe, 0xbb, 0x42, 0x40, 0x67, 0x30, 0x6c}},
            {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
              0x45, 0x51, 0x23, 0x19, 0x50, 0xb7, 0x5f, 0xc4,
              0x40, 0x2d, 0xa1, 0x73, 0x2f, 0xc9, 0xbe, 0xbd},
             {0x27, 0x59, 0xc7, 0x35, 0x60, 0x71, 0xa6, 0xf1,
              0x79, 0xa5, 0xfd, 0x79, 0x16, 0xf3, 0x41, 0xf0,
              0x57, 0xb4, 0x02, 0x97, 0x32, 0xe7, 0xde, 0x59,
              0xe2, 0x2d, 0x9b, 0x11, 0xea, 0x2c, 0x35, 0x92}},
            {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
              0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
              0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x40},
             {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}},
            {{0x1c, 0xc4, 0xf7, 0xda, 0x0f, 0x65, 0xca, 0x39,
              0x70, 0x52, 0x92, 0x8e, 0xc3, 0xc8, 0x15, 0xea,
              0x7f, 0x10, 0x9e, 0x77, 0x4b, 0x6e, 0x2d, 0xdf,
              0xe8, 0x30, 0x9d, 0xda, 0xe8, 0x9a, 0x65, 0xae},
             {0x02, 0xb0, 0x16, 0xb1, 0x1d, 0xc8, 0x57, 0x7b,
              0xa2, 0x3a, 0xa2, 0xa3, 0x38, 0x5c, 0x8f, 0xeb,
              0x66, 0x37, 0x91, 0xa8, 0x5f, 0xef, 0x04, 0xf6,
              0x59, 0x75, 0xe1, 0xee, 0x92, 0xf6, 0x0e, 0x30}},
            {{0x8d, 0x76, 0x14, 0xa4, 0x14, 0x06, 0x9f, 0x9a,
              0xdf, 0x4a, 0x85, 0xa7, 0x6b, 0xbf, 0x29, 0x6f,
              0xbc, 0x34, 0x87, 0x5d, 0xeb, 0xbb, 0x2e, 0xa9,
              0xc9, 0x1f, 0x58, 0xd6, 0x9a, 0x82, 0xa0, 0x56},
             {0xd4, 0xb9, 0xdb, 0x88, 0x1d, 0x04, 0xe9, 0x93,
              0x8d, 0x3f, 0x20, 0xd5, 0x86, 0xa8, 0x83, 0x07,
              0xdb, 0x09, 0xd8, 0x22, 0x1f, 0x7f, 0xf1, 0x71,
              0xc8, 0xe7, 0x5d, 0x47, 0xaf, 0x8b, 0x72, 0xe9}},
            {{0x83, 0xb9, 0x39, 0xb2, 0xa4, 0xdf, 0x46, 0x87,
              0xc2, 0xb8, 0xf1, 0xe6, 0x4c, 0xd1, 0xe2, 0xa9,
              0xe4, 0x70, 0x30, 0x34, 0xbc, 0x52, 0x7c, 0x55,
              0xa6, 0xec, 0x80, 0xa4, 0xe5, 0xd2, 0xdc, 0x73},
             {0x08, 0xf1, 0x03, 0xcf, 0x16, 0x73, 0xe8, 0x7d,
              0xb6, 0x7e, 0x9b, 0xc0, 0xb4, 0xc2, 0xa5, 0x86,
              0x02, 0x77, 0xd5, 0x27, 0x86, 0xa5, 0x15, 0xfb,
              0xae, 0x9b, 0x8c, 0xa9, 0xf9, 0xf8, 0xa8, 0x4a}},
            {{0x8b, 0x00, 0x49, 0xdb, 0xfa, 0xf0, 0x1b, 0xa2,
              0xed, 0x8a, 0x9a, 0x7a, 0x36, 0x78, 0x4a, 0xc7,
              0xf7, 0xad, 0x39, 0xd0, 0x6c, 0x65, 0x7a, 0x41,
              0xce, 0xd6, 0xd6, 0x4c, 0x20, 0x21, 0x6b, 0xc7},
             {0xc6, 0xca, 0x78, 0x1d, 0x32, 0x6c, 0x6c, 0x06,
              0x91, 0xf2, 0x1a, 0xe8, 0x43, 0x16, 0xea, 0x04,
              0x3c, 0x1f, 0x07, 0x85, 0xf7, 0x09, 0x22, 0x08,
              0xba, 0x13, 0xfd, 0x78, 0x1e, 0x3f, 0x6f, 0x62}},
            {{0x25, 0x9b, 0x7c, 0xb0, 0xac, 0x72, 0x6f, 0xb2,
              0xe3, 0x53, 0x84, 0x7a, 0x1a, 0x9a, 0x98, 0x9b,
              0x44, 0xd3, 0x59, 0xd0, 0x8e, 0x57, 0x41, 0x40,
              0x78, 0xa7, 0x30, 0x2f, 0x4c, 0x9c, 0xb9, 0x68},
             {0xb7, 0x75, 0x03, 0x63, 0x61, 0xc2, 0x48, 0x6e,
              0x12, 0x3d, 0xbf, 0x4b, 0x27, 0xdf, 0xb1, 0x7a,
              0xff, 0x4e, 0x31, 0x07, 0x83, 0xf4, 0x62, 0x5b,
              0x19, 0xa5, 0xac, 0xa0, 0x32, 0x58, 0x0d, 0xa7}},
            {{0x43, 0x4f, 0x10, 0xa4, 0xca, 0xdb, 0x38, 0x67,
              0xfa, 0xae, 0x96, 0xb5, 0x6d, 0x97, 0xff, 0x1f,
              0xb6, 0x83, 0x43, 0xd3, 0xa0, 0x2d, 0x70, 0x7a,
              0x64, 0x05, 0x4c, 0xa7, 0xc1, 0xa5, 0x21, 0x51},
             {0xe4, 0xf1, 0x23, 0x84, 0xe1, 0xb5, 0x9d, 0xf2,
              0xb8, 0x73, 0x8b, 0x45, 0x2b, 0x35, 0x46, 0x38,
              0x10, 0x2b, 0x50, 0xf8, 0x8b, 0x35, 0xcd, 0x34,
              0xc8, 0x0e, 0xf6, 0xdb, 0x09, 0x35, 0xf0, 0xda}},
            {{0xdb, 0x21, 0x5c, 0x8d, 0x83, 0x1d, 0xb3, 0x34,
              0xc7, 0x0e, 0x43, 0xa1, 0x58, 0x79, 0x67, 0x13,
              0x1e, 0x86, 0x5d, 0x89, 0x63, 0xe6, 0x0a, 0x46,
              0x5c, 0x02, 0x97, 0x1b, 0x62, 0x43, 0x86, 0xf5},
             {0xdb, 0x21, 0x5c, 0x8d, 0x83, 0x1d, 0xb3, 0x34,
              0xc7, 0x0e, 0x43, 0xa1, 0x58, 0x79, 0x67, 0x13,
              0x1e, 0x86, 0x5d, 0x89, 0x63, 0xe6, 0x0a, 0x46,
              0x5c, 0x02, 0x97, 0x1b, 0x62, 0x43, 0x86, 0xf5}}
        };
        rustsecp256k1_v0_4_0_scalar_set_int(&one, 1);
        for (i = 0; i < 33; i++) {
            rustsecp256k1_v0_4_0_scalar_set_b32(&x, chal[i][0], &overflow);
            CHECK(!overflow);
            rustsecp256k1_v0_4_0_scalar_set_b32(&y, chal[i][1], &overflow);
            CHECK(!overflow);
            rustsecp256k1_v0_4_0_scalar_set_b32(&r1, res[i][0], &overflow);
            CHECK(!overflow);
            rustsecp256k1_v0_4_0_scalar_set_b32(&r2, res[i][1], &overflow);
            CHECK(!overflow);
            rustsecp256k1_v0_4_0_scalar_mul(&z, &x, &y);
            CHECK(!rustsecp256k1_v0_4_0_scalar_check_overflow(&z));
            CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r1, &z));
            if (!rustsecp256k1_v0_4_0_scalar_is_zero(&y)) {
                rustsecp256k1_v0_4_0_scalar_inverse(&zz, &y);
                CHECK(!rustsecp256k1_v0_4_0_scalar_check_overflow(&zz));
#if defined(USE_SCALAR_INV_NUM)
                rustsecp256k1_v0_4_0_scalar_inverse_var(&zzv, &y);
                CHECK(rustsecp256k1_v0_4_0_scalar_eq(&zzv, &zz));
#endif
                rustsecp256k1_v0_4_0_scalar_mul(&z, &z, &zz);
                CHECK(!rustsecp256k1_v0_4_0_scalar_check_overflow(&z));
                CHECK(rustsecp256k1_v0_4_0_scalar_eq(&x, &z));
                rustsecp256k1_v0_4_0_scalar_mul(&zz, &zz, &y);
                CHECK(!rustsecp256k1_v0_4_0_scalar_check_overflow(&zz));
                CHECK(rustsecp256k1_v0_4_0_scalar_eq(&one, &zz));
            }
            rustsecp256k1_v0_4_0_scalar_mul(&z, &x, &x);
            CHECK(!rustsecp256k1_v0_4_0_scalar_check_overflow(&z));
            rustsecp256k1_v0_4_0_scalar_sqr(&zz, &x);
            CHECK(!rustsecp256k1_v0_4_0_scalar_check_overflow(&zz));
            CHECK(rustsecp256k1_v0_4_0_scalar_eq(&zz, &z));
            CHECK(rustsecp256k1_v0_4_0_scalar_eq(&r2, &zz));
        }
    }
}

/***** FIELD TESTS *****/

void random_fe(rustsecp256k1_v0_4_0_fe *x) {
    unsigned char bin[32];
    do {
        rustsecp256k1_v0_4_0_testrand256(bin);
        if (rustsecp256k1_v0_4_0_fe_set_b32(x, bin)) {
            return;
        }
    } while(1);
}

void random_fe_test(rustsecp256k1_v0_4_0_fe *x) {
    unsigned char bin[32];
    do {
        rustsecp256k1_v0_4_0_testrand256_test(bin);
        if (rustsecp256k1_v0_4_0_fe_set_b32(x, bin)) {
            return;
        }
    } while(1);
}

void random_fe_non_zero(rustsecp256k1_v0_4_0_fe *nz) {
    int tries = 10;
    while (--tries >= 0) {
        random_fe(nz);
        rustsecp256k1_v0_4_0_fe_normalize(nz);
        if (!rustsecp256k1_v0_4_0_fe_is_zero(nz)) {
            break;
        }
    }
    /* Infinitesimal probability of spurious failure here */
    CHECK(tries >= 0);
}

void random_fe_non_square(rustsecp256k1_v0_4_0_fe *ns) {
    rustsecp256k1_v0_4_0_fe r;
    random_fe_non_zero(ns);
    if (rustsecp256k1_v0_4_0_fe_sqrt(&r, ns)) {
        rustsecp256k1_v0_4_0_fe_negate(ns, ns, 1);
    }
}

int check_fe_equal(const rustsecp256k1_v0_4_0_fe *a, const rustsecp256k1_v0_4_0_fe *b) {
    rustsecp256k1_v0_4_0_fe an = *a;
    rustsecp256k1_v0_4_0_fe bn = *b;
    rustsecp256k1_v0_4_0_fe_normalize_weak(&an);
    rustsecp256k1_v0_4_0_fe_normalize_var(&bn);
    return rustsecp256k1_v0_4_0_fe_equal_var(&an, &bn);
}

int check_fe_inverse(const rustsecp256k1_v0_4_0_fe *a, const rustsecp256k1_v0_4_0_fe *ai) {
    rustsecp256k1_v0_4_0_fe x;
    rustsecp256k1_v0_4_0_fe one = SECP256K1_FE_CONST(0, 0, 0, 0, 0, 0, 0, 1);
    rustsecp256k1_v0_4_0_fe_mul(&x, a, ai);
    return check_fe_equal(&x, &one);
}

void run_field_convert(void) {
    static const unsigned char b32[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
        0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x40
    };
    static const rustsecp256k1_v0_4_0_fe_storage fes = SECP256K1_FE_STORAGE_CONST(
        0x00010203UL, 0x04050607UL, 0x11121314UL, 0x15161718UL,
        0x22232425UL, 0x26272829UL, 0x33343536UL, 0x37383940UL
    );
    static const rustsecp256k1_v0_4_0_fe fe = SECP256K1_FE_CONST(
        0x00010203UL, 0x04050607UL, 0x11121314UL, 0x15161718UL,
        0x22232425UL, 0x26272829UL, 0x33343536UL, 0x37383940UL
    );
    rustsecp256k1_v0_4_0_fe fe2;
    unsigned char b322[32];
    rustsecp256k1_v0_4_0_fe_storage fes2;
    /* Check conversions to fe. */
    CHECK(rustsecp256k1_v0_4_0_fe_set_b32(&fe2, b32));
    CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&fe, &fe2));
    rustsecp256k1_v0_4_0_fe_from_storage(&fe2, &fes);
    CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&fe, &fe2));
    /* Check conversion from fe. */
    rustsecp256k1_v0_4_0_fe_get_b32(b322, &fe);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(b322, b32, 32) == 0);
    rustsecp256k1_v0_4_0_fe_to_storage(&fes2, &fe);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&fes2, &fes, sizeof(fes)) == 0);
}

int fe_rustsecp256k1_v0_4_0_memcmp_var(const rustsecp256k1_v0_4_0_fe *a, const rustsecp256k1_v0_4_0_fe *b) {
    rustsecp256k1_v0_4_0_fe t = *b;
#ifdef VERIFY
    t.magnitude = a->magnitude;
    t.normalized = a->normalized;
#endif
    return rustsecp256k1_v0_4_0_memcmp_var(a, &t, sizeof(rustsecp256k1_v0_4_0_fe));
}

void run_field_misc(void) {
    rustsecp256k1_v0_4_0_fe x;
    rustsecp256k1_v0_4_0_fe y;
    rustsecp256k1_v0_4_0_fe z;
    rustsecp256k1_v0_4_0_fe q;
    rustsecp256k1_v0_4_0_fe fe5 = SECP256K1_FE_CONST(0, 0, 0, 0, 0, 0, 0, 5);
    int i, j;
    for (i = 0; i < 5*count; i++) {
        rustsecp256k1_v0_4_0_fe_storage xs, ys, zs;
        random_fe(&x);
        random_fe_non_zero(&y);
        /* Test the fe equality and comparison operations. */
        CHECK(rustsecp256k1_v0_4_0_fe_cmp_var(&x, &x) == 0);
        CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&x, &x));
        z = x;
        rustsecp256k1_v0_4_0_fe_add(&z,&y);
        /* Test fe conditional move; z is not normalized here. */
        q = x;
        rustsecp256k1_v0_4_0_fe_cmov(&x, &z, 0);
#ifdef VERIFY
        CHECK(x.normalized && x.magnitude == 1);
#endif
        rustsecp256k1_v0_4_0_fe_cmov(&x, &x, 1);
        CHECK(fe_rustsecp256k1_v0_4_0_memcmp_var(&x, &z) != 0);
        CHECK(fe_rustsecp256k1_v0_4_0_memcmp_var(&x, &q) == 0);
        rustsecp256k1_v0_4_0_fe_cmov(&q, &z, 1);
#ifdef VERIFY
        CHECK(!q.normalized && q.magnitude == z.magnitude);
#endif
        CHECK(fe_rustsecp256k1_v0_4_0_memcmp_var(&q, &z) == 0);
        rustsecp256k1_v0_4_0_fe_normalize_var(&x);
        rustsecp256k1_v0_4_0_fe_normalize_var(&z);
        CHECK(!rustsecp256k1_v0_4_0_fe_equal_var(&x, &z));
        rustsecp256k1_v0_4_0_fe_normalize_var(&q);
        rustsecp256k1_v0_4_0_fe_cmov(&q, &z, (i&1));
#ifdef VERIFY
        CHECK(q.normalized && q.magnitude == 1);
#endif
        for (j = 0; j < 6; j++) {
            rustsecp256k1_v0_4_0_fe_negate(&z, &z, j+1);
            rustsecp256k1_v0_4_0_fe_normalize_var(&q);
            rustsecp256k1_v0_4_0_fe_cmov(&q, &z, (j&1));
#ifdef VERIFY
            CHECK((q.normalized != (j&1)) && q.magnitude == ((j&1) ? z.magnitude : 1));
#endif
        }
        rustsecp256k1_v0_4_0_fe_normalize_var(&z);
        /* Test storage conversion and conditional moves. */
        rustsecp256k1_v0_4_0_fe_to_storage(&xs, &x);
        rustsecp256k1_v0_4_0_fe_to_storage(&ys, &y);
        rustsecp256k1_v0_4_0_fe_to_storage(&zs, &z);
        rustsecp256k1_v0_4_0_fe_storage_cmov(&zs, &xs, 0);
        rustsecp256k1_v0_4_0_fe_storage_cmov(&zs, &zs, 1);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(&xs, &zs, sizeof(xs)) != 0);
        rustsecp256k1_v0_4_0_fe_storage_cmov(&ys, &xs, 1);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(&xs, &ys, sizeof(xs)) == 0);
        rustsecp256k1_v0_4_0_fe_from_storage(&x, &xs);
        rustsecp256k1_v0_4_0_fe_from_storage(&y, &ys);
        rustsecp256k1_v0_4_0_fe_from_storage(&z, &zs);
        /* Test that mul_int, mul, and add agree. */
        rustsecp256k1_v0_4_0_fe_add(&y, &x);
        rustsecp256k1_v0_4_0_fe_add(&y, &x);
        z = x;
        rustsecp256k1_v0_4_0_fe_mul_int(&z, 3);
        CHECK(check_fe_equal(&y, &z));
        rustsecp256k1_v0_4_0_fe_add(&y, &x);
        rustsecp256k1_v0_4_0_fe_add(&z, &x);
        CHECK(check_fe_equal(&z, &y));
        z = x;
        rustsecp256k1_v0_4_0_fe_mul_int(&z, 5);
        rustsecp256k1_v0_4_0_fe_mul(&q, &x, &fe5);
        CHECK(check_fe_equal(&z, &q));
        rustsecp256k1_v0_4_0_fe_negate(&x, &x, 1);
        rustsecp256k1_v0_4_0_fe_add(&z, &x);
        rustsecp256k1_v0_4_0_fe_add(&q, &x);
        CHECK(check_fe_equal(&y, &z));
        CHECK(check_fe_equal(&q, &y));
    }
}

void run_field_inv(void) {
    rustsecp256k1_v0_4_0_fe x, xi, xii;
    int i;
    for (i = 0; i < 10*count; i++) {
        random_fe_non_zero(&x);
        rustsecp256k1_v0_4_0_fe_inv(&xi, &x);
        CHECK(check_fe_inverse(&x, &xi));
        rustsecp256k1_v0_4_0_fe_inv(&xii, &xi);
        CHECK(check_fe_equal(&x, &xii));
    }
}

void run_field_inv_var(void) {
    rustsecp256k1_v0_4_0_fe x, xi, xii;
    int i;
    for (i = 0; i < 10*count; i++) {
        random_fe_non_zero(&x);
        rustsecp256k1_v0_4_0_fe_inv_var(&xi, &x);
        CHECK(check_fe_inverse(&x, &xi));
        rustsecp256k1_v0_4_0_fe_inv_var(&xii, &xi);
        CHECK(check_fe_equal(&x, &xii));
    }
}

void run_field_inv_all_var(void) {
    rustsecp256k1_v0_4_0_fe x[16], xi[16], xii[16];
    int i;
    /* Check it's safe to call for 0 elements */
    rustsecp256k1_v0_4_0_fe_inv_all_var(xi, x, 0);
    for (i = 0; i < count; i++) {
        size_t j;
        size_t len = rustsecp256k1_v0_4_0_testrand_int(15) + 1;
        for (j = 0; j < len; j++) {
            random_fe_non_zero(&x[j]);
        }
        rustsecp256k1_v0_4_0_fe_inv_all_var(xi, x, len);
        for (j = 0; j < len; j++) {
            CHECK(check_fe_inverse(&x[j], &xi[j]));
        }
        rustsecp256k1_v0_4_0_fe_inv_all_var(xii, xi, len);
        for (j = 0; j < len; j++) {
            CHECK(check_fe_equal(&x[j], &xii[j]));
        }
    }
}

void run_sqr(void) {
    rustsecp256k1_v0_4_0_fe x, s;

    {
        int i;
        rustsecp256k1_v0_4_0_fe_set_int(&x, 1);
        rustsecp256k1_v0_4_0_fe_negate(&x, &x, 1);

        for (i = 1; i <= 512; ++i) {
            rustsecp256k1_v0_4_0_fe_mul_int(&x, 2);
            rustsecp256k1_v0_4_0_fe_normalize(&x);
            rustsecp256k1_v0_4_0_fe_sqr(&s, &x);
        }
    }
}

void test_sqrt(const rustsecp256k1_v0_4_0_fe *a, const rustsecp256k1_v0_4_0_fe *k) {
    rustsecp256k1_v0_4_0_fe r1, r2;
    int v = rustsecp256k1_v0_4_0_fe_sqrt(&r1, a);
    CHECK((v == 0) == (k == NULL));

    if (k != NULL) {
        /* Check that the returned root is +/- the given known answer */
        rustsecp256k1_v0_4_0_fe_negate(&r2, &r1, 1);
        rustsecp256k1_v0_4_0_fe_add(&r1, k); rustsecp256k1_v0_4_0_fe_add(&r2, k);
        rustsecp256k1_v0_4_0_fe_normalize(&r1); rustsecp256k1_v0_4_0_fe_normalize(&r2);
        CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&r1) || rustsecp256k1_v0_4_0_fe_is_zero(&r2));
    }
}

void run_sqrt(void) {
    rustsecp256k1_v0_4_0_fe ns, x, s, t;
    int i;

    /* Check sqrt(0) is 0 */
    rustsecp256k1_v0_4_0_fe_set_int(&x, 0);
    rustsecp256k1_v0_4_0_fe_sqr(&s, &x);
    test_sqrt(&s, &x);

    /* Check sqrt of small squares (and their negatives) */
    for (i = 1; i <= 100; i++) {
        rustsecp256k1_v0_4_0_fe_set_int(&x, i);
        rustsecp256k1_v0_4_0_fe_sqr(&s, &x);
        test_sqrt(&s, &x);
        rustsecp256k1_v0_4_0_fe_negate(&t, &s, 1);
        test_sqrt(&t, NULL);
    }

    /* Consistency checks for large random values */
    for (i = 0; i < 10; i++) {
        int j;
        random_fe_non_square(&ns);
        for (j = 0; j < count; j++) {
            random_fe(&x);
            rustsecp256k1_v0_4_0_fe_sqr(&s, &x);
            test_sqrt(&s, &x);
            rustsecp256k1_v0_4_0_fe_negate(&t, &s, 1);
            test_sqrt(&t, NULL);
            rustsecp256k1_v0_4_0_fe_mul(&t, &s, &ns);
            test_sqrt(&t, NULL);
        }
    }
}

/***** GROUP TESTS *****/

void ge_equals_ge(const rustsecp256k1_v0_4_0_ge *a, const rustsecp256k1_v0_4_0_ge *b) {
    CHECK(a->infinity == b->infinity);
    if (a->infinity) {
        return;
    }
    CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&a->x, &b->x));
    CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&a->y, &b->y));
}

/* This compares jacobian points including their Z, not just their geometric meaning. */
int gej_xyz_equals_gej(const rustsecp256k1_v0_4_0_gej *a, const rustsecp256k1_v0_4_0_gej *b) {
    rustsecp256k1_v0_4_0_gej a2;
    rustsecp256k1_v0_4_0_gej b2;
    int ret = 1;
    ret &= a->infinity == b->infinity;
    if (ret && !a->infinity) {
        a2 = *a;
        b2 = *b;
        rustsecp256k1_v0_4_0_fe_normalize(&a2.x);
        rustsecp256k1_v0_4_0_fe_normalize(&a2.y);
        rustsecp256k1_v0_4_0_fe_normalize(&a2.z);
        rustsecp256k1_v0_4_0_fe_normalize(&b2.x);
        rustsecp256k1_v0_4_0_fe_normalize(&b2.y);
        rustsecp256k1_v0_4_0_fe_normalize(&b2.z);
        ret &= rustsecp256k1_v0_4_0_fe_cmp_var(&a2.x, &b2.x) == 0;
        ret &= rustsecp256k1_v0_4_0_fe_cmp_var(&a2.y, &b2.y) == 0;
        ret &= rustsecp256k1_v0_4_0_fe_cmp_var(&a2.z, &b2.z) == 0;
    }
    return ret;
}

void ge_equals_gej(const rustsecp256k1_v0_4_0_ge *a, const rustsecp256k1_v0_4_0_gej *b) {
    rustsecp256k1_v0_4_0_fe z2s;
    rustsecp256k1_v0_4_0_fe u1, u2, s1, s2;
    CHECK(a->infinity == b->infinity);
    if (a->infinity) {
        return;
    }
    /* Check a.x * b.z^2 == b.x && a.y * b.z^3 == b.y, to avoid inverses. */
    rustsecp256k1_v0_4_0_fe_sqr(&z2s, &b->z);
    rustsecp256k1_v0_4_0_fe_mul(&u1, &a->x, &z2s);
    u2 = b->x; rustsecp256k1_v0_4_0_fe_normalize_weak(&u2);
    rustsecp256k1_v0_4_0_fe_mul(&s1, &a->y, &z2s); rustsecp256k1_v0_4_0_fe_mul(&s1, &s1, &b->z);
    s2 = b->y; rustsecp256k1_v0_4_0_fe_normalize_weak(&s2);
    CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&u1, &u2));
    CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&s1, &s2));
}

void test_ge(void) {
    int i, i1;
    int runs = 6;
    /* 25 points are used:
     * - infinity
     * - for each of four random points p1 p2 p3 p4, we add the point, its
     *   negation, and then those two again but with randomized Z coordinate.
     * - The same is then done for lambda*p1 and lambda^2*p1.
     */
    rustsecp256k1_v0_4_0_ge *ge = (rustsecp256k1_v0_4_0_ge *)checked_malloc(&ctx->error_callback, sizeof(rustsecp256k1_v0_4_0_ge) * (1 + 4 * runs));
    rustsecp256k1_v0_4_0_gej *gej = (rustsecp256k1_v0_4_0_gej *)checked_malloc(&ctx->error_callback, sizeof(rustsecp256k1_v0_4_0_gej) * (1 + 4 * runs));
    rustsecp256k1_v0_4_0_fe *zinv = (rustsecp256k1_v0_4_0_fe *)checked_malloc(&ctx->error_callback, sizeof(rustsecp256k1_v0_4_0_fe) * (1 + 4 * runs));
    rustsecp256k1_v0_4_0_fe zf;
    rustsecp256k1_v0_4_0_fe zfi2, zfi3;

    rustsecp256k1_v0_4_0_gej_set_infinity(&gej[0]);
    rustsecp256k1_v0_4_0_ge_clear(&ge[0]);
    rustsecp256k1_v0_4_0_ge_set_gej_var(&ge[0], &gej[0]);
    for (i = 0; i < runs; i++) {
        int j;
        rustsecp256k1_v0_4_0_ge g;
        random_group_element_test(&g);
        if (i >= runs - 2) {
            rustsecp256k1_v0_4_0_ge_mul_lambda(&g, &ge[1]);
        }
        if (i >= runs - 1) {
            rustsecp256k1_v0_4_0_ge_mul_lambda(&g, &g);
        }
        ge[1 + 4 * i] = g;
        ge[2 + 4 * i] = g;
        rustsecp256k1_v0_4_0_ge_neg(&ge[3 + 4 * i], &g);
        rustsecp256k1_v0_4_0_ge_neg(&ge[4 + 4 * i], &g);
        rustsecp256k1_v0_4_0_gej_set_ge(&gej[1 + 4 * i], &ge[1 + 4 * i]);
        random_group_element_jacobian_test(&gej[2 + 4 * i], &ge[2 + 4 * i]);
        rustsecp256k1_v0_4_0_gej_set_ge(&gej[3 + 4 * i], &ge[3 + 4 * i]);
        random_group_element_jacobian_test(&gej[4 + 4 * i], &ge[4 + 4 * i]);
        for (j = 0; j < 4; j++) {
            random_field_element_magnitude(&ge[1 + j + 4 * i].x);
            random_field_element_magnitude(&ge[1 + j + 4 * i].y);
            random_field_element_magnitude(&gej[1 + j + 4 * i].x);
            random_field_element_magnitude(&gej[1 + j + 4 * i].y);
            random_field_element_magnitude(&gej[1 + j + 4 * i].z);
        }
    }

    /* Compute z inverses. */
    {
        rustsecp256k1_v0_4_0_fe *zs = checked_malloc(&ctx->error_callback, sizeof(rustsecp256k1_v0_4_0_fe) * (1 + 4 * runs));
        for (i = 0; i < 4 * runs + 1; i++) {
            if (i == 0) {
                /* The point at infinity does not have a meaningful z inverse. Any should do. */
                do {
                    random_field_element_test(&zs[i]);
                } while(rustsecp256k1_v0_4_0_fe_is_zero(&zs[i]));
            } else {
                zs[i] = gej[i].z;
            }
        }
        rustsecp256k1_v0_4_0_fe_inv_all_var(zinv, zs, 4 * runs + 1);
        free(zs);
    }

    /* Generate random zf, and zfi2 = 1/zf^2, zfi3 = 1/zf^3 */
    do {
        random_field_element_test(&zf);
    } while(rustsecp256k1_v0_4_0_fe_is_zero(&zf));
    random_field_element_magnitude(&zf);
    rustsecp256k1_v0_4_0_fe_inv_var(&zfi3, &zf);
    rustsecp256k1_v0_4_0_fe_sqr(&zfi2, &zfi3);
    rustsecp256k1_v0_4_0_fe_mul(&zfi3, &zfi3, &zfi2);

    for (i1 = 0; i1 < 1 + 4 * runs; i1++) {
        int i2;
        for (i2 = 0; i2 < 1 + 4 * runs; i2++) {
            /* Compute reference result using gej + gej (var). */
            rustsecp256k1_v0_4_0_gej refj, resj;
            rustsecp256k1_v0_4_0_ge ref;
            rustsecp256k1_v0_4_0_fe zr;
            rustsecp256k1_v0_4_0_gej_add_var(&refj, &gej[i1], &gej[i2], rustsecp256k1_v0_4_0_gej_is_infinity(&gej[i1]) ? NULL : &zr);
            /* Check Z ratio. */
            if (!rustsecp256k1_v0_4_0_gej_is_infinity(&gej[i1]) && !rustsecp256k1_v0_4_0_gej_is_infinity(&refj)) {
                rustsecp256k1_v0_4_0_fe zrz; rustsecp256k1_v0_4_0_fe_mul(&zrz, &zr, &gej[i1].z);
                CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&zrz, &refj.z));
            }
            rustsecp256k1_v0_4_0_ge_set_gej_var(&ref, &refj);

            /* Test gej + ge with Z ratio result (var). */
            rustsecp256k1_v0_4_0_gej_add_ge_var(&resj, &gej[i1], &ge[i2], rustsecp256k1_v0_4_0_gej_is_infinity(&gej[i1]) ? NULL : &zr);
            ge_equals_gej(&ref, &resj);
            if (!rustsecp256k1_v0_4_0_gej_is_infinity(&gej[i1]) && !rustsecp256k1_v0_4_0_gej_is_infinity(&resj)) {
                rustsecp256k1_v0_4_0_fe zrz; rustsecp256k1_v0_4_0_fe_mul(&zrz, &zr, &gej[i1].z);
                CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&zrz, &resj.z));
            }

            /* Test gej + ge (var, with additional Z factor). */
            {
                rustsecp256k1_v0_4_0_ge ge2_zfi = ge[i2]; /* the second term with x and y rescaled for z = 1/zf */
                rustsecp256k1_v0_4_0_fe_mul(&ge2_zfi.x, &ge2_zfi.x, &zfi2);
                rustsecp256k1_v0_4_0_fe_mul(&ge2_zfi.y, &ge2_zfi.y, &zfi3);
                random_field_element_magnitude(&ge2_zfi.x);
                random_field_element_magnitude(&ge2_zfi.y);
                rustsecp256k1_v0_4_0_gej_add_zinv_var(&resj, &gej[i1], &ge2_zfi, &zf);
                ge_equals_gej(&ref, &resj);
            }

            /* Test gej + ge (const). */
            if (i2 != 0) {
                /* rustsecp256k1_v0_4_0_gej_add_ge does not support its second argument being infinity. */
                rustsecp256k1_v0_4_0_gej_add_ge(&resj, &gej[i1], &ge[i2]);
                ge_equals_gej(&ref, &resj);
            }

            /* Test doubling (var). */
            if ((i1 == 0 && i2 == 0) || ((i1 + 3)/4 == (i2 + 3)/4 && ((i1 + 3)%4)/2 == ((i2 + 3)%4)/2)) {
                rustsecp256k1_v0_4_0_fe zr2;
                /* Normal doubling with Z ratio result. */
                rustsecp256k1_v0_4_0_gej_double_var(&resj, &gej[i1], &zr2);
                ge_equals_gej(&ref, &resj);
                /* Check Z ratio. */
                rustsecp256k1_v0_4_0_fe_mul(&zr2, &zr2, &gej[i1].z);
                CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&zr2, &resj.z));
                /* Normal doubling. */
                rustsecp256k1_v0_4_0_gej_double_var(&resj, &gej[i2], NULL);
                ge_equals_gej(&ref, &resj);
                /* Constant-time doubling. */
                rustsecp256k1_v0_4_0_gej_double(&resj, &gej[i2]);
                ge_equals_gej(&ref, &resj);
            }

            /* Test adding opposites. */
            if ((i1 == 0 && i2 == 0) || ((i1 + 3)/4 == (i2 + 3)/4 && ((i1 + 3)%4)/2 != ((i2 + 3)%4)/2)) {
                CHECK(rustsecp256k1_v0_4_0_ge_is_infinity(&ref));
            }

            /* Test adding infinity. */
            if (i1 == 0) {
                CHECK(rustsecp256k1_v0_4_0_ge_is_infinity(&ge[i1]));
                CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&gej[i1]));
                ge_equals_gej(&ref, &gej[i2]);
            }
            if (i2 == 0) {
                CHECK(rustsecp256k1_v0_4_0_ge_is_infinity(&ge[i2]));
                CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&gej[i2]));
                ge_equals_gej(&ref, &gej[i1]);
            }
        }
    }

    /* Test adding all points together in random order equals infinity. */
    {
        rustsecp256k1_v0_4_0_gej sum = SECP256K1_GEJ_CONST_INFINITY;
        rustsecp256k1_v0_4_0_gej *gej_shuffled = (rustsecp256k1_v0_4_0_gej *)checked_malloc(&ctx->error_callback, (4 * runs + 1) * sizeof(rustsecp256k1_v0_4_0_gej));
        for (i = 0; i < 4 * runs + 1; i++) {
            gej_shuffled[i] = gej[i];
        }
        for (i = 0; i < 4 * runs + 1; i++) {
            int swap = i + rustsecp256k1_v0_4_0_testrand_int(4 * runs + 1 - i);
            if (swap != i) {
                rustsecp256k1_v0_4_0_gej t = gej_shuffled[i];
                gej_shuffled[i] = gej_shuffled[swap];
                gej_shuffled[swap] = t;
            }
        }
        for (i = 0; i < 4 * runs + 1; i++) {
            rustsecp256k1_v0_4_0_gej_add_var(&sum, &sum, &gej_shuffled[i], NULL);
        }
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&sum));
        free(gej_shuffled);
    }

    /* Test batch gej -> ge conversion with and without known z ratios. */
    {
        rustsecp256k1_v0_4_0_fe *zr = (rustsecp256k1_v0_4_0_fe *)checked_malloc(&ctx->error_callback, (4 * runs + 1) * sizeof(rustsecp256k1_v0_4_0_fe));
        rustsecp256k1_v0_4_0_ge *ge_set_all = (rustsecp256k1_v0_4_0_ge *)checked_malloc(&ctx->error_callback, (4 * runs + 1) * sizeof(rustsecp256k1_v0_4_0_ge));
        for (i = 0; i < 4 * runs + 1; i++) {
            /* Compute gej[i + 1].z / gez[i].z (with gej[n].z taken to be 1). */
            if (i < 4 * runs) {
                rustsecp256k1_v0_4_0_fe_mul(&zr[i + 1], &zinv[i], &gej[i + 1].z);
            }
        }
        rustsecp256k1_v0_4_0_ge_set_all_gej_var(ge_set_all, gej, 4 * runs + 1);
        for (i = 0; i < 4 * runs + 1; i++) {
            rustsecp256k1_v0_4_0_fe s;
            random_fe_non_zero(&s);
            rustsecp256k1_v0_4_0_gej_rescale(&gej[i], &s);
            ge_equals_gej(&ge_set_all[i], &gej[i]);
        }
        free(ge_set_all);
        free(zr);
    }

    /* Test batch gej -> ge conversion with many infinities. */
    for (i = 0; i < 4 * runs + 1; i++) {
        random_group_element_test(&ge[i]);
        /* randomly set half the points to infinity */
        if(rustsecp256k1_v0_4_0_fe_is_odd(&ge[i].x)) {
            rustsecp256k1_v0_4_0_ge_set_infinity(&ge[i]);
        }
        rustsecp256k1_v0_4_0_gej_set_ge(&gej[i], &ge[i]);
    }
    /* batch invert */
    rustsecp256k1_v0_4_0_ge_set_all_gej_var(ge, gej, 4 * runs + 1);
    /* check result */
    for (i = 0; i < 4 * runs + 1; i++) {
        ge_equals_gej(&ge[i], &gej[i]);
    }

    free(ge);
    free(gej);
    free(zinv);
}


void test_intialized_inf(void) {
    rustsecp256k1_v0_4_0_ge p;
    rustsecp256k1_v0_4_0_gej pj, npj, infj1, infj2, infj3;
    rustsecp256k1_v0_4_0_fe zinv;

    /* Test that adding P+(-P) results in a fully initalized infinity*/
    random_group_element_test(&p);
    rustsecp256k1_v0_4_0_gej_set_ge(&pj, &p);
    rustsecp256k1_v0_4_0_gej_neg(&npj, &pj);

    rustsecp256k1_v0_4_0_gej_add_var(&infj1, &pj, &npj, NULL);
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&infj1));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj1.x));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj1.y));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj1.z));

    rustsecp256k1_v0_4_0_gej_add_ge_var(&infj2, &npj, &p, NULL);
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&infj2));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj2.x));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj2.y));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj2.z));

    rustsecp256k1_v0_4_0_fe_set_int(&zinv, 1);
    rustsecp256k1_v0_4_0_gej_add_zinv_var(&infj3, &npj, &p, &zinv);
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&infj3));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj3.x));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj3.y));
    CHECK(rustsecp256k1_v0_4_0_fe_is_zero(&infj3.z));


}

void test_add_neg_y_diff_x(void) {
    /* The point of this test is to check that we can add two points
     * whose y-coordinates are negatives of each other but whose x
     * coordinates differ. If the x-coordinates were the same, these
     * points would be negatives of each other and their sum is
     * infinity. This is cool because it "covers up" any degeneracy
     * in the addition algorithm that would cause the xy coordinates
     * of the sum to be wrong (since infinity has no xy coordinates).
     * HOWEVER, if the x-coordinates are different, infinity is the
     * wrong answer, and such degeneracies are exposed. This is the
     * root of https://github.com/bitcoin-core/secp256k1/issues/257
     * which this test is a regression test for.
     *
     * These points were generated in sage as
     * # secp256k1 params
     * F = FiniteField (0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F)
     * C = EllipticCurve ([F (0), F (7)])
     * G = C.lift_x(0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798)
     * N = FiniteField(G.order())
     *
     * # endomorphism values (lambda is 1^{1/3} in N, beta is 1^{1/3} in F)
     * x = polygen(N)
     * lam  = (1 - x^3).roots()[1][0]
     *
     * # random "bad pair"
     * P = C.random_element()
     * Q = -int(lam) * P
     * print "    P: %x %x" % P.xy()
     * print "    Q: %x %x" % Q.xy()
     * print "P + Q: %x %x" % (P + Q).xy()
     */
    rustsecp256k1_v0_4_0_gej aj = SECP256K1_GEJ_CONST(
        0x8d24cd95, 0x0a355af1, 0x3c543505, 0x44238d30,
        0x0643d79f, 0x05a59614, 0x2f8ec030, 0xd58977cb,
        0x001e337a, 0x38093dcd, 0x6c0f386d, 0x0b1293a8,
        0x4d72c879, 0xd7681924, 0x44e6d2f3, 0x9190117d
    );
    rustsecp256k1_v0_4_0_gej bj = SECP256K1_GEJ_CONST(
        0xc7b74206, 0x1f788cd9, 0xabd0937d, 0x164a0d86,
        0x95f6ff75, 0xf19a4ce9, 0xd013bd7b, 0xbf92d2a7,
        0xffe1cc85, 0xc7f6c232, 0x93f0c792, 0xf4ed6c57,
        0xb28d3786, 0x2897e6db, 0xbb192d0b, 0x6e6feab2
    );
    rustsecp256k1_v0_4_0_gej sumj = SECP256K1_GEJ_CONST(
        0x671a63c0, 0x3efdad4c, 0x389a7798, 0x24356027,
        0xb3d69010, 0x278625c3, 0x5c86d390, 0x184a8f7a,
        0x5f6409c2, 0x2ce01f2b, 0x511fd375, 0x25071d08,
        0xda651801, 0x70e95caf, 0x8f0d893c, 0xbed8fbbe
    );
    rustsecp256k1_v0_4_0_ge b;
    rustsecp256k1_v0_4_0_gej resj;
    rustsecp256k1_v0_4_0_ge res;
    rustsecp256k1_v0_4_0_ge_set_gej(&b, &bj);

    rustsecp256k1_v0_4_0_gej_add_var(&resj, &aj, &bj, NULL);
    rustsecp256k1_v0_4_0_ge_set_gej(&res, &resj);
    ge_equals_gej(&res, &sumj);

    rustsecp256k1_v0_4_0_gej_add_ge(&resj, &aj, &b);
    rustsecp256k1_v0_4_0_ge_set_gej(&res, &resj);
    ge_equals_gej(&res, &sumj);

    rustsecp256k1_v0_4_0_gej_add_ge_var(&resj, &aj, &b, NULL);
    rustsecp256k1_v0_4_0_ge_set_gej(&res, &resj);
    ge_equals_gej(&res, &sumj);
}

void run_ge(void) {
    int i;
    for (i = 0; i < count * 32; i++) {
        test_ge();
    }
    test_add_neg_y_diff_x();
    test_intialized_inf();
}

void test_ec_combine(void) {
    rustsecp256k1_v0_4_0_scalar sum = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 0);
    rustsecp256k1_v0_4_0_pubkey data[6];
    const rustsecp256k1_v0_4_0_pubkey* d[6];
    rustsecp256k1_v0_4_0_pubkey sd;
    rustsecp256k1_v0_4_0_pubkey sd2;
    rustsecp256k1_v0_4_0_gej Qj;
    rustsecp256k1_v0_4_0_ge Q;
    int i;
    for (i = 1; i <= 6; i++) {
        rustsecp256k1_v0_4_0_scalar s;
        random_scalar_order_test(&s);
        rustsecp256k1_v0_4_0_scalar_add(&sum, &sum, &s);
        rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &Qj, &s);
        rustsecp256k1_v0_4_0_ge_set_gej(&Q, &Qj);
        rustsecp256k1_v0_4_0_pubkey_save(&data[i - 1], &Q);
        d[i - 1] = &data[i - 1];
        rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &Qj, &sum);
        rustsecp256k1_v0_4_0_ge_set_gej(&Q, &Qj);
        rustsecp256k1_v0_4_0_pubkey_save(&sd, &Q);
        CHECK(rustsecp256k1_v0_4_0_ec_pubkey_combine(ctx, &sd2, d, i) == 1);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(&sd, &sd2, sizeof(sd)) == 0);
    }
}

void run_ec_combine(void) {
    int i;
    for (i = 0; i < count * 8; i++) {
         test_ec_combine();
    }
}

void test_group_decompress(const rustsecp256k1_v0_4_0_fe* x) {
    /* The input itself, normalized. */
    rustsecp256k1_v0_4_0_fe fex = *x;
    rustsecp256k1_v0_4_0_fe fez;
    /* Results of set_xquad_var, set_xo_var(..., 0), set_xo_var(..., 1). */
    rustsecp256k1_v0_4_0_ge ge_quad, ge_even, ge_odd;
    rustsecp256k1_v0_4_0_gej gej_quad;
    /* Return values of the above calls. */
    int res_quad, res_even, res_odd;

    rustsecp256k1_v0_4_0_fe_normalize_var(&fex);

    res_quad = rustsecp256k1_v0_4_0_ge_set_xquad(&ge_quad, &fex);
    res_even = rustsecp256k1_v0_4_0_ge_set_xo_var(&ge_even, &fex, 0);
    res_odd = rustsecp256k1_v0_4_0_ge_set_xo_var(&ge_odd, &fex, 1);

    CHECK(res_quad == res_even);
    CHECK(res_quad == res_odd);

    if (res_quad) {
        rustsecp256k1_v0_4_0_fe_normalize_var(&ge_quad.x);
        rustsecp256k1_v0_4_0_fe_normalize_var(&ge_odd.x);
        rustsecp256k1_v0_4_0_fe_normalize_var(&ge_even.x);
        rustsecp256k1_v0_4_0_fe_normalize_var(&ge_quad.y);
        rustsecp256k1_v0_4_0_fe_normalize_var(&ge_odd.y);
        rustsecp256k1_v0_4_0_fe_normalize_var(&ge_even.y);

        /* No infinity allowed. */
        CHECK(!ge_quad.infinity);
        CHECK(!ge_even.infinity);
        CHECK(!ge_odd.infinity);

        /* Check that the x coordinates check out. */
        CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&ge_quad.x, x));
        CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&ge_even.x, x));
        CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&ge_odd.x, x));

        /* Check that the Y coordinate result in ge_quad is a square. */
        CHECK(rustsecp256k1_v0_4_0_fe_is_quad_var(&ge_quad.y));

        /* Check odd/even Y in ge_odd, ge_even. */
        CHECK(rustsecp256k1_v0_4_0_fe_is_odd(&ge_odd.y));
        CHECK(!rustsecp256k1_v0_4_0_fe_is_odd(&ge_even.y));

        /* Check rustsecp256k1_v0_4_0_gej_has_quad_y_var. */
        rustsecp256k1_v0_4_0_gej_set_ge(&gej_quad, &ge_quad);
        CHECK(rustsecp256k1_v0_4_0_gej_has_quad_y_var(&gej_quad));
        do {
            random_fe_test(&fez);
        } while (rustsecp256k1_v0_4_0_fe_is_zero(&fez));
        rustsecp256k1_v0_4_0_gej_rescale(&gej_quad, &fez);
        CHECK(rustsecp256k1_v0_4_0_gej_has_quad_y_var(&gej_quad));
        rustsecp256k1_v0_4_0_gej_neg(&gej_quad, &gej_quad);
        CHECK(!rustsecp256k1_v0_4_0_gej_has_quad_y_var(&gej_quad));
        do {
            random_fe_test(&fez);
        } while (rustsecp256k1_v0_4_0_fe_is_zero(&fez));
        rustsecp256k1_v0_4_0_gej_rescale(&gej_quad, &fez);
        CHECK(!rustsecp256k1_v0_4_0_gej_has_quad_y_var(&gej_quad));
        rustsecp256k1_v0_4_0_gej_neg(&gej_quad, &gej_quad);
        CHECK(rustsecp256k1_v0_4_0_gej_has_quad_y_var(&gej_quad));
    }
}

void run_group_decompress(void) {
    int i;
    for (i = 0; i < count * 4; i++) {
        rustsecp256k1_v0_4_0_fe fe;
        random_fe_test(&fe);
        test_group_decompress(&fe);
    }
}

/***** ECMULT TESTS *****/

void run_ecmult_chain(void) {
    /* random starting point A (on the curve) */
    rustsecp256k1_v0_4_0_gej a = SECP256K1_GEJ_CONST(
        0x8b30bbe9, 0xae2a9906, 0x96b22f67, 0x0709dff3,
        0x727fd8bc, 0x04d3362c, 0x6c7bf458, 0xe2846004,
        0xa357ae91, 0x5c4a6528, 0x1309edf2, 0x0504740f,
        0x0eb33439, 0x90216b4f, 0x81063cb6, 0x5f2f7e0f
    );
    /* two random initial factors xn and gn */
    rustsecp256k1_v0_4_0_scalar xn = SECP256K1_SCALAR_CONST(
        0x84cc5452, 0xf7fde1ed, 0xb4d38a8c, 0xe9b1b84c,
        0xcef31f14, 0x6e569be9, 0x705d357a, 0x42985407
    );
    rustsecp256k1_v0_4_0_scalar gn = SECP256K1_SCALAR_CONST(
        0xa1e58d22, 0x553dcd42, 0xb2398062, 0x5d4c57a9,
        0x6e9323d4, 0x2b3152e5, 0xca2c3990, 0xedc7c9de
    );
    /* two small multipliers to be applied to xn and gn in every iteration: */
    static const rustsecp256k1_v0_4_0_scalar xf = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 0x1337);
    static const rustsecp256k1_v0_4_0_scalar gf = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 0x7113);
    /* accumulators with the resulting coefficients to A and G */
    rustsecp256k1_v0_4_0_scalar ae = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 1);
    rustsecp256k1_v0_4_0_scalar ge = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 0);
    /* actual points */
    rustsecp256k1_v0_4_0_gej x;
    rustsecp256k1_v0_4_0_gej x2;
    int i;

    /* the point being computed */
    x = a;
    for (i = 0; i < 200*count; i++) {
        /* in each iteration, compute X = xn*X + gn*G; */
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &x, &x, &xn, &gn);
        /* also compute ae and ge: the actual accumulated factors for A and G */
        /* if X was (ae*A+ge*G), xn*X + gn*G results in (xn*ae*A + (xn*ge+gn)*G) */
        rustsecp256k1_v0_4_0_scalar_mul(&ae, &ae, &xn);
        rustsecp256k1_v0_4_0_scalar_mul(&ge, &ge, &xn);
        rustsecp256k1_v0_4_0_scalar_add(&ge, &ge, &gn);
        /* modify xn and gn */
        rustsecp256k1_v0_4_0_scalar_mul(&xn, &xn, &xf);
        rustsecp256k1_v0_4_0_scalar_mul(&gn, &gn, &gf);

        /* verify */
        if (i == 19999) {
            /* expected result after 19999 iterations */
            rustsecp256k1_v0_4_0_gej rp = SECP256K1_GEJ_CONST(
                0xD6E96687, 0xF9B10D09, 0x2A6F3543, 0x9D86CEBE,
                0xA4535D0D, 0x409F5358, 0x6440BD74, 0xB933E830,
                0xB95CBCA2, 0xC77DA786, 0x539BE8FD, 0x53354D2D,
                0x3B4F566A, 0xE6580454, 0x07ED6015, 0xEE1B2A88
            );

            rustsecp256k1_v0_4_0_gej_neg(&rp, &rp);
            rustsecp256k1_v0_4_0_gej_add_var(&rp, &rp, &x, NULL);
            CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&rp));
        }
    }
    /* redo the computation, but directly with the resulting ae and ge coefficients: */
    rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &x2, &a, &ae, &ge);
    rustsecp256k1_v0_4_0_gej_neg(&x2, &x2);
    rustsecp256k1_v0_4_0_gej_add_var(&x2, &x2, &x, NULL);
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&x2));
}

void test_point_times_order(const rustsecp256k1_v0_4_0_gej *point) {
    /* X * (point + G) + (order-X) * (pointer + G) = 0 */
    rustsecp256k1_v0_4_0_scalar x;
    rustsecp256k1_v0_4_0_scalar nx;
    rustsecp256k1_v0_4_0_scalar zero = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 0);
    rustsecp256k1_v0_4_0_scalar one = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 1);
    rustsecp256k1_v0_4_0_gej res1, res2;
    rustsecp256k1_v0_4_0_ge res3;
    unsigned char pub[65];
    size_t psize = 65;
    random_scalar_order_test(&x);
    rustsecp256k1_v0_4_0_scalar_negate(&nx, &x);
    rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &res1, point, &x, &x); /* calc res1 = x * point + x * G; */
    rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &res2, point, &nx, &nx); /* calc res2 = (order - x) * point + (order - x) * G; */
    rustsecp256k1_v0_4_0_gej_add_var(&res1, &res1, &res2, NULL);
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&res1));
    rustsecp256k1_v0_4_0_ge_set_gej(&res3, &res1);
    CHECK(rustsecp256k1_v0_4_0_ge_is_infinity(&res3));
    CHECK(rustsecp256k1_v0_4_0_ge_is_valid_var(&res3) == 0);
    CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_serialize(&res3, pub, &psize, 0) == 0);
    psize = 65;
    CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_serialize(&res3, pub, &psize, 1) == 0);
    /* check zero/one edge cases */
    rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &res1, point, &zero, &zero);
    rustsecp256k1_v0_4_0_ge_set_gej(&res3, &res1);
    CHECK(rustsecp256k1_v0_4_0_ge_is_infinity(&res3));
    rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &res1, point, &one, &zero);
    rustsecp256k1_v0_4_0_ge_set_gej(&res3, &res1);
    ge_equals_gej(&res3, point);
    rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &res1, point, &zero, &one);
    rustsecp256k1_v0_4_0_ge_set_gej(&res3, &res1);
    ge_equals_ge(&res3, &rustsecp256k1_v0_4_0_ge_const_g);
}

/* These scalars reach large (in absolute value) outputs when fed to rustsecp256k1_v0_4_0_scalar_split_lambda.
 *
 * They are computed as:
 * - For a in [-2, -1, 0, 1, 2]:
 *   - For b in [-3, -1, 1, 3]:
 *     - Output (a*LAMBDA + (ORDER+b)/2) % ORDER
 */
static const rustsecp256k1_v0_4_0_scalar scalars_near_split_bounds[20] = {
    SECP256K1_SCALAR_CONST(0xd938a566, 0x7f479e3e, 0xb5b3c7fa, 0xefdb3749, 0x3aa0585c, 0xc5ea2367, 0xe1b660db, 0x0209e6fc),
    SECP256K1_SCALAR_CONST(0xd938a566, 0x7f479e3e, 0xb5b3c7fa, 0xefdb3749, 0x3aa0585c, 0xc5ea2367, 0xe1b660db, 0x0209e6fd),
    SECP256K1_SCALAR_CONST(0xd938a566, 0x7f479e3e, 0xb5b3c7fa, 0xefdb3749, 0x3aa0585c, 0xc5ea2367, 0xe1b660db, 0x0209e6fe),
    SECP256K1_SCALAR_CONST(0xd938a566, 0x7f479e3e, 0xb5b3c7fa, 0xefdb3749, 0x3aa0585c, 0xc5ea2367, 0xe1b660db, 0x0209e6ff),
    SECP256K1_SCALAR_CONST(0x2c9c52b3, 0x3fa3cf1f, 0x5ad9e3fd, 0x77ed9ba5, 0xb294b893, 0x3722e9a5, 0x00e698ca, 0x4cf7632d),
    SECP256K1_SCALAR_CONST(0x2c9c52b3, 0x3fa3cf1f, 0x5ad9e3fd, 0x77ed9ba5, 0xb294b893, 0x3722e9a5, 0x00e698ca, 0x4cf7632e),
    SECP256K1_SCALAR_CONST(0x2c9c52b3, 0x3fa3cf1f, 0x5ad9e3fd, 0x77ed9ba5, 0xb294b893, 0x3722e9a5, 0x00e698ca, 0x4cf7632f),
    SECP256K1_SCALAR_CONST(0x2c9c52b3, 0x3fa3cf1f, 0x5ad9e3fd, 0x77ed9ba5, 0xb294b893, 0x3722e9a5, 0x00e698ca, 0x4cf76330),
    SECP256K1_SCALAR_CONST(0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xd576e735, 0x57a4501d, 0xdfe92f46, 0x681b209f),
    SECP256K1_SCALAR_CONST(0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xd576e735, 0x57a4501d, 0xdfe92f46, 0x681b20a0),
    SECP256K1_SCALAR_CONST(0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xd576e735, 0x57a4501d, 0xdfe92f46, 0x681b20a1),
    SECP256K1_SCALAR_CONST(0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xd576e735, 0x57a4501d, 0xdfe92f46, 0x681b20a2),
    SECP256K1_SCALAR_CONST(0xd363ad4c, 0xc05c30e0, 0xa5261c02, 0x88126459, 0xf85915d7, 0x7825b696, 0xbeebc5c2, 0x833ede11),
    SECP256K1_SCALAR_CONST(0xd363ad4c, 0xc05c30e0, 0xa5261c02, 0x88126459, 0xf85915d7, 0x7825b696, 0xbeebc5c2, 0x833ede12),
    SECP256K1_SCALAR_CONST(0xd363ad4c, 0xc05c30e0, 0xa5261c02, 0x88126459, 0xf85915d7, 0x7825b696, 0xbeebc5c2, 0x833ede13),
    SECP256K1_SCALAR_CONST(0xd363ad4c, 0xc05c30e0, 0xa5261c02, 0x88126459, 0xf85915d7, 0x7825b696, 0xbeebc5c2, 0x833ede14),
    SECP256K1_SCALAR_CONST(0x26c75a99, 0x80b861c1, 0x4a4c3805, 0x1024c8b4, 0x704d760e, 0xe95e7cd3, 0xde1bfdb1, 0xce2c5a42),
    SECP256K1_SCALAR_CONST(0x26c75a99, 0x80b861c1, 0x4a4c3805, 0x1024c8b4, 0x704d760e, 0xe95e7cd3, 0xde1bfdb1, 0xce2c5a43),
    SECP256K1_SCALAR_CONST(0x26c75a99, 0x80b861c1, 0x4a4c3805, 0x1024c8b4, 0x704d760e, 0xe95e7cd3, 0xde1bfdb1, 0xce2c5a44),
    SECP256K1_SCALAR_CONST(0x26c75a99, 0x80b861c1, 0x4a4c3805, 0x1024c8b4, 0x704d760e, 0xe95e7cd3, 0xde1bfdb1, 0xce2c5a45)
};

void test_ecmult_target(const rustsecp256k1_v0_4_0_scalar* target, int mode) {
    /* Mode: 0=ecmult_gen, 1=ecmult, 2=ecmult_const */
    rustsecp256k1_v0_4_0_scalar n1, n2;
    rustsecp256k1_v0_4_0_ge p;
    rustsecp256k1_v0_4_0_gej pj, p1j, p2j, ptj;
    static const rustsecp256k1_v0_4_0_scalar zero = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 0);

    /* Generate random n1,n2 such that n1+n2 = -target. */
    random_scalar_order_test(&n1);
    rustsecp256k1_v0_4_0_scalar_add(&n2, &n1, target);
    rustsecp256k1_v0_4_0_scalar_negate(&n2, &n2);

    /* Generate a random input point. */
    if (mode != 0) {
        random_group_element_test(&p);
        rustsecp256k1_v0_4_0_gej_set_ge(&pj, &p);
    }

    /* EC multiplications */
    if (mode == 0) {
        rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &p1j, &n1);
        rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &p2j, &n2);
        rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &ptj, target);
    } else if (mode == 1) {
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &p1j, &pj, &n1, &zero);
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &p2j, &pj, &n2, &zero);
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &ptj, &pj, target, &zero);
    } else {
        rustsecp256k1_v0_4_0_ecmult_const(&p1j, &p, &n1, 256);
        rustsecp256k1_v0_4_0_ecmult_const(&p2j, &p, &n2, 256);
        rustsecp256k1_v0_4_0_ecmult_const(&ptj, &p, target, 256);
    }

    /* Add them all up: n1*P + n2*P + target*P = (n1+n2+target)*P = (n1+n1-n1-n2)*P = 0. */
    rustsecp256k1_v0_4_0_gej_add_var(&ptj, &ptj, &p1j, NULL);
    rustsecp256k1_v0_4_0_gej_add_var(&ptj, &ptj, &p2j, NULL);
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&ptj));
}

void run_ecmult_near_split_bound(void) {
    int i;
    unsigned j;
    for (i = 0; i < 4*count; ++i) {
        for (j = 0; j < sizeof(scalars_near_split_bounds) / sizeof(scalars_near_split_bounds[0]); ++j) {
            test_ecmult_target(&scalars_near_split_bounds[j], 0);
            test_ecmult_target(&scalars_near_split_bounds[j], 1);
            test_ecmult_target(&scalars_near_split_bounds[j], 2);
        }
    }
}

void run_point_times_order(void) {
    int i;
    rustsecp256k1_v0_4_0_fe x = SECP256K1_FE_CONST(0, 0, 0, 0, 0, 0, 0, 2);
    static const rustsecp256k1_v0_4_0_fe xr = SECP256K1_FE_CONST(
        0x7603CB59, 0xB0EF6C63, 0xFE608479, 0x2A0C378C,
        0xDB3233A8, 0x0F8A9A09, 0xA877DEAD, 0x31B38C45
    );
    for (i = 0; i < 500; i++) {
        rustsecp256k1_v0_4_0_ge p;
        if (rustsecp256k1_v0_4_0_ge_set_xo_var(&p, &x, 1)) {
            rustsecp256k1_v0_4_0_gej j;
            CHECK(rustsecp256k1_v0_4_0_ge_is_valid_var(&p));
            rustsecp256k1_v0_4_0_gej_set_ge(&j, &p);
            test_point_times_order(&j);
        }
        rustsecp256k1_v0_4_0_fe_sqr(&x, &x);
    }
    rustsecp256k1_v0_4_0_fe_normalize_var(&x);
    CHECK(rustsecp256k1_v0_4_0_fe_equal_var(&x, &xr));
}

void ecmult_const_random_mult(void) {
    /* random starting point A (on the curve) */
    rustsecp256k1_v0_4_0_ge a = SECP256K1_GE_CONST(
        0x6d986544, 0x57ff52b8, 0xcf1b8126, 0x5b802a5b,
        0xa97f9263, 0xb1e88044, 0x93351325, 0x91bc450a,
        0x535c59f7, 0x325e5d2b, 0xc391fbe8, 0x3c12787c,
        0x337e4a98, 0xe82a9011, 0x0123ba37, 0xdd769c7d
    );
    /* random initial factor xn */
    rustsecp256k1_v0_4_0_scalar xn = SECP256K1_SCALAR_CONST(
        0x649d4f77, 0xc4242df7, 0x7f2079c9, 0x14530327,
        0xa31b876a, 0xd2d8ce2a, 0x2236d5c6, 0xd7b2029b
    );
    /* expected xn * A (from sage) */
    rustsecp256k1_v0_4_0_ge expected_b = SECP256K1_GE_CONST(
        0x23773684, 0x4d209dc7, 0x098a786f, 0x20d06fcd,
        0x070a38bf, 0xc11ac651, 0x03004319, 0x1e2a8786,
        0xed8c3b8e, 0xc06dd57b, 0xd06ea66e, 0x45492b0f,
        0xb84e4e1b, 0xfb77e21f, 0x96baae2a, 0x63dec956
    );
    rustsecp256k1_v0_4_0_gej b;
    rustsecp256k1_v0_4_0_ecmult_const(&b, &a, &xn, 256);

    CHECK(rustsecp256k1_v0_4_0_ge_is_valid_var(&a));
    ge_equals_gej(&expected_b, &b);
}

void ecmult_const_commutativity(void) {
    rustsecp256k1_v0_4_0_scalar a;
    rustsecp256k1_v0_4_0_scalar b;
    rustsecp256k1_v0_4_0_gej res1;
    rustsecp256k1_v0_4_0_gej res2;
    rustsecp256k1_v0_4_0_ge mid1;
    rustsecp256k1_v0_4_0_ge mid2;
    random_scalar_order_test(&a);
    random_scalar_order_test(&b);

    rustsecp256k1_v0_4_0_ecmult_const(&res1, &rustsecp256k1_v0_4_0_ge_const_g, &a, 256);
    rustsecp256k1_v0_4_0_ecmult_const(&res2, &rustsecp256k1_v0_4_0_ge_const_g, &b, 256);
    rustsecp256k1_v0_4_0_ge_set_gej(&mid1, &res1);
    rustsecp256k1_v0_4_0_ge_set_gej(&mid2, &res2);
    rustsecp256k1_v0_4_0_ecmult_const(&res1, &mid1, &b, 256);
    rustsecp256k1_v0_4_0_ecmult_const(&res2, &mid2, &a, 256);
    rustsecp256k1_v0_4_0_ge_set_gej(&mid1, &res1);
    rustsecp256k1_v0_4_0_ge_set_gej(&mid2, &res2);
    ge_equals_ge(&mid1, &mid2);
}

void ecmult_const_mult_zero_one(void) {
    rustsecp256k1_v0_4_0_scalar zero = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 0);
    rustsecp256k1_v0_4_0_scalar one = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 1);
    rustsecp256k1_v0_4_0_scalar negone;
    rustsecp256k1_v0_4_0_gej res1;
    rustsecp256k1_v0_4_0_ge res2;
    rustsecp256k1_v0_4_0_ge point;
    rustsecp256k1_v0_4_0_scalar_negate(&negone, &one);

    random_group_element_test(&point);
    rustsecp256k1_v0_4_0_ecmult_const(&res1, &point, &zero, 3);
    rustsecp256k1_v0_4_0_ge_set_gej(&res2, &res1);
    CHECK(rustsecp256k1_v0_4_0_ge_is_infinity(&res2));
    rustsecp256k1_v0_4_0_ecmult_const(&res1, &point, &one, 2);
    rustsecp256k1_v0_4_0_ge_set_gej(&res2, &res1);
    ge_equals_ge(&res2, &point);
    rustsecp256k1_v0_4_0_ecmult_const(&res1, &point, &negone, 256);
    rustsecp256k1_v0_4_0_gej_neg(&res1, &res1);
    rustsecp256k1_v0_4_0_ge_set_gej(&res2, &res1);
    ge_equals_ge(&res2, &point);
}

void ecmult_const_chain_multiply(void) {
    /* Check known result (randomly generated test problem from sage) */
    const rustsecp256k1_v0_4_0_scalar scalar = SECP256K1_SCALAR_CONST(
        0x4968d524, 0x2abf9b7a, 0x466abbcf, 0x34b11b6d,
        0xcd83d307, 0x827bed62, 0x05fad0ce, 0x18fae63b
    );
    const rustsecp256k1_v0_4_0_gej expected_point = SECP256K1_GEJ_CONST(
        0x5494c15d, 0x32099706, 0xc2395f94, 0x348745fd,
        0x757ce30e, 0x4e8c90fb, 0xa2bad184, 0xf883c69f,
        0x5d195d20, 0xe191bf7f, 0x1be3e55f, 0x56a80196,
        0x6071ad01, 0xf1462f66, 0xc997fa94, 0xdb858435
    );
    rustsecp256k1_v0_4_0_gej point;
    rustsecp256k1_v0_4_0_ge res;
    int i;

    rustsecp256k1_v0_4_0_gej_set_ge(&point, &rustsecp256k1_v0_4_0_ge_const_g);
    for (i = 0; i < 100; ++i) {
        rustsecp256k1_v0_4_0_ge tmp;
        rustsecp256k1_v0_4_0_ge_set_gej(&tmp, &point);
        rustsecp256k1_v0_4_0_ecmult_const(&point, &tmp, &scalar, 256);
    }
    rustsecp256k1_v0_4_0_ge_set_gej(&res, &point);
    ge_equals_gej(&res, &expected_point);
}

void run_ecmult_const_tests(void) {
    ecmult_const_mult_zero_one();
    ecmult_const_random_mult();
    ecmult_const_commutativity();
    ecmult_const_chain_multiply();
}

typedef struct {
    rustsecp256k1_v0_4_0_scalar *sc;
    rustsecp256k1_v0_4_0_ge *pt;
} ecmult_multi_data;

static int ecmult_multi_callback(rustsecp256k1_v0_4_0_scalar *sc, rustsecp256k1_v0_4_0_ge *pt, size_t idx, void *cbdata) {
    ecmult_multi_data *data = (ecmult_multi_data*) cbdata;
    *sc = data->sc[idx];
    *pt = data->pt[idx];
    return 1;
}

static int ecmult_multi_false_callback(rustsecp256k1_v0_4_0_scalar *sc, rustsecp256k1_v0_4_0_ge *pt, size_t idx, void *cbdata) {
    (void)sc;
    (void)pt;
    (void)idx;
    (void)cbdata;
    return 0;
}

void test_ecmult_multi(rustsecp256k1_v0_4_0_scratch *scratch, rustsecp256k1_v0_4_0_ecmult_multi_func ecmult_multi) {
    int ncount;
    rustsecp256k1_v0_4_0_scalar szero;
    rustsecp256k1_v0_4_0_scalar sc[32];
    rustsecp256k1_v0_4_0_ge pt[32];
    rustsecp256k1_v0_4_0_gej r;
    rustsecp256k1_v0_4_0_gej r2;
    ecmult_multi_data data;

    data.sc = sc;
    data.pt = pt;
    rustsecp256k1_v0_4_0_scalar_set_int(&szero, 0);

    /* No points to multiply */
    CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, NULL, ecmult_multi_callback, &data, 0));

    /* Check 1- and 2-point multiplies against ecmult */
    for (ncount = 0; ncount < count; ncount++) {
        rustsecp256k1_v0_4_0_ge ptg;
        rustsecp256k1_v0_4_0_gej ptgj;
        random_scalar_order(&sc[0]);
        random_scalar_order(&sc[1]);

        random_group_element_test(&ptg);
        rustsecp256k1_v0_4_0_gej_set_ge(&ptgj, &ptg);
        pt[0] = ptg;
        pt[1] = rustsecp256k1_v0_4_0_ge_const_g;

        /* only G scalar */
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &r2, &ptgj, &szero, &sc[0]);
        CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &sc[0], ecmult_multi_callback, &data, 0));
        rustsecp256k1_v0_4_0_gej_neg(&r2, &r2);
        rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));

        /* 1-point */
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &r2, &ptgj, &sc[0], &szero);
        CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, 1));
        rustsecp256k1_v0_4_0_gej_neg(&r2, &r2);
        rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));

        /* Try to multiply 1 point, but callback returns false */
        CHECK(!ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_false_callback, &data, 1));

        /* 2-point */
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &r2, &ptgj, &sc[0], &sc[1]);
        CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, 2));
        rustsecp256k1_v0_4_0_gej_neg(&r2, &r2);
        rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));

        /* 2-point with G scalar */
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &r2, &ptgj, &sc[0], &sc[1]);
        CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &sc[1], ecmult_multi_callback, &data, 1));
        rustsecp256k1_v0_4_0_gej_neg(&r2, &r2);
        rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
    }

    /* Check infinite outputs of various forms */
    for (ncount = 0; ncount < count; ncount++) {
        rustsecp256k1_v0_4_0_ge ptg;
        size_t i, j;
        size_t sizes[] = { 2, 10, 32 };

        for (j = 0; j < 3; j++) {
            for (i = 0; i < 32; i++) {
                random_scalar_order(&sc[i]);
                rustsecp256k1_v0_4_0_ge_set_infinity(&pt[i]);
            }
            CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, sizes[j]));
            CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
        }

        for (j = 0; j < 3; j++) {
            for (i = 0; i < 32; i++) {
                random_group_element_test(&ptg);
                pt[i] = ptg;
                rustsecp256k1_v0_4_0_scalar_set_int(&sc[i], 0);
            }
            CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, sizes[j]));
            CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
        }

        for (j = 0; j < 3; j++) {
            random_group_element_test(&ptg);
            for (i = 0; i < 16; i++) {
                random_scalar_order(&sc[2*i]);
                rustsecp256k1_v0_4_0_scalar_negate(&sc[2*i + 1], &sc[2*i]);
                pt[2 * i] = ptg;
                pt[2 * i + 1] = ptg;
            }

            CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, sizes[j]));
            CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));

            random_scalar_order(&sc[0]);
            for (i = 0; i < 16; i++) {
                random_group_element_test(&ptg);

                sc[2*i] = sc[0];
                sc[2*i+1] = sc[0];
                pt[2 * i] = ptg;
                rustsecp256k1_v0_4_0_ge_neg(&pt[2*i+1], &pt[2*i]);
            }

            CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, sizes[j]));
            CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
        }

        random_group_element_test(&ptg);
        rustsecp256k1_v0_4_0_scalar_set_int(&sc[0], 0);
        pt[0] = ptg;
        for (i = 1; i < 32; i++) {
            pt[i] = ptg;

            random_scalar_order(&sc[i]);
            rustsecp256k1_v0_4_0_scalar_add(&sc[0], &sc[0], &sc[i]);
            rustsecp256k1_v0_4_0_scalar_negate(&sc[i], &sc[i]);
        }

        CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, 32));
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
    }

    /* Check random points, constant scalar */
    for (ncount = 0; ncount < count; ncount++) {
        size_t i;
        rustsecp256k1_v0_4_0_gej_set_infinity(&r);

        random_scalar_order(&sc[0]);
        for (i = 0; i < 20; i++) {
            rustsecp256k1_v0_4_0_ge ptg;
            sc[i] = sc[0];
            random_group_element_test(&ptg);
            pt[i] = ptg;
            rustsecp256k1_v0_4_0_gej_add_ge_var(&r, &r, &pt[i], NULL);
        }

        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &r2, &r, &sc[0], &szero);
        CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, 20));
        rustsecp256k1_v0_4_0_gej_neg(&r2, &r2);
        rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
    }

    /* Check random scalars, constant point */
    for (ncount = 0; ncount < count; ncount++) {
        size_t i;
        rustsecp256k1_v0_4_0_ge ptg;
        rustsecp256k1_v0_4_0_gej p0j;
        rustsecp256k1_v0_4_0_scalar rs;
        rustsecp256k1_v0_4_0_scalar_set_int(&rs, 0);

        random_group_element_test(&ptg);
        for (i = 0; i < 20; i++) {
            random_scalar_order(&sc[i]);
            pt[i] = ptg;
            rustsecp256k1_v0_4_0_scalar_add(&rs, &rs, &sc[i]);
        }

        rustsecp256k1_v0_4_0_gej_set_ge(&p0j, &pt[0]);
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &r2, &p0j, &rs, &szero);
        CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, 20));
        rustsecp256k1_v0_4_0_gej_neg(&r2, &r2);
        rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
    }

    /* Sanity check that zero scalars don't cause problems */
    for (ncount = 0; ncount < 20; ncount++) {
        random_scalar_order(&sc[ncount]);
        random_group_element_test(&pt[ncount]);
    }

    rustsecp256k1_v0_4_0_scalar_clear(&sc[0]);
    CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, 20));
    rustsecp256k1_v0_4_0_scalar_clear(&sc[1]);
    rustsecp256k1_v0_4_0_scalar_clear(&sc[2]);
    rustsecp256k1_v0_4_0_scalar_clear(&sc[3]);
    rustsecp256k1_v0_4_0_scalar_clear(&sc[4]);
    CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, 6));
    CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &szero, ecmult_multi_callback, &data, 5));
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));

    /* Run through s0*(t0*P) + s1*(t1*P) exhaustively for many small values of s0, s1, t0, t1 */
    {
        const size_t TOP = 8;
        size_t s0i, s1i;
        size_t t0i, t1i;
        rustsecp256k1_v0_4_0_ge ptg;
        rustsecp256k1_v0_4_0_gej ptgj;

        random_group_element_test(&ptg);
        rustsecp256k1_v0_4_0_gej_set_ge(&ptgj, &ptg);

        for(t0i = 0; t0i < TOP; t0i++) {
            for(t1i = 0; t1i < TOP; t1i++) {
                rustsecp256k1_v0_4_0_gej t0p, t1p;
                rustsecp256k1_v0_4_0_scalar t0, t1;

                rustsecp256k1_v0_4_0_scalar_set_int(&t0, (t0i + 1) / 2);
                rustsecp256k1_v0_4_0_scalar_cond_negate(&t0, t0i & 1);
                rustsecp256k1_v0_4_0_scalar_set_int(&t1, (t1i + 1) / 2);
                rustsecp256k1_v0_4_0_scalar_cond_negate(&t1, t1i & 1);

                rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &t0p, &ptgj, &t0, &szero);
                rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &t1p, &ptgj, &t1, &szero);

                for(s0i = 0; s0i < TOP; s0i++) {
                    for(s1i = 0; s1i < TOP; s1i++) {
                        rustsecp256k1_v0_4_0_scalar tmp1, tmp2;
                        rustsecp256k1_v0_4_0_gej expected, actual;

                        rustsecp256k1_v0_4_0_ge_set_gej(&pt[0], &t0p);
                        rustsecp256k1_v0_4_0_ge_set_gej(&pt[1], &t1p);

                        rustsecp256k1_v0_4_0_scalar_set_int(&sc[0], (s0i + 1) / 2);
                        rustsecp256k1_v0_4_0_scalar_cond_negate(&sc[0], s0i & 1);
                        rustsecp256k1_v0_4_0_scalar_set_int(&sc[1], (s1i + 1) / 2);
                        rustsecp256k1_v0_4_0_scalar_cond_negate(&sc[1], s1i & 1);

                        rustsecp256k1_v0_4_0_scalar_mul(&tmp1, &t0, &sc[0]);
                        rustsecp256k1_v0_4_0_scalar_mul(&tmp2, &t1, &sc[1]);
                        rustsecp256k1_v0_4_0_scalar_add(&tmp1, &tmp1, &tmp2);

                        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &expected, &ptgj, &tmp1, &szero);
                        CHECK(ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &actual, &szero, ecmult_multi_callback, &data, 2));
                        rustsecp256k1_v0_4_0_gej_neg(&expected, &expected);
                        rustsecp256k1_v0_4_0_gej_add_var(&actual, &actual, &expected, NULL);
                        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&actual));
                    }
                }
            }
        }
    }
}

void test_ecmult_multi_batch_single(rustsecp256k1_v0_4_0_ecmult_multi_func ecmult_multi) {
    rustsecp256k1_v0_4_0_scalar szero;
    rustsecp256k1_v0_4_0_scalar sc;
    rustsecp256k1_v0_4_0_ge pt;
    rustsecp256k1_v0_4_0_gej r;
    ecmult_multi_data data;
    rustsecp256k1_v0_4_0_scratch *scratch_empty;

    random_group_element_test(&pt);
    random_scalar_order(&sc);
    data.sc = &sc;
    data.pt = &pt;
    rustsecp256k1_v0_4_0_scalar_set_int(&szero, 0);

    /* Try to multiply 1 point, but scratch space is empty.*/
    scratch_empty = rustsecp256k1_v0_4_0_scratch_create(&ctx->error_callback, 0);
    CHECK(!ecmult_multi(&ctx->error_callback, &ctx->ecmult_ctx, scratch_empty, &r, &szero, ecmult_multi_callback, &data, 1));
    rustsecp256k1_v0_4_0_scratch_destroy(&ctx->error_callback, scratch_empty);
}

void test_rustsecp256k1_v0_4_0_pippenger_bucket_window_inv(void) {
    int i;

    CHECK(rustsecp256k1_v0_4_0_pippenger_bucket_window_inv(0) == 0);
    for(i = 1; i <= PIPPENGER_MAX_BUCKET_WINDOW; i++) {
        /* Bucket_window of 8 is not used with endo */
        if (i == 8) {
            continue;
        }
        CHECK(rustsecp256k1_v0_4_0_pippenger_bucket_window(rustsecp256k1_v0_4_0_pippenger_bucket_window_inv(i)) == i);
        if (i != PIPPENGER_MAX_BUCKET_WINDOW) {
            CHECK(rustsecp256k1_v0_4_0_pippenger_bucket_window(rustsecp256k1_v0_4_0_pippenger_bucket_window_inv(i)+1) > i);
        }
    }
}

/**
 * Probabilistically test the function returning the maximum number of possible points
 * for a given scratch space.
 */
void test_ecmult_multi_pippenger_max_points(void) {
    size_t scratch_size = rustsecp256k1_v0_4_0_testrand_int(256);
    size_t max_size = rustsecp256k1_v0_4_0_pippenger_scratch_size(rustsecp256k1_v0_4_0_pippenger_bucket_window_inv(PIPPENGER_MAX_BUCKET_WINDOW-1)+512, 12);
    rustsecp256k1_v0_4_0_scratch *scratch;
    size_t n_points_supported;
    int bucket_window = 0;

    for(; scratch_size < max_size; scratch_size+=256) {
        size_t i;
        size_t total_alloc;
        size_t checkpoint;
        scratch = rustsecp256k1_v0_4_0_scratch_create(&ctx->error_callback, scratch_size);
        CHECK(scratch != NULL);
        checkpoint = rustsecp256k1_v0_4_0_scratch_checkpoint(&ctx->error_callback, scratch);
        n_points_supported = rustsecp256k1_v0_4_0_pippenger_max_points(&ctx->error_callback, scratch);
        if (n_points_supported == 0) {
            rustsecp256k1_v0_4_0_scratch_destroy(&ctx->error_callback, scratch);
            continue;
        }
        bucket_window = rustsecp256k1_v0_4_0_pippenger_bucket_window(n_points_supported);
        /* allocate `total_alloc` bytes over `PIPPENGER_SCRATCH_OBJECTS` many allocations */
        total_alloc = rustsecp256k1_v0_4_0_pippenger_scratch_size(n_points_supported, bucket_window);
        for (i = 0; i < PIPPENGER_SCRATCH_OBJECTS - 1; i++) {
            CHECK(rustsecp256k1_v0_4_0_scratch_alloc(&ctx->error_callback, scratch, 1));
            total_alloc--;
        }
        CHECK(rustsecp256k1_v0_4_0_scratch_alloc(&ctx->error_callback, scratch, total_alloc));
        rustsecp256k1_v0_4_0_scratch_apply_checkpoint(&ctx->error_callback, scratch, checkpoint);
        rustsecp256k1_v0_4_0_scratch_destroy(&ctx->error_callback, scratch);
    }
    CHECK(bucket_window == PIPPENGER_MAX_BUCKET_WINDOW);
}

void test_ecmult_multi_batch_size_helper(void) {
    size_t n_batches, n_batch_points, max_n_batch_points, n;

    max_n_batch_points = 0;
    n = 1;
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_batch_size_helper(&n_batches, &n_batch_points, max_n_batch_points, n) == 0);

    max_n_batch_points = 1;
    n = 0;
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_batch_size_helper(&n_batches, &n_batch_points, max_n_batch_points, n) == 1);
    CHECK(n_batches == 0);
    CHECK(n_batch_points == 0);

    max_n_batch_points = 2;
    n = 5;
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_batch_size_helper(&n_batches, &n_batch_points, max_n_batch_points, n) == 1);
    CHECK(n_batches == 3);
    CHECK(n_batch_points == 2);

    max_n_batch_points = ECMULT_MAX_POINTS_PER_BATCH;
    n = ECMULT_MAX_POINTS_PER_BATCH;
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_batch_size_helper(&n_batches, &n_batch_points, max_n_batch_points, n) == 1);
    CHECK(n_batches == 1);
    CHECK(n_batch_points == ECMULT_MAX_POINTS_PER_BATCH);

    max_n_batch_points = ECMULT_MAX_POINTS_PER_BATCH + 1;
    n = ECMULT_MAX_POINTS_PER_BATCH + 1;
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_batch_size_helper(&n_batches, &n_batch_points, max_n_batch_points, n) == 1);
    CHECK(n_batches == 2);
    CHECK(n_batch_points == ECMULT_MAX_POINTS_PER_BATCH/2 + 1);

    max_n_batch_points = 1;
    n = SIZE_MAX;
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_batch_size_helper(&n_batches, &n_batch_points, max_n_batch_points, n) == 1);
    CHECK(n_batches == SIZE_MAX);
    CHECK(n_batch_points == 1);

    max_n_batch_points = 2;
    n = SIZE_MAX;
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_batch_size_helper(&n_batches, &n_batch_points, max_n_batch_points, n) == 1);
    CHECK(n_batches == SIZE_MAX/2 + 1);
    CHECK(n_batch_points == 2);
}

/**
 * Run rustsecp256k1_v0_4_0_ecmult_multi_var with num points and a scratch space restricted to
 * 1 <= i <= num points.
 */
void test_ecmult_multi_batching(void) {
    static const int n_points = 2*ECMULT_PIPPENGER_THRESHOLD;
    rustsecp256k1_v0_4_0_scalar scG;
    rustsecp256k1_v0_4_0_scalar szero;
    rustsecp256k1_v0_4_0_scalar *sc = (rustsecp256k1_v0_4_0_scalar *)checked_malloc(&ctx->error_callback, sizeof(rustsecp256k1_v0_4_0_scalar) * n_points);
    rustsecp256k1_v0_4_0_ge *pt = (rustsecp256k1_v0_4_0_ge *)checked_malloc(&ctx->error_callback, sizeof(rustsecp256k1_v0_4_0_ge) * n_points);
    rustsecp256k1_v0_4_0_gej r;
    rustsecp256k1_v0_4_0_gej r2;
    ecmult_multi_data data;
    int i;
    rustsecp256k1_v0_4_0_scratch *scratch;

    rustsecp256k1_v0_4_0_gej_set_infinity(&r2);
    rustsecp256k1_v0_4_0_scalar_set_int(&szero, 0);

    /* Get random scalars and group elements and compute result */
    random_scalar_order(&scG);
    rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &r2, &r2, &szero, &scG);
    for(i = 0; i < n_points; i++) {
        rustsecp256k1_v0_4_0_ge ptg;
        rustsecp256k1_v0_4_0_gej ptgj;
        random_group_element_test(&ptg);
        rustsecp256k1_v0_4_0_gej_set_ge(&ptgj, &ptg);
        pt[i] = ptg;
        random_scalar_order(&sc[i]);
        rustsecp256k1_v0_4_0_ecmult(&ctx->ecmult_ctx, &ptgj, &ptgj, &sc[i], NULL);
        rustsecp256k1_v0_4_0_gej_add_var(&r2, &r2, &ptgj, NULL);
    }
    data.sc = sc;
    data.pt = pt;
    rustsecp256k1_v0_4_0_gej_neg(&r2, &r2);

    /* Test with empty scratch space. It should compute the correct result using
     * ecmult_mult_simple algorithm which doesn't require a scratch space. */
    scratch = rustsecp256k1_v0_4_0_scratch_create(&ctx->error_callback, 0);
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_var(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &scG, ecmult_multi_callback, &data, n_points));
    rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
    rustsecp256k1_v0_4_0_scratch_destroy(&ctx->error_callback, scratch);

    /* Test with space for 1 point in pippenger. That's not enough because
     * ecmult_multi selects strauss which requires more memory. It should
     * therefore select the simple algorithm. */
    scratch = rustsecp256k1_v0_4_0_scratch_create(&ctx->error_callback, rustsecp256k1_v0_4_0_pippenger_scratch_size(1, 1) + PIPPENGER_SCRATCH_OBJECTS*ALIGNMENT);
    CHECK(rustsecp256k1_v0_4_0_ecmult_multi_var(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &scG, ecmult_multi_callback, &data, n_points));
    rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
    CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
    rustsecp256k1_v0_4_0_scratch_destroy(&ctx->error_callback, scratch);

    for(i = 1; i <= n_points; i++) {
        if (i > ECMULT_PIPPENGER_THRESHOLD) {
            int bucket_window = rustsecp256k1_v0_4_0_pippenger_bucket_window(i);
            size_t scratch_size = rustsecp256k1_v0_4_0_pippenger_scratch_size(i, bucket_window);
            scratch = rustsecp256k1_v0_4_0_scratch_create(&ctx->error_callback, scratch_size + PIPPENGER_SCRATCH_OBJECTS*ALIGNMENT);
        } else {
            size_t scratch_size = rustsecp256k1_v0_4_0_strauss_scratch_size(i);
            scratch = rustsecp256k1_v0_4_0_scratch_create(&ctx->error_callback, scratch_size + STRAUSS_SCRATCH_OBJECTS*ALIGNMENT);
        }
        CHECK(rustsecp256k1_v0_4_0_ecmult_multi_var(&ctx->error_callback, &ctx->ecmult_ctx, scratch, &r, &scG, ecmult_multi_callback, &data, n_points));
        rustsecp256k1_v0_4_0_gej_add_var(&r, &r, &r2, NULL);
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
        rustsecp256k1_v0_4_0_scratch_destroy(&ctx->error_callback, scratch);
    }
    free(sc);
    free(pt);
}

void run_ecmult_multi_tests(void) {
    rustsecp256k1_v0_4_0_scratch *scratch;

    test_rustsecp256k1_v0_4_0_pippenger_bucket_window_inv();
    test_ecmult_multi_pippenger_max_points();
    scratch = rustsecp256k1_v0_4_0_scratch_create(&ctx->error_callback, 819200);
    test_ecmult_multi(scratch, rustsecp256k1_v0_4_0_ecmult_multi_var);
    test_ecmult_multi(NULL, rustsecp256k1_v0_4_0_ecmult_multi_var);
    test_ecmult_multi(scratch, rustsecp256k1_v0_4_0_ecmult_pippenger_batch_single);
    test_ecmult_multi_batch_single(rustsecp256k1_v0_4_0_ecmult_pippenger_batch_single);
    test_ecmult_multi(scratch, rustsecp256k1_v0_4_0_ecmult_strauss_batch_single);
    test_ecmult_multi_batch_single(rustsecp256k1_v0_4_0_ecmult_strauss_batch_single);
    rustsecp256k1_v0_4_0_scratch_destroy(&ctx->error_callback, scratch);

    /* Run test_ecmult_multi with space for exactly one point */
    scratch = rustsecp256k1_v0_4_0_scratch_create(&ctx->error_callback, rustsecp256k1_v0_4_0_strauss_scratch_size(1) + STRAUSS_SCRATCH_OBJECTS*ALIGNMENT);
    test_ecmult_multi(scratch, rustsecp256k1_v0_4_0_ecmult_multi_var);
    rustsecp256k1_v0_4_0_scratch_destroy(&ctx->error_callback, scratch);

    test_ecmult_multi_batch_size_helper();
    test_ecmult_multi_batching();
}

void test_wnaf(const rustsecp256k1_v0_4_0_scalar *number, int w) {
    rustsecp256k1_v0_4_0_scalar x, two, t;
    int wnaf[256];
    int zeroes = -1;
    int i;
    int bits;
    rustsecp256k1_v0_4_0_scalar_set_int(&x, 0);
    rustsecp256k1_v0_4_0_scalar_set_int(&two, 2);
    bits = rustsecp256k1_v0_4_0_ecmult_wnaf(wnaf, 256, number, w);
    CHECK(bits <= 256);
    for (i = bits-1; i >= 0; i--) {
        int v = wnaf[i];
        rustsecp256k1_v0_4_0_scalar_mul(&x, &x, &two);
        if (v) {
            CHECK(zeroes == -1 || zeroes >= w-1); /* check that distance between non-zero elements is at least w-1 */
            zeroes=0;
            CHECK((v & 1) == 1); /* check non-zero elements are odd */
            CHECK(v <= (1 << (w-1)) - 1); /* check range below */
            CHECK(v >= -(1 << (w-1)) - 1); /* check range above */
        } else {
            CHECK(zeroes != -1); /* check that no unnecessary zero padding exists */
            zeroes++;
        }
        if (v >= 0) {
            rustsecp256k1_v0_4_0_scalar_set_int(&t, v);
        } else {
            rustsecp256k1_v0_4_0_scalar_set_int(&t, -v);
            rustsecp256k1_v0_4_0_scalar_negate(&t, &t);
        }
        rustsecp256k1_v0_4_0_scalar_add(&x, &x, &t);
    }
    CHECK(rustsecp256k1_v0_4_0_scalar_eq(&x, number)); /* check that wnaf represents number */
}

void test_constant_wnaf_negate(const rustsecp256k1_v0_4_0_scalar *number) {
    rustsecp256k1_v0_4_0_scalar neg1 = *number;
    rustsecp256k1_v0_4_0_scalar neg2 = *number;
    int sign1 = 1;
    int sign2 = 1;

    if (!rustsecp256k1_v0_4_0_scalar_get_bits(&neg1, 0, 1)) {
        rustsecp256k1_v0_4_0_scalar_negate(&neg1, &neg1);
        sign1 = -1;
    }
    sign2 = rustsecp256k1_v0_4_0_scalar_cond_negate(&neg2, rustsecp256k1_v0_4_0_scalar_is_even(&neg2));
    CHECK(sign1 == sign2);
    CHECK(rustsecp256k1_v0_4_0_scalar_eq(&neg1, &neg2));
}

void test_constant_wnaf(const rustsecp256k1_v0_4_0_scalar *number, int w) {
    rustsecp256k1_v0_4_0_scalar x, shift;
    int wnaf[256] = {0};
    int i;
    int skew;
    int bits = 256;
    rustsecp256k1_v0_4_0_scalar num = *number;
    rustsecp256k1_v0_4_0_scalar scalar_skew;

    rustsecp256k1_v0_4_0_scalar_set_int(&x, 0);
    rustsecp256k1_v0_4_0_scalar_set_int(&shift, 1 << w);
    for (i = 0; i < 16; ++i) {
        rustsecp256k1_v0_4_0_scalar_shr_int(&num, 8);
    }
    bits = 128;
    skew = rustsecp256k1_v0_4_0_wnaf_const(wnaf, &num, w, bits);

    for (i = WNAF_SIZE_BITS(bits, w); i >= 0; --i) {
        rustsecp256k1_v0_4_0_scalar t;
        int v = wnaf[i];
        CHECK(v != 0); /* check nonzero */
        CHECK(v & 1);  /* check parity */
        CHECK(v > -(1 << w)); /* check range above */
        CHECK(v < (1 << w));  /* check range below */

        rustsecp256k1_v0_4_0_scalar_mul(&x, &x, &shift);
        if (v >= 0) {
            rustsecp256k1_v0_4_0_scalar_set_int(&t, v);
        } else {
            rustsecp256k1_v0_4_0_scalar_set_int(&t, -v);
            rustsecp256k1_v0_4_0_scalar_negate(&t, &t);
        }
        rustsecp256k1_v0_4_0_scalar_add(&x, &x, &t);
    }
    /* Skew num because when encoding numbers as odd we use an offset */
    rustsecp256k1_v0_4_0_scalar_set_int(&scalar_skew, 1 << (skew == 2));
    rustsecp256k1_v0_4_0_scalar_add(&num, &num, &scalar_skew);
    CHECK(rustsecp256k1_v0_4_0_scalar_eq(&x, &num));
}

void test_fixed_wnaf(const rustsecp256k1_v0_4_0_scalar *number, int w) {
    rustsecp256k1_v0_4_0_scalar x, shift;
    int wnaf[256] = {0};
    int i;
    int skew;
    rustsecp256k1_v0_4_0_scalar num = *number;

    rustsecp256k1_v0_4_0_scalar_set_int(&x, 0);
    rustsecp256k1_v0_4_0_scalar_set_int(&shift, 1 << w);
    for (i = 0; i < 16; ++i) {
        rustsecp256k1_v0_4_0_scalar_shr_int(&num, 8);
    }
    skew = rustsecp256k1_v0_4_0_wnaf_fixed(wnaf, &num, w);

    for (i = WNAF_SIZE(w)-1; i >= 0; --i) {
        rustsecp256k1_v0_4_0_scalar t;
        int v = wnaf[i];
        CHECK(v == 0 || v & 1);  /* check parity */
        CHECK(v > -(1 << w)); /* check range above */
        CHECK(v < (1 << w));  /* check range below */

        rustsecp256k1_v0_4_0_scalar_mul(&x, &x, &shift);
        if (v >= 0) {
            rustsecp256k1_v0_4_0_scalar_set_int(&t, v);
        } else {
            rustsecp256k1_v0_4_0_scalar_set_int(&t, -v);
            rustsecp256k1_v0_4_0_scalar_negate(&t, &t);
        }
        rustsecp256k1_v0_4_0_scalar_add(&x, &x, &t);
    }
    /* If skew is 1 then add 1 to num */
    rustsecp256k1_v0_4_0_scalar_cadd_bit(&num, 0, skew == 1);
    CHECK(rustsecp256k1_v0_4_0_scalar_eq(&x, &num));
}

/* Checks that the first 8 elements of wnaf are equal to wnaf_expected and the
 * rest is 0.*/
void test_fixed_wnaf_small_helper(int *wnaf, int *wnaf_expected, int w) {
    int i;
    for (i = WNAF_SIZE(w)-1; i >= 8; --i) {
        CHECK(wnaf[i] == 0);
    }
    for (i = 7; i >= 0; --i) {
        CHECK(wnaf[i] == wnaf_expected[i]);
    }
}

void test_fixed_wnaf_small(void) {
    int w = 4;
    int wnaf[256] = {0};
    int i;
    int skew;
    rustsecp256k1_v0_4_0_scalar num;

    rustsecp256k1_v0_4_0_scalar_set_int(&num, 0);
    skew = rustsecp256k1_v0_4_0_wnaf_fixed(wnaf, &num, w);
    for (i = WNAF_SIZE(w)-1; i >= 0; --i) {
        int v = wnaf[i];
        CHECK(v == 0);
    }
    CHECK(skew == 0);

    rustsecp256k1_v0_4_0_scalar_set_int(&num, 1);
    skew = rustsecp256k1_v0_4_0_wnaf_fixed(wnaf, &num, w);
    for (i = WNAF_SIZE(w)-1; i >= 1; --i) {
        int v = wnaf[i];
        CHECK(v == 0);
    }
    CHECK(wnaf[0] == 1);
    CHECK(skew == 0);

    {
        int wnaf_expected[8] = { 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf };
        rustsecp256k1_v0_4_0_scalar_set_int(&num, 0xffffffff);
        skew = rustsecp256k1_v0_4_0_wnaf_fixed(wnaf, &num, w);
        test_fixed_wnaf_small_helper(wnaf, wnaf_expected, w);
        CHECK(skew == 0);
    }
    {
        int wnaf_expected[8] = { -1, -1, -1, -1, -1, -1, -1, 0xf };
        rustsecp256k1_v0_4_0_scalar_set_int(&num, 0xeeeeeeee);
        skew = rustsecp256k1_v0_4_0_wnaf_fixed(wnaf, &num, w);
        test_fixed_wnaf_small_helper(wnaf, wnaf_expected, w);
        CHECK(skew == 1);
    }
    {
        int wnaf_expected[8] = { 1, 0, 1, 0, 1, 0, 1, 0 };
        rustsecp256k1_v0_4_0_scalar_set_int(&num, 0x01010101);
        skew = rustsecp256k1_v0_4_0_wnaf_fixed(wnaf, &num, w);
        test_fixed_wnaf_small_helper(wnaf, wnaf_expected, w);
        CHECK(skew == 0);
    }
    {
        int wnaf_expected[8] = { -0xf, 0, 0xf, -0xf, 0, 0xf, 1, 0 };
        rustsecp256k1_v0_4_0_scalar_set_int(&num, 0x01ef1ef1);
        skew = rustsecp256k1_v0_4_0_wnaf_fixed(wnaf, &num, w);
        test_fixed_wnaf_small_helper(wnaf, wnaf_expected, w);
        CHECK(skew == 0);
    }
}

void run_wnaf(void) {
    int i;
    rustsecp256k1_v0_4_0_scalar n = {{0}};

    test_constant_wnaf(&n, 4);
    /* Sanity check: 1 and 2 are the smallest odd and even numbers and should
     *               have easier-to-diagnose failure modes  */
    n.d[0] = 1;
    test_constant_wnaf(&n, 4);
    n.d[0] = 2;
    test_constant_wnaf(&n, 4);
    /* Test -1, because it's a special case in wnaf_const */
    n = rustsecp256k1_v0_4_0_scalar_one;
    rustsecp256k1_v0_4_0_scalar_negate(&n, &n);
    test_constant_wnaf(&n, 4);

    /* Test -2, which may not lead to overflows in wnaf_const */
    rustsecp256k1_v0_4_0_scalar_add(&n, &rustsecp256k1_v0_4_0_scalar_one, &rustsecp256k1_v0_4_0_scalar_one);
    rustsecp256k1_v0_4_0_scalar_negate(&n, &n);
    test_constant_wnaf(&n, 4);

    /* Test (1/2) - 1 = 1/-2 and 1/2 = (1/-2) + 1
       as corner cases of negation handling in wnaf_const */
    rustsecp256k1_v0_4_0_scalar_inverse(&n, &n);
    test_constant_wnaf(&n, 4);

    rustsecp256k1_v0_4_0_scalar_add(&n, &n, &rustsecp256k1_v0_4_0_scalar_one);
    test_constant_wnaf(&n, 4);

    /* Test 0 for fixed wnaf */
    test_fixed_wnaf_small();
    /* Random tests */
    for (i = 0; i < count; i++) {
        random_scalar_order(&n);
        test_wnaf(&n, 4+(i%10));
        test_constant_wnaf_negate(&n);
        test_constant_wnaf(&n, 4 + (i % 10));
        test_fixed_wnaf(&n, 4 + (i % 10));
    }
    rustsecp256k1_v0_4_0_scalar_set_int(&n, 0);
    CHECK(rustsecp256k1_v0_4_0_scalar_cond_negate(&n, 1) == -1);
    CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&n));
    CHECK(rustsecp256k1_v0_4_0_scalar_cond_negate(&n, 0) == 1);
    CHECK(rustsecp256k1_v0_4_0_scalar_is_zero(&n));
}

void test_ecmult_constants(void) {
    /* Test ecmult_gen() for [0..36) and [order-36..0). */
    rustsecp256k1_v0_4_0_scalar x;
    rustsecp256k1_v0_4_0_gej r;
    rustsecp256k1_v0_4_0_ge ng;
    int i;
    int j;
    rustsecp256k1_v0_4_0_ge_neg(&ng, &rustsecp256k1_v0_4_0_ge_const_g);
    for (i = 0; i < 36; i++ ) {
        rustsecp256k1_v0_4_0_scalar_set_int(&x, i);
        rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &r, &x);
        for (j = 0; j < i; j++) {
            if (j == i - 1) {
                ge_equals_gej(&rustsecp256k1_v0_4_0_ge_const_g, &r);
            }
            rustsecp256k1_v0_4_0_gej_add_ge(&r, &r, &ng);
        }
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
    }
    for (i = 1; i <= 36; i++ ) {
        rustsecp256k1_v0_4_0_scalar_set_int(&x, i);
        rustsecp256k1_v0_4_0_scalar_negate(&x, &x);
        rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &r, &x);
        for (j = 0; j < i; j++) {
            if (j == i - 1) {
                ge_equals_gej(&ng, &r);
            }
            rustsecp256k1_v0_4_0_gej_add_ge(&r, &r, &rustsecp256k1_v0_4_0_ge_const_g);
        }
        CHECK(rustsecp256k1_v0_4_0_gej_is_infinity(&r));
    }
}

void run_ecmult_constants(void) {
    test_ecmult_constants();
}

void test_ecmult_gen_blind(void) {
    /* Test ecmult_gen() blinding and confirm that the blinding changes, the affine points match, and the z's don't match. */
    rustsecp256k1_v0_4_0_scalar key;
    rustsecp256k1_v0_4_0_scalar b;
    unsigned char seed32[32];
    rustsecp256k1_v0_4_0_gej pgej;
    rustsecp256k1_v0_4_0_gej pgej2;
    rustsecp256k1_v0_4_0_gej i;
    rustsecp256k1_v0_4_0_ge pge;
    random_scalar_order_test(&key);
    rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &pgej, &key);
    rustsecp256k1_v0_4_0_testrand256(seed32);
    b = ctx->ecmult_gen_ctx.blind;
    i = ctx->ecmult_gen_ctx.initial;
    rustsecp256k1_v0_4_0_ecmult_gen_blind(&ctx->ecmult_gen_ctx, seed32);
    CHECK(!rustsecp256k1_v0_4_0_scalar_eq(&b, &ctx->ecmult_gen_ctx.blind));
    rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &pgej2, &key);
    CHECK(!gej_xyz_equals_gej(&pgej, &pgej2));
    CHECK(!gej_xyz_equals_gej(&i, &ctx->ecmult_gen_ctx.initial));
    rustsecp256k1_v0_4_0_ge_set_gej(&pge, &pgej);
    ge_equals_gej(&pge, &pgej2);
}

void test_ecmult_gen_blind_reset(void) {
    /* Test ecmult_gen() blinding reset and confirm that the blinding is consistent. */
    rustsecp256k1_v0_4_0_scalar b;
    rustsecp256k1_v0_4_0_gej initial;
    rustsecp256k1_v0_4_0_ecmult_gen_blind(&ctx->ecmult_gen_ctx, 0);
    b = ctx->ecmult_gen_ctx.blind;
    initial = ctx->ecmult_gen_ctx.initial;
    rustsecp256k1_v0_4_0_ecmult_gen_blind(&ctx->ecmult_gen_ctx, 0);
    CHECK(rustsecp256k1_v0_4_0_scalar_eq(&b, &ctx->ecmult_gen_ctx.blind));
    CHECK(gej_xyz_equals_gej(&initial, &ctx->ecmult_gen_ctx.initial));
}

void run_ecmult_gen_blind(void) {
    int i;
    test_ecmult_gen_blind_reset();
    for (i = 0; i < 10; i++) {
        test_ecmult_gen_blind();
    }
}

/***** ENDOMORPHISH TESTS *****/
void test_scalar_split(const rustsecp256k1_v0_4_0_scalar* full) {
    rustsecp256k1_v0_4_0_scalar s, s1, slam;
    const unsigned char zero[32] = {0};
    unsigned char tmp[32];

    rustsecp256k1_v0_4_0_scalar_split_lambda(&s1, &slam, full);

    /* check slam*lambda + s1 == full */
    rustsecp256k1_v0_4_0_scalar_mul(&s, &rustsecp256k1_v0_4_0_const_lambda, &slam);
    rustsecp256k1_v0_4_0_scalar_add(&s, &s, &s1);
    CHECK(rustsecp256k1_v0_4_0_scalar_eq(&s, full));

    /* check that both are <= 128 bits in size */
    if (rustsecp256k1_v0_4_0_scalar_is_high(&s1)) {
        rustsecp256k1_v0_4_0_scalar_negate(&s1, &s1);
    }
    if (rustsecp256k1_v0_4_0_scalar_is_high(&slam)) {
        rustsecp256k1_v0_4_0_scalar_negate(&slam, &slam);
    }

    rustsecp256k1_v0_4_0_scalar_get_b32(tmp, &s1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(zero, tmp, 16) == 0);
    rustsecp256k1_v0_4_0_scalar_get_b32(tmp, &slam);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(zero, tmp, 16) == 0);
}


void run_endomorphism_tests(void) {
    unsigned i;
    static rustsecp256k1_v0_4_0_scalar s;
    test_scalar_split(&rustsecp256k1_v0_4_0_scalar_zero);
    test_scalar_split(&rustsecp256k1_v0_4_0_scalar_one);
    rustsecp256k1_v0_4_0_scalar_negate(&s,&rustsecp256k1_v0_4_0_scalar_one);
    test_scalar_split(&s);
    test_scalar_split(&rustsecp256k1_v0_4_0_const_lambda);
    rustsecp256k1_v0_4_0_scalar_add(&s, &rustsecp256k1_v0_4_0_const_lambda, &rustsecp256k1_v0_4_0_scalar_one);
    test_scalar_split(&s);

    for (i = 0; i < 100U * count; ++i) {
        rustsecp256k1_v0_4_0_scalar full;
        random_scalar_order_test(&full);
        test_scalar_split(&full);
    }
    for (i = 0; i < sizeof(scalars_near_split_bounds) / sizeof(scalars_near_split_bounds[0]); ++i) {
        test_scalar_split(&scalars_near_split_bounds[i]);
    }
}

void ec_pubkey_parse_pointtest(const unsigned char *input, int xvalid, int yvalid) {
    unsigned char pubkeyc[65];
    rustsecp256k1_v0_4_0_pubkey pubkey;
    rustsecp256k1_v0_4_0_ge ge;
    size_t pubkeyclen;
    int32_t ecount;
    ecount = 0;
    rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, counting_illegal_callback_fn, &ecount);
    for (pubkeyclen = 3; pubkeyclen <= 65; pubkeyclen++) {
        /* Smaller sizes are tested exhaustively elsewhere. */
        int32_t i;
        memcpy(&pubkeyc[1], input, 64);
        VG_UNDEF(&pubkeyc[pubkeyclen], 65 - pubkeyclen);
        for (i = 0; i < 256; i++) {
            /* Try all type bytes. */
            int xpass;
            int ypass;
            int ysign;
            pubkeyc[0] = i;
            /* What sign does this point have? */
            ysign = (input[63] & 1) + 2;
            /* For the current type (i) do we expect parsing to work? Handled all of compressed/uncompressed/hybrid. */
            xpass = xvalid && (pubkeyclen == 33) && ((i & 254) == 2);
            /* Do we expect a parse and re-serialize as uncompressed to give a matching y? */
            ypass = xvalid && yvalid && ((i & 4) == ((pubkeyclen == 65) << 2)) &&
                ((i == 4) || ((i & 251) == ysign)) && ((pubkeyclen == 33) || (pubkeyclen == 65));
            if (xpass || ypass) {
                /* These cases must parse. */
                unsigned char pubkeyo[65];
                size_t outl;
                memset(&pubkey, 0, sizeof(pubkey));
                VG_UNDEF(&pubkey, sizeof(pubkey));
                ecount = 0;
                CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, pubkeyc, pubkeyclen) == 1);
                VG_CHECK(&pubkey, sizeof(pubkey));
                outl = 65;
                VG_UNDEF(pubkeyo, 65);
                CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, pubkeyo, &outl, &pubkey, SECP256K1_EC_COMPRESSED) == 1);
                VG_CHECK(pubkeyo, outl);
                CHECK(outl == 33);
                CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkeyo[1], &pubkeyc[1], 32) == 0);
                CHECK((pubkeyclen != 33) || (pubkeyo[0] == pubkeyc[0]));
                if (ypass) {
                    /* This test isn't always done because we decode with alternative signs, so the y won't match. */
                    CHECK(pubkeyo[0] == ysign);
                    CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 1);
                    memset(&pubkey, 0, sizeof(pubkey));
                    VG_UNDEF(&pubkey, sizeof(pubkey));
                    rustsecp256k1_v0_4_0_pubkey_save(&pubkey, &ge);
                    VG_CHECK(&pubkey, sizeof(pubkey));
                    outl = 65;
                    VG_UNDEF(pubkeyo, 65);
                    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, pubkeyo, &outl, &pubkey, SECP256K1_EC_UNCOMPRESSED) == 1);
                    VG_CHECK(pubkeyo, outl);
                    CHECK(outl == 65);
                    CHECK(pubkeyo[0] == 4);
                    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkeyo[1], input, 64) == 0);
                }
                CHECK(ecount == 0);
            } else {
                /* These cases must fail to parse. */
                memset(&pubkey, 0xfe, sizeof(pubkey));
                ecount = 0;
                VG_UNDEF(&pubkey, sizeof(pubkey));
                CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, pubkeyc, pubkeyclen) == 0);
                VG_CHECK(&pubkey, sizeof(pubkey));
                CHECK(ecount == 0);
                CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 0);
                CHECK(ecount == 1);
            }
        }
    }
    rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, NULL, NULL);
}

void run_ec_pubkey_parse_test(void) {
#define SECP256K1_EC_PARSE_TEST_NVALID (12)
    const unsigned char valid[SECP256K1_EC_PARSE_TEST_NVALID][64] = {
        {
            /* Point with leading and trailing zeros in x and y serialization. */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x52,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x64, 0xef, 0xa1, 0x7b, 0x77, 0x61, 0xe1, 0xe4, 0x27, 0x06, 0x98, 0x9f, 0xb4, 0x83,
            0xb8, 0xd2, 0xd4, 0x9b, 0xf7, 0x8f, 0xae, 0x98, 0x03, 0xf0, 0x99, 0xb8, 0x34, 0xed, 0xeb, 0x00
        },
        {
            /* Point with x equal to a 3rd root of unity.*/
            0x7a, 0xe9, 0x6a, 0x2b, 0x65, 0x7c, 0x07, 0x10, 0x6e, 0x64, 0x47, 0x9e, 0xac, 0x34, 0x34, 0xe9,
            0x9c, 0xf0, 0x49, 0x75, 0x12, 0xf5, 0x89, 0x95, 0xc1, 0x39, 0x6c, 0x28, 0x71, 0x95, 0x01, 0xee,
            0x42, 0x18, 0xf2, 0x0a, 0xe6, 0xc6, 0x46, 0xb3, 0x63, 0xdb, 0x68, 0x60, 0x58, 0x22, 0xfb, 0x14,
            0x26, 0x4c, 0xa8, 0xd2, 0x58, 0x7f, 0xdd, 0x6f, 0xbc, 0x75, 0x0d, 0x58, 0x7e, 0x76, 0xa7, 0xee,
        },
        {
            /* Point with largest x. (1/2) */
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2c,
            0x0e, 0x99, 0x4b, 0x14, 0xea, 0x72, 0xf8, 0xc3, 0xeb, 0x95, 0xc7, 0x1e, 0xf6, 0x92, 0x57, 0x5e,
            0x77, 0x50, 0x58, 0x33, 0x2d, 0x7e, 0x52, 0xd0, 0x99, 0x5c, 0xf8, 0x03, 0x88, 0x71, 0xb6, 0x7d,
        },
        {
            /* Point with largest x. (2/2) */
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2c,
            0xf1, 0x66, 0xb4, 0xeb, 0x15, 0x8d, 0x07, 0x3c, 0x14, 0x6a, 0x38, 0xe1, 0x09, 0x6d, 0xa8, 0xa1,
            0x88, 0xaf, 0xa7, 0xcc, 0xd2, 0x81, 0xad, 0x2f, 0x66, 0xa3, 0x07, 0xfb, 0x77, 0x8e, 0x45, 0xb2,
        },
        {
            /* Point with smallest x. (1/2) */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x42, 0x18, 0xf2, 0x0a, 0xe6, 0xc6, 0x46, 0xb3, 0x63, 0xdb, 0x68, 0x60, 0x58, 0x22, 0xfb, 0x14,
            0x26, 0x4c, 0xa8, 0xd2, 0x58, 0x7f, 0xdd, 0x6f, 0xbc, 0x75, 0x0d, 0x58, 0x7e, 0x76, 0xa7, 0xee,
        },
        {
            /* Point with smallest x. (2/2) */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0xbd, 0xe7, 0x0d, 0xf5, 0x19, 0x39, 0xb9, 0x4c, 0x9c, 0x24, 0x97, 0x9f, 0xa7, 0xdd, 0x04, 0xeb,
            0xd9, 0xb3, 0x57, 0x2d, 0xa7, 0x80, 0x22, 0x90, 0x43, 0x8a, 0xf2, 0xa6, 0x81, 0x89, 0x54, 0x41,
        },
        {
            /* Point with largest y. (1/3) */
            0x1f, 0xe1, 0xe5, 0xef, 0x3f, 0xce, 0xb5, 0xc1, 0x35, 0xab, 0x77, 0x41, 0x33, 0x3c, 0xe5, 0xa6,
            0xe8, 0x0d, 0x68, 0x16, 0x76, 0x53, 0xf6, 0xb2, 0xb2, 0x4b, 0xcb, 0xcf, 0xaa, 0xaf, 0xf5, 0x07,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2e,
        },
        {
            /* Point with largest y. (2/3) */
            0xcb, 0xb0, 0xde, 0xab, 0x12, 0x57, 0x54, 0xf1, 0xfd, 0xb2, 0x03, 0x8b, 0x04, 0x34, 0xed, 0x9c,
            0xb3, 0xfb, 0x53, 0xab, 0x73, 0x53, 0x91, 0x12, 0x99, 0x94, 0xa5, 0x35, 0xd9, 0x25, 0xf6, 0x73,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2e,
        },
        {
            /* Point with largest y. (3/3) */
            0x14, 0x6d, 0x3b, 0x65, 0xad, 0xd9, 0xf5, 0x4c, 0xcc, 0xa2, 0x85, 0x33, 0xc8, 0x8e, 0x2c, 0xbc,
            0x63, 0xf7, 0x44, 0x3e, 0x16, 0x58, 0x78, 0x3a, 0xb4, 0x1f, 0x8e, 0xf9, 0x7c, 0x2a, 0x10, 0xb5,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2e,
        },
        {
            /* Point with smallest y. (1/3) */
            0x1f, 0xe1, 0xe5, 0xef, 0x3f, 0xce, 0xb5, 0xc1, 0x35, 0xab, 0x77, 0x41, 0x33, 0x3c, 0xe5, 0xa6,
            0xe8, 0x0d, 0x68, 0x16, 0x76, 0x53, 0xf6, 0xb2, 0xb2, 0x4b, 0xcb, 0xcf, 0xaa, 0xaf, 0xf5, 0x07,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        },
        {
            /* Point with smallest y. (2/3) */
            0xcb, 0xb0, 0xde, 0xab, 0x12, 0x57, 0x54, 0xf1, 0xfd, 0xb2, 0x03, 0x8b, 0x04, 0x34, 0xed, 0x9c,
            0xb3, 0xfb, 0x53, 0xab, 0x73, 0x53, 0x91, 0x12, 0x99, 0x94, 0xa5, 0x35, 0xd9, 0x25, 0xf6, 0x73,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        },
        {
            /* Point with smallest y. (3/3) */
            0x14, 0x6d, 0x3b, 0x65, 0xad, 0xd9, 0xf5, 0x4c, 0xcc, 0xa2, 0x85, 0x33, 0xc8, 0x8e, 0x2c, 0xbc,
            0x63, 0xf7, 0x44, 0x3e, 0x16, 0x58, 0x78, 0x3a, 0xb4, 0x1f, 0x8e, 0xf9, 0x7c, 0x2a, 0x10, 0xb5,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
        }
    };
#define SECP256K1_EC_PARSE_TEST_NXVALID (4)
    const unsigned char onlyxvalid[SECP256K1_EC_PARSE_TEST_NXVALID][64] = {
        {
            /* Valid if y overflow ignored (y = 1 mod p). (1/3) */
            0x1f, 0xe1, 0xe5, 0xef, 0x3f, 0xce, 0xb5, 0xc1, 0x35, 0xab, 0x77, 0x41, 0x33, 0x3c, 0xe5, 0xa6,
            0xe8, 0x0d, 0x68, 0x16, 0x76, 0x53, 0xf6, 0xb2, 0xb2, 0x4b, 0xcb, 0xcf, 0xaa, 0xaf, 0xf5, 0x07,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x30,
        },
        {
            /* Valid if y overflow ignored (y = 1 mod p). (2/3) */
            0xcb, 0xb0, 0xde, 0xab, 0x12, 0x57, 0x54, 0xf1, 0xfd, 0xb2, 0x03, 0x8b, 0x04, 0x34, 0xed, 0x9c,
            0xb3, 0xfb, 0x53, 0xab, 0x73, 0x53, 0x91, 0x12, 0x99, 0x94, 0xa5, 0x35, 0xd9, 0x25, 0xf6, 0x73,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x30,
        },
        {
            /* Valid if y overflow ignored (y = 1 mod p). (3/3)*/
            0x14, 0x6d, 0x3b, 0x65, 0xad, 0xd9, 0xf5, 0x4c, 0xcc, 0xa2, 0x85, 0x33, 0xc8, 0x8e, 0x2c, 0xbc,
            0x63, 0xf7, 0x44, 0x3e, 0x16, 0x58, 0x78, 0x3a, 0xb4, 0x1f, 0x8e, 0xf9, 0x7c, 0x2a, 0x10, 0xb5,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x30,
        },
        {
            /* x on curve, y is from y^2 = x^3 + 8. */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03
        }
    };
#define SECP256K1_EC_PARSE_TEST_NINVALID (7)
    const unsigned char invalid[SECP256K1_EC_PARSE_TEST_NINVALID][64] = {
        {
            /* x is third root of -8, y is -1 * (x^3+7); also on the curve for y^2 = x^3 + 9. */
            0x0a, 0x2d, 0x2b, 0xa9, 0x35, 0x07, 0xf1, 0xdf, 0x23, 0x37, 0x70, 0xc2, 0xa7, 0x97, 0x96, 0x2c,
            0xc6, 0x1f, 0x6d, 0x15, 0xda, 0x14, 0xec, 0xd4, 0x7d, 0x8d, 0x27, 0xae, 0x1c, 0xd5, 0xf8, 0x53,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        },
        {
            /* Valid if x overflow ignored (x = 1 mod p). */
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x30,
            0x42, 0x18, 0xf2, 0x0a, 0xe6, 0xc6, 0x46, 0xb3, 0x63, 0xdb, 0x68, 0x60, 0x58, 0x22, 0xfb, 0x14,
            0x26, 0x4c, 0xa8, 0xd2, 0x58, 0x7f, 0xdd, 0x6f, 0xbc, 0x75, 0x0d, 0x58, 0x7e, 0x76, 0xa7, 0xee,
        },
        {
            /* Valid if x overflow ignored (x = 1 mod p). */
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x30,
            0xbd, 0xe7, 0x0d, 0xf5, 0x19, 0x39, 0xb9, 0x4c, 0x9c, 0x24, 0x97, 0x9f, 0xa7, 0xdd, 0x04, 0xeb,
            0xd9, 0xb3, 0x57, 0x2d, 0xa7, 0x80, 0x22, 0x90, 0x43, 0x8a, 0xf2, 0xa6, 0x81, 0x89, 0x54, 0x41,
        },
        {
            /* x is -1, y is the result of the sqrt ladder; also on the curve for y^2 = x^3 - 5. */
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2e,
            0xf4, 0x84, 0x14, 0x5c, 0xb0, 0x14, 0x9b, 0x82, 0x5d, 0xff, 0x41, 0x2f, 0xa0, 0x52, 0xa8, 0x3f,
            0xcb, 0x72, 0xdb, 0x61, 0xd5, 0x6f, 0x37, 0x70, 0xce, 0x06, 0x6b, 0x73, 0x49, 0xa2, 0xaa, 0x28,
        },
        {
            /* x is -1, y is the result of the sqrt ladder; also on the curve for y^2 = x^3 - 5. */
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xfc, 0x2e,
            0x0b, 0x7b, 0xeb, 0xa3, 0x4f, 0xeb, 0x64, 0x7d, 0xa2, 0x00, 0xbe, 0xd0, 0x5f, 0xad, 0x57, 0xc0,
            0x34, 0x8d, 0x24, 0x9e, 0x2a, 0x90, 0xc8, 0x8f, 0x31, 0xf9, 0x94, 0x8b, 0xb6, 0x5d, 0x52, 0x07,
        },
        {
            /* x is zero, y is the result of the sqrt ladder; also on the curve for y^2 = x^3 - 7. */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x8f, 0x53, 0x7e, 0xef, 0xdf, 0xc1, 0x60, 0x6a, 0x07, 0x27, 0xcd, 0x69, 0xb4, 0xa7, 0x33, 0x3d,
            0x38, 0xed, 0x44, 0xe3, 0x93, 0x2a, 0x71, 0x79, 0xee, 0xcb, 0x4b, 0x6f, 0xba, 0x93, 0x60, 0xdc,
        },
        {
            /* x is zero, y is the result of the sqrt ladder; also on the curve for y^2 = x^3 - 7. */
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x70, 0xac, 0x81, 0x10, 0x20, 0x3e, 0x9f, 0x95, 0xf8, 0xd8, 0x32, 0x96, 0x4b, 0x58, 0xcc, 0xc2,
            0xc7, 0x12, 0xbb, 0x1c, 0x6c, 0xd5, 0x8e, 0x86, 0x11, 0x34, 0xb4, 0x8f, 0x45, 0x6c, 0x9b, 0x53
        }
    };
    const unsigned char pubkeyc[66] = {
        /* Serialization of G. */
        0x04, 0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC, 0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B,
        0x07, 0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9, 0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17,
        0x98, 0x48, 0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65, 0x5D, 0xA4, 0xFB, 0xFC, 0x0E, 0x11, 0x08,
        0xA8, 0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54, 0x19, 0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4,
        0xB8, 0x00
    };
    unsigned char sout[65];
    unsigned char shortkey[2];
    rustsecp256k1_v0_4_0_ge ge;
    rustsecp256k1_v0_4_0_pubkey pubkey;
    size_t len;
    int32_t i;
    int32_t ecount;
    int32_t ecount2;
    ecount = 0;
    /* Nothing should be reading this far into pubkeyc. */
    VG_UNDEF(&pubkeyc[65], 1);
    rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, counting_illegal_callback_fn, &ecount);
    /* Zero length claimed, fail, zeroize, no illegal arg error. */
    memset(&pubkey, 0xfe, sizeof(pubkey));
    ecount = 0;
    VG_UNDEF(shortkey, 2);
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, shortkey, 0) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(ecount == 0);
    CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 0);
    CHECK(ecount == 1);
    /* Length one claimed, fail, zeroize, no illegal arg error. */
    for (i = 0; i < 256 ; i++) {
        memset(&pubkey, 0xfe, sizeof(pubkey));
        ecount = 0;
        shortkey[0] = i;
        VG_UNDEF(&shortkey[1], 1);
        VG_UNDEF(&pubkey, sizeof(pubkey));
        CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, shortkey, 1) == 0);
        VG_CHECK(&pubkey, sizeof(pubkey));
        CHECK(ecount == 0);
        CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 0);
        CHECK(ecount == 1);
    }
    /* Length two claimed, fail, zeroize, no illegal arg error. */
    for (i = 0; i < 65536 ; i++) {
        memset(&pubkey, 0xfe, sizeof(pubkey));
        ecount = 0;
        shortkey[0] = i & 255;
        shortkey[1] = i >> 8;
        VG_UNDEF(&pubkey, sizeof(pubkey));
        CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, shortkey, 2) == 0);
        VG_CHECK(&pubkey, sizeof(pubkey));
        CHECK(ecount == 0);
        CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 0);
        CHECK(ecount == 1);
    }
    memset(&pubkey, 0xfe, sizeof(pubkey));
    ecount = 0;
    VG_UNDEF(&pubkey, sizeof(pubkey));
    /* 33 bytes claimed on otherwise valid input starting with 0x04, fail, zeroize output, no illegal arg error. */
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, pubkeyc, 33) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(ecount == 0);
    CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 0);
    CHECK(ecount == 1);
    /* NULL pubkey, illegal arg error. Pubkey isn't rewritten before this step, since it's NULL into the parser. */
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, NULL, pubkeyc, 65) == 0);
    CHECK(ecount == 2);
    /* NULL input string. Illegal arg and zeroize output. */
    memset(&pubkey, 0xfe, sizeof(pubkey));
    ecount = 0;
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, NULL, 65) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(ecount == 1);
    CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 0);
    CHECK(ecount == 2);
    /* 64 bytes claimed on input starting with 0x04, fail, zeroize output, no illegal arg error. */
    memset(&pubkey, 0xfe, sizeof(pubkey));
    ecount = 0;
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, pubkeyc, 64) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(ecount == 0);
    CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 0);
    CHECK(ecount == 1);
    /* 66 bytes claimed, fail, zeroize output, no illegal arg error. */
    memset(&pubkey, 0xfe, sizeof(pubkey));
    ecount = 0;
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, pubkeyc, 66) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(ecount == 0);
    CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 0);
    CHECK(ecount == 1);
    /* Valid parse. */
    memset(&pubkey, 0, sizeof(pubkey));
    ecount = 0;
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, pubkeyc, 65) == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(rustsecp256k1_v0_4_0_context_no_precomp, &pubkey, pubkeyc, 65) == 1);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(ecount == 0);
    VG_UNDEF(&ge, sizeof(ge));
    CHECK(rustsecp256k1_v0_4_0_pubkey_load(ctx, &ge, &pubkey) == 1);
    VG_CHECK(&ge.x, sizeof(ge.x));
    VG_CHECK(&ge.y, sizeof(ge.y));
    VG_CHECK(&ge.infinity, sizeof(ge.infinity));
    ge_equals_ge(&rustsecp256k1_v0_4_0_ge_const_g, &ge);
    CHECK(ecount == 0);
    /* rustsecp256k1_v0_4_0_ec_pubkey_serialize illegal args. */
    ecount = 0;
    len = 65;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, NULL, &len, &pubkey, SECP256K1_EC_UNCOMPRESSED) == 0);
    CHECK(ecount == 1);
    CHECK(len == 0);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, sout, NULL, &pubkey, SECP256K1_EC_UNCOMPRESSED) == 0);
    CHECK(ecount == 2);
    len = 65;
    VG_UNDEF(sout, 65);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, sout, &len, NULL, SECP256K1_EC_UNCOMPRESSED) == 0);
    VG_CHECK(sout, 65);
    CHECK(ecount == 3);
    CHECK(len == 0);
    len = 65;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, sout, &len, &pubkey, ~0) == 0);
    CHECK(ecount == 4);
    CHECK(len == 0);
    len = 65;
    VG_UNDEF(sout, 65);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, sout, &len, &pubkey, SECP256K1_EC_UNCOMPRESSED) == 1);
    VG_CHECK(sout, 65);
    CHECK(ecount == 4);
    CHECK(len == 65);
    /* Multiple illegal args. Should still set arg error only once. */
    ecount = 0;
    ecount2 = 11;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, NULL, NULL, 65) == 0);
    CHECK(ecount == 1);
    /* Does the illegal arg callback actually change the behavior? */
    rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, uncounting_illegal_callback_fn, &ecount2);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, NULL, NULL, 65) == 0);
    CHECK(ecount == 1);
    CHECK(ecount2 == 10);
    rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, NULL, NULL);
    /* Try a bunch of prefabbed points with all possible encodings. */
    for (i = 0; i < SECP256K1_EC_PARSE_TEST_NVALID; i++) {
        ec_pubkey_parse_pointtest(valid[i], 1, 1);
    }
    for (i = 0; i < SECP256K1_EC_PARSE_TEST_NXVALID; i++) {
        ec_pubkey_parse_pointtest(onlyxvalid[i], 1, 0);
    }
    for (i = 0; i < SECP256K1_EC_PARSE_TEST_NINVALID; i++) {
        ec_pubkey_parse_pointtest(invalid[i], 0, 0);
    }
}

void run_eckey_edge_case_test(void) {
    const unsigned char orderc[32] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
        0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
        0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x41
    };
    const unsigned char zeros[sizeof(rustsecp256k1_v0_4_0_pubkey)] = {0x00};
    unsigned char ctmp[33];
    unsigned char ctmp2[33];
    rustsecp256k1_v0_4_0_pubkey pubkey;
    rustsecp256k1_v0_4_0_pubkey pubkey2;
    rustsecp256k1_v0_4_0_pubkey pubkey_one;
    rustsecp256k1_v0_4_0_pubkey pubkey_negone;
    const rustsecp256k1_v0_4_0_pubkey *pubkeys[3];
    size_t len;
    int32_t ecount;
    /* Group order is too large, reject. */
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, orderc) == 0);
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, orderc) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    /* Maximum value is too large, reject. */
    memset(ctmp, 255, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, ctmp) == 0);
    memset(&pubkey, 1, sizeof(pubkey));
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, ctmp) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    /* Zero is too small, reject. */
    memset(ctmp, 0, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, ctmp) == 0);
    memset(&pubkey, 1, sizeof(pubkey));
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, ctmp) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    /* One must be accepted. */
    ctmp[31] = 0x01;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, ctmp) == 1);
    memset(&pubkey, 0, sizeof(pubkey));
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, ctmp) == 1);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) > 0);
    pubkey_one = pubkey;
    /* Group order + 1 is too large, reject. */
    memcpy(ctmp, orderc, 32);
    ctmp[31] = 0x42;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, ctmp) == 0);
    memset(&pubkey, 1, sizeof(pubkey));
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, ctmp) == 0);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    /* -1 must be accepted. */
    ctmp[31] = 0x40;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, ctmp) == 1);
    memset(&pubkey, 0, sizeof(pubkey));
    VG_UNDEF(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, ctmp) == 1);
    VG_CHECK(&pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) > 0);
    pubkey_negone = pubkey;
    /* Tweak of zero leaves the value unchanged. */
    memset(ctmp2, 0, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_add(ctx, ctmp, ctmp2) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(orderc, ctmp, 31) == 0 && ctmp[31] == 0x40);
    memcpy(&pubkey2, &pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, &pubkey, ctmp2) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, &pubkey2, sizeof(pubkey)) == 0);
    /* Multiply tweak of zero zeroizes the output. */
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_mul(ctx, ctmp, ctmp2) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(zeros, ctmp, 32) == 0);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(ctx, &pubkey, ctmp2) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(pubkey)) == 0);
    memcpy(&pubkey, &pubkey2, sizeof(pubkey));
    /* If seckey_tweak_add or seckey_tweak_mul are called with an overflowing
    seckey, the seckey is zeroized. */
    memcpy(ctmp, orderc, 32);
    memset(ctmp2, 0, 32);
    ctmp2[31] = 0x01;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, ctmp2) == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, ctmp) == 0);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_add(ctx, ctmp, ctmp2) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(zeros, ctmp, 32) == 0);
    memcpy(ctmp, orderc, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_mul(ctx, ctmp, ctmp2) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(zeros, ctmp, 32) == 0);
    /* If seckey_tweak_add or seckey_tweak_mul are called with an overflowing
    tweak, the seckey is zeroized. */
    memcpy(ctmp, orderc, 32);
    ctmp[31] = 0x40;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_add(ctx, ctmp, orderc) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(zeros, ctmp, 32) == 0);
    memcpy(ctmp, orderc, 32);
    ctmp[31] = 0x40;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_mul(ctx, ctmp, orderc) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(zeros, ctmp, 32) == 0);
    memcpy(ctmp, orderc, 32);
    ctmp[31] = 0x40;
    /* If pubkey_tweak_add or pubkey_tweak_mul are called with an overflowing
    tweak, the pubkey is zeroized. */
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, &pubkey, orderc) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(pubkey)) == 0);
    memcpy(&pubkey, &pubkey2, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(ctx, &pubkey, orderc) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(pubkey)) == 0);
    memcpy(&pubkey, &pubkey2, sizeof(pubkey));
    /* If the resulting key in rustsecp256k1_v0_4_0_ec_seckey_tweak_add and
     * rustsecp256k1_v0_4_0_ec_pubkey_tweak_add is 0 the functions fail and in the latter
     * case the pubkey is zeroized. */
    memcpy(ctmp, orderc, 32);
    ctmp[31] = 0x40;
    memset(ctmp2, 0, 32);
    ctmp2[31] = 1;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_add(ctx, ctmp2, ctmp) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(zeros, ctmp2, 32) == 0);
    ctmp2[31] = 1;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, &pubkey, ctmp2) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(pubkey)) == 0);
    memcpy(&pubkey, &pubkey2, sizeof(pubkey));
    /* Tweak computation wraps and results in a key of 1. */
    ctmp2[31] = 2;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_add(ctx, ctmp2, ctmp) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(ctmp2, zeros, 31) == 0 && ctmp2[31] == 1);
    ctmp2[31] = 2;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, &pubkey, ctmp2) == 1);
    ctmp2[31] = 1;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey2, ctmp2) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, &pubkey2, sizeof(pubkey)) == 0);
    /* Tweak mul * 2 = 1+1. */
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, &pubkey, ctmp2) == 1);
    ctmp2[31] = 2;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(ctx, &pubkey2, ctmp2) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, &pubkey2, sizeof(pubkey)) == 0);
    /* Test argument errors. */
    ecount = 0;
    rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, counting_illegal_callback_fn, &ecount);
    CHECK(ecount == 0);
    /* Zeroize pubkey on parse error. */
    memset(&pubkey, 0, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, &pubkey, ctmp2) == 0);
    CHECK(ecount == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(pubkey)) == 0);
    memcpy(&pubkey, &pubkey2, sizeof(pubkey));
    memset(&pubkey2, 0, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(ctx, &pubkey2, ctmp2) == 0);
    CHECK(ecount == 2);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey2, zeros, sizeof(pubkey2)) == 0);
    /* Plain argument errors. */
    ecount = 0;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, ctmp) == 1);
    CHECK(ecount == 0);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, NULL) == 0);
    CHECK(ecount == 1);
    ecount = 0;
    memset(ctmp2, 0, 32);
    ctmp2[31] = 4;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, NULL, ctmp2) == 0);
    CHECK(ecount == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, &pubkey, NULL) == 0);
    CHECK(ecount == 2);
    ecount = 0;
    memset(ctmp2, 0, 32);
    ctmp2[31] = 4;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(ctx, NULL, ctmp2) == 0);
    CHECK(ecount == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(ctx, &pubkey, NULL) == 0);
    CHECK(ecount == 2);
    ecount = 0;
    memset(ctmp2, 0, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_add(ctx, NULL, ctmp2) == 0);
    CHECK(ecount == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_add(ctx, ctmp, NULL) == 0);
    CHECK(ecount == 2);
    ecount = 0;
    memset(ctmp2, 0, 32);
    ctmp2[31] = 1;
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_mul(ctx, NULL, ctmp2) == 0);
    CHECK(ecount == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_tweak_mul(ctx, ctmp, NULL) == 0);
    CHECK(ecount == 2);
    ecount = 0;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, NULL, ctmp) == 0);
    CHECK(ecount == 1);
    memset(&pubkey, 1, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, NULL) == 0);
    CHECK(ecount == 2);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    /* rustsecp256k1_v0_4_0_ec_pubkey_combine tests. */
    ecount = 0;
    pubkeys[0] = &pubkey_one;
    VG_UNDEF(&pubkeys[0], sizeof(rustsecp256k1_v0_4_0_pubkey *));
    VG_UNDEF(&pubkeys[1], sizeof(rustsecp256k1_v0_4_0_pubkey *));
    VG_UNDEF(&pubkeys[2], sizeof(rustsecp256k1_v0_4_0_pubkey *));
    memset(&pubkey, 255, sizeof(rustsecp256k1_v0_4_0_pubkey));
    VG_UNDEF(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_combine(ctx, &pubkey, pubkeys, 0) == 0);
    VG_CHECK(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    CHECK(ecount == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_combine(ctx, NULL, pubkeys, 1) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    CHECK(ecount == 2);
    memset(&pubkey, 255, sizeof(rustsecp256k1_v0_4_0_pubkey));
    VG_UNDEF(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_combine(ctx, &pubkey, NULL, 1) == 0);
    VG_CHECK(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    CHECK(ecount == 3);
    pubkeys[0] = &pubkey_negone;
    memset(&pubkey, 255, sizeof(rustsecp256k1_v0_4_0_pubkey));
    VG_UNDEF(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_combine(ctx, &pubkey, pubkeys, 1) == 1);
    VG_CHECK(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) > 0);
    CHECK(ecount == 3);
    len = 33;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, ctmp, &len, &pubkey, SECP256K1_EC_COMPRESSED) == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, ctmp2, &len, &pubkey_negone, SECP256K1_EC_COMPRESSED) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(ctmp, ctmp2, 33) == 0);
    /* Result is infinity. */
    pubkeys[0] = &pubkey_one;
    pubkeys[1] = &pubkey_negone;
    memset(&pubkey, 255, sizeof(rustsecp256k1_v0_4_0_pubkey));
    VG_UNDEF(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_combine(ctx, &pubkey, pubkeys, 2) == 0);
    VG_CHECK(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) == 0);
    CHECK(ecount == 3);
    /* Passes through infinity but comes out one. */
    pubkeys[2] = &pubkey_one;
    memset(&pubkey, 255, sizeof(rustsecp256k1_v0_4_0_pubkey));
    VG_UNDEF(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_combine(ctx, &pubkey, pubkeys, 3) == 1);
    VG_CHECK(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) > 0);
    CHECK(ecount == 3);
    len = 33;
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, ctmp, &len, &pubkey, SECP256K1_EC_COMPRESSED) == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, ctmp2, &len, &pubkey_one, SECP256K1_EC_COMPRESSED) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(ctmp, ctmp2, 33) == 0);
    /* Adds to two. */
    pubkeys[1] = &pubkey_one;
    memset(&pubkey, 255, sizeof(rustsecp256k1_v0_4_0_pubkey));
    VG_UNDEF(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_combine(ctx, &pubkey, pubkeys, 2) == 1);
    VG_CHECK(&pubkey, sizeof(rustsecp256k1_v0_4_0_pubkey));
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, zeros, sizeof(rustsecp256k1_v0_4_0_pubkey)) > 0);
    CHECK(ecount == 3);
    rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, NULL, NULL);
}

void run_eckey_negate_test(void) {
    unsigned char seckey[32];
    unsigned char seckey_tmp[32];

    random_scalar_order_b32(seckey);
    memcpy(seckey_tmp, seckey, 32);

    /* Verify negation changes the key and changes it back */
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_negate(ctx, seckey) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(seckey, seckey_tmp, 32) != 0);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_negate(ctx, seckey) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(seckey, seckey_tmp, 32) == 0);

    /* Check that privkey alias gives same result */
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_negate(ctx, seckey) == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_privkey_negate(ctx, seckey_tmp) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(seckey, seckey_tmp, 32) == 0);

    /* Negating all 0s fails */
    memset(seckey, 0, 32);
    memset(seckey_tmp, 0, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_negate(ctx, seckey) == 0);
    /* Check that seckey is not modified */
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(seckey, seckey_tmp, 32) == 0);

    /* Negating an overflowing seckey fails and the seckey is zeroed. In this
     * test, the seckey has 16 random bytes to ensure that ec_seckey_negate
     * doesn't just set seckey to a constant value in case of failure. */
    random_scalar_order_b32(seckey);
    memset(seckey, 0xFF, 16);
    memset(seckey_tmp, 0, 32);
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_negate(ctx, seckey) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(seckey, seckey_tmp, 32) == 0);
}

void random_sign(rustsecp256k1_v0_4_0_scalar *sigr, rustsecp256k1_v0_4_0_scalar *sigs, const rustsecp256k1_v0_4_0_scalar *key, const rustsecp256k1_v0_4_0_scalar *msg, int *recid) {
    rustsecp256k1_v0_4_0_scalar nonce;
    do {
        random_scalar_order_test(&nonce);
    } while(!rustsecp256k1_v0_4_0_ecdsa_sig_sign(&ctx->ecmult_gen_ctx, sigr, sigs, key, msg, &nonce, recid));
}

void test_ecdsa_sign_verify(void) {
    rustsecp256k1_v0_4_0_gej pubj;
    rustsecp256k1_v0_4_0_ge pub;
    rustsecp256k1_v0_4_0_scalar one;
    rustsecp256k1_v0_4_0_scalar msg, key;
    rustsecp256k1_v0_4_0_scalar sigr, sigs;
    int recid;
    int getrec;
    random_scalar_order_test(&msg);
    random_scalar_order_test(&key);
    rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &pubj, &key);
    rustsecp256k1_v0_4_0_ge_set_gej(&pub, &pubj);
    getrec = rustsecp256k1_v0_4_0_testrand_bits(1);
    random_sign(&sigr, &sigs, &key, &msg, getrec?&recid:NULL);
    if (getrec) {
        CHECK(recid >= 0 && recid < 4);
    }
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sigr, &sigs, &pub, &msg));
    rustsecp256k1_v0_4_0_scalar_set_int(&one, 1);
    rustsecp256k1_v0_4_0_scalar_add(&msg, &msg, &one);
    CHECK(!rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sigr, &sigs, &pub, &msg));
}

void run_ecdsa_sign_verify(void) {
    int i;
    for (i = 0; i < 10*count; i++) {
        test_ecdsa_sign_verify();
    }
}

/** Dummy nonce generation function that just uses a precomputed nonce, and fails if it is not accepted. Use only for testing. */
static int precomputed_nonce_function(unsigned char *nonce32, const unsigned char *msg32, const unsigned char *key32, const unsigned char *algo16, void *data, unsigned int counter) {
    (void)msg32;
    (void)key32;
    (void)algo16;
    memcpy(nonce32, data, 32);
    return (counter == 0);
}

static int nonce_function_test_fail(unsigned char *nonce32, const unsigned char *msg32, const unsigned char *key32, const unsigned char *algo16, void *data, unsigned int counter) {
   /* Dummy nonce generator that has a fatal error on the first counter value. */
   if (counter == 0) {
       return 0;
   }
   return nonce_function_rfc6979(nonce32, msg32, key32, algo16, data, counter - 1);
}

static int nonce_function_test_retry(unsigned char *nonce32, const unsigned char *msg32, const unsigned char *key32, const unsigned char *algo16, void *data, unsigned int counter) {
   /* Dummy nonce generator that produces unacceptable nonces for the first several counter values. */
   if (counter < 3) {
       memset(nonce32, counter==0 ? 0 : 255, 32);
       if (counter == 2) {
           nonce32[31]--;
       }
       return 1;
   }
   if (counter < 5) {
       static const unsigned char order[] = {
           0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
           0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
           0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,
           0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41
       };
       memcpy(nonce32, order, 32);
       if (counter == 4) {
           nonce32[31]++;
       }
       return 1;
   }
   /* Retry rate of 6979 is negligible esp. as we only call this in deterministic tests. */
   /* If someone does fine a case where it retries for secp256k1, we'd like to know. */
   if (counter > 5) {
       return 0;
   }
   return nonce_function_rfc6979(nonce32, msg32, key32, algo16, data, counter - 5);
}

int is_empty_signature(const rustsecp256k1_v0_4_0_ecdsa_signature *sig) {
    static const unsigned char res[sizeof(rustsecp256k1_v0_4_0_ecdsa_signature)] = {0};
    return rustsecp256k1_v0_4_0_memcmp_var(sig, res, sizeof(rustsecp256k1_v0_4_0_ecdsa_signature)) == 0;
}

void test_ecdsa_end_to_end(void) {
    unsigned char extra[32] = {0x00};
    unsigned char privkey[32];
    unsigned char message[32];
    unsigned char privkey2[32];
    rustsecp256k1_v0_4_0_ecdsa_signature signature[6];
    rustsecp256k1_v0_4_0_scalar r, s;
    unsigned char sig[74];
    size_t siglen = 74;
    unsigned char pubkeyc[65];
    size_t pubkeyclen = 65;
    rustsecp256k1_v0_4_0_pubkey pubkey;
    rustsecp256k1_v0_4_0_pubkey pubkey_tmp;
    unsigned char seckey[300];
    size_t seckeylen = 300;

    /* Generate a random key and message. */
    {
        rustsecp256k1_v0_4_0_scalar msg, key;
        random_scalar_order_test(&msg);
        random_scalar_order_test(&key);
        rustsecp256k1_v0_4_0_scalar_get_b32(privkey, &key);
        rustsecp256k1_v0_4_0_scalar_get_b32(message, &msg);
    }

    /* Construct and verify corresponding public key. */
    CHECK(rustsecp256k1_v0_4_0_ec_seckey_verify(ctx, privkey) == 1);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, privkey) == 1);

    /* Verify exporting and importing public key. */
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_serialize(ctx, pubkeyc, &pubkeyclen, &pubkey, rustsecp256k1_v0_4_0_testrand_bits(1) == 1 ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED));
    memset(&pubkey, 0, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_parse(ctx, &pubkey, pubkeyc, pubkeyclen) == 1);

    /* Verify negation changes the key and changes it back */
    memcpy(&pubkey_tmp, &pubkey, sizeof(pubkey));
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_negate(ctx, &pubkey_tmp) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey_tmp, &pubkey, sizeof(pubkey)) != 0);
    CHECK(rustsecp256k1_v0_4_0_ec_pubkey_negate(ctx, &pubkey_tmp) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey_tmp, &pubkey, sizeof(pubkey)) == 0);

    /* Verify private key import and export. */
    CHECK(ec_privkey_export_der(ctx, seckey, &seckeylen, privkey, rustsecp256k1_v0_4_0_testrand_bits(1) == 1));
    CHECK(ec_privkey_import_der(ctx, privkey2, seckey, seckeylen) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(privkey, privkey2, 32) == 0);

    /* Optionally tweak the keys using addition. */
    if (rustsecp256k1_v0_4_0_testrand_int(3) == 0) {
        int ret1;
        int ret2;
        int ret3;
        unsigned char rnd[32];
        unsigned char privkey_tmp[32];
        rustsecp256k1_v0_4_0_pubkey pubkey2;
        rustsecp256k1_v0_4_0_testrand256_test(rnd);
        memcpy(privkey_tmp, privkey, 32);
        ret1 = rustsecp256k1_v0_4_0_ec_seckey_tweak_add(ctx, privkey, rnd);
        ret2 = rustsecp256k1_v0_4_0_ec_pubkey_tweak_add(ctx, &pubkey, rnd);
        /* Check that privkey alias gives same result */
        ret3 = rustsecp256k1_v0_4_0_ec_privkey_tweak_add(ctx, privkey_tmp, rnd);
        CHECK(ret1 == ret2);
        CHECK(ret2 == ret3);
        if (ret1 == 0) {
            return;
        }
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(privkey, privkey_tmp, 32) == 0);
        CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey2, privkey) == 1);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, &pubkey2, sizeof(pubkey)) == 0);
    }

    /* Optionally tweak the keys using multiplication. */
    if (rustsecp256k1_v0_4_0_testrand_int(3) == 0) {
        int ret1;
        int ret2;
        int ret3;
        unsigned char rnd[32];
        unsigned char privkey_tmp[32];
        rustsecp256k1_v0_4_0_pubkey pubkey2;
        rustsecp256k1_v0_4_0_testrand256_test(rnd);
        memcpy(privkey_tmp, privkey, 32);
        ret1 = rustsecp256k1_v0_4_0_ec_seckey_tweak_mul(ctx, privkey, rnd);
        ret2 = rustsecp256k1_v0_4_0_ec_pubkey_tweak_mul(ctx, &pubkey, rnd);
        /* Check that privkey alias gives same result */
        ret3 = rustsecp256k1_v0_4_0_ec_privkey_tweak_mul(ctx, privkey_tmp, rnd);
        CHECK(ret1 == ret2);
        CHECK(ret2 == ret3);
        if (ret1 == 0) {
            return;
        }
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(privkey, privkey_tmp, 32) == 0);
        CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey2, privkey) == 1);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(&pubkey, &pubkey2, sizeof(pubkey)) == 0);
    }

    /* Sign. */
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &signature[0], message, privkey, NULL, NULL) == 1);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &signature[4], message, privkey, NULL, NULL) == 1);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &signature[1], message, privkey, NULL, extra) == 1);
    extra[31] = 1;
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &signature[2], message, privkey, NULL, extra) == 1);
    extra[31] = 0;
    extra[0] = 1;
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &signature[3], message, privkey, NULL, extra) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&signature[0], &signature[4], sizeof(signature[0])) == 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&signature[0], &signature[1], sizeof(signature[0])) != 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&signature[0], &signature[2], sizeof(signature[0])) != 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&signature[0], &signature[3], sizeof(signature[0])) != 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&signature[1], &signature[2], sizeof(signature[0])) != 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&signature[1], &signature[3], sizeof(signature[0])) != 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&signature[2], &signature[3], sizeof(signature[0])) != 0);
    /* Verify. */
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[0], message, &pubkey) == 1);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[1], message, &pubkey) == 1);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[2], message, &pubkey) == 1);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[3], message, &pubkey) == 1);
    /* Test lower-S form, malleate, verify and fail, test again, malleate again */
    CHECK(!rustsecp256k1_v0_4_0_ecdsa_signature_normalize(ctx, NULL, &signature[0]));
    rustsecp256k1_v0_4_0_ecdsa_signature_load(ctx, &r, &s, &signature[0]);
    rustsecp256k1_v0_4_0_scalar_negate(&s, &s);
    rustsecp256k1_v0_4_0_ecdsa_signature_save(&signature[5], &r, &s);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[5], message, &pubkey) == 0);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_normalize(ctx, NULL, &signature[5]));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_normalize(ctx, &signature[5], &signature[5]));
    CHECK(!rustsecp256k1_v0_4_0_ecdsa_signature_normalize(ctx, NULL, &signature[5]));
    CHECK(!rustsecp256k1_v0_4_0_ecdsa_signature_normalize(ctx, &signature[5], &signature[5]));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[5], message, &pubkey) == 1);
    rustsecp256k1_v0_4_0_scalar_negate(&s, &s);
    rustsecp256k1_v0_4_0_ecdsa_signature_save(&signature[5], &r, &s);
    CHECK(!rustsecp256k1_v0_4_0_ecdsa_signature_normalize(ctx, NULL, &signature[5]));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[5], message, &pubkey) == 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&signature[5], &signature[0], 64) == 0);

    /* Serialize/parse DER and verify again */
    CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, sig, &siglen, &signature[0]) == 1);
    memset(&signature[0], 0, sizeof(signature[0]));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_der(ctx, &signature[0], sig, siglen) == 1);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[0], message, &pubkey) == 1);
    /* Serialize/destroy/parse DER and verify again. */
    siglen = 74;
    CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, sig, &siglen, &signature[0]) == 1);
    sig[rustsecp256k1_v0_4_0_testrand_int(siglen)] += 1 + rustsecp256k1_v0_4_0_testrand_int(255);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_der(ctx, &signature[0], sig, siglen) == 0 ||
          rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &signature[0], message, &pubkey) == 0);
}

void test_random_pubkeys(void) {
    rustsecp256k1_v0_4_0_ge elem;
    rustsecp256k1_v0_4_0_ge elem2;
    unsigned char in[65];
    /* Generate some randomly sized pubkeys. */
    size_t len = rustsecp256k1_v0_4_0_testrand_bits(2) == 0 ? 65 : 33;
    if (rustsecp256k1_v0_4_0_testrand_bits(2) == 0) {
        len = rustsecp256k1_v0_4_0_testrand_bits(6);
    }
    if (len == 65) {
      in[0] = rustsecp256k1_v0_4_0_testrand_bits(1) ? 4 : (rustsecp256k1_v0_4_0_testrand_bits(1) ? 6 : 7);
    } else {
      in[0] = rustsecp256k1_v0_4_0_testrand_bits(1) ? 2 : 3;
    }
    if (rustsecp256k1_v0_4_0_testrand_bits(3) == 0) {
        in[0] = rustsecp256k1_v0_4_0_testrand_bits(8);
    }
    if (len > 1) {
        rustsecp256k1_v0_4_0_testrand256(&in[1]);
    }
    if (len > 33) {
        rustsecp256k1_v0_4_0_testrand256(&in[33]);
    }
    if (rustsecp256k1_v0_4_0_eckey_pubkey_parse(&elem, in, len)) {
        unsigned char out[65];
        unsigned char firstb;
        int res;
        size_t size = len;
        firstb = in[0];
        /* If the pubkey can be parsed, it should round-trip... */
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_serialize(&elem, out, &size, len == 33));
        CHECK(size == len);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(&in[1], &out[1], len-1) == 0);
        /* ... except for the type of hybrid inputs. */
        if ((in[0] != 6) && (in[0] != 7)) {
            CHECK(in[0] == out[0]);
        }
        size = 65;
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_serialize(&elem, in, &size, 0));
        CHECK(size == 65);
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_parse(&elem2, in, size));
        ge_equals_ge(&elem,&elem2);
        /* Check that the X9.62 hybrid type is checked. */
        in[0] = rustsecp256k1_v0_4_0_testrand_bits(1) ? 6 : 7;
        res = rustsecp256k1_v0_4_0_eckey_pubkey_parse(&elem2, in, size);
        if (firstb == 2 || firstb == 3) {
            if (in[0] == firstb + 4) {
              CHECK(res);
            } else {
              CHECK(!res);
            }
        }
        if (res) {
            ge_equals_ge(&elem,&elem2);
            CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_serialize(&elem, out, &size, 0));
            CHECK(rustsecp256k1_v0_4_0_memcmp_var(&in[1], &out[1], 64) == 0);
        }
    }
}

void run_random_pubkeys(void) {
    int i;
    for (i = 0; i < 10*count; i++) {
        test_random_pubkeys();
    }
}

void run_ecdsa_end_to_end(void) {
    int i;
    for (i = 0; i < 64*count; i++) {
        test_ecdsa_end_to_end();
    }
}

int test_ecdsa_der_parse(const unsigned char *sig, size_t siglen, int certainly_der, int certainly_not_der) {
    static const unsigned char zeroes[32] = {0};
#ifdef ENABLE_OPENSSL_TESTS
    static const unsigned char max_scalar[32] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
        0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
        0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x40
    };
#endif

    int ret = 0;

    rustsecp256k1_v0_4_0_ecdsa_signature sig_der;
    unsigned char roundtrip_der[2048];
    unsigned char compact_der[64];
    size_t len_der = 2048;
    int parsed_der = 0, valid_der = 0, roundtrips_der = 0;

    rustsecp256k1_v0_4_0_ecdsa_signature sig_der_lax;
    unsigned char roundtrip_der_lax[2048];
    unsigned char compact_der_lax[64];
    size_t len_der_lax = 2048;
    int parsed_der_lax = 0, valid_der_lax = 0, roundtrips_der_lax = 0;

#ifdef ENABLE_OPENSSL_TESTS
    ECDSA_SIG *sig_openssl;
    const BIGNUM *r = NULL, *s = NULL;
    const unsigned char *sigptr;
    unsigned char roundtrip_openssl[2048];
    int len_openssl = 2048;
    int parsed_openssl, valid_openssl = 0, roundtrips_openssl = 0;
#endif

    parsed_der = rustsecp256k1_v0_4_0_ecdsa_signature_parse_der(ctx, &sig_der, sig, siglen);
    if (parsed_der) {
        ret |= (!rustsecp256k1_v0_4_0_ecdsa_signature_serialize_compact(ctx, compact_der, &sig_der)) << 0;
        valid_der = (rustsecp256k1_v0_4_0_memcmp_var(compact_der, zeroes, 32) != 0) && (rustsecp256k1_v0_4_0_memcmp_var(compact_der + 32, zeroes, 32) != 0);
    }
    if (valid_der) {
        ret |= (!rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, roundtrip_der, &len_der, &sig_der)) << 1;
        roundtrips_der = (len_der == siglen) && rustsecp256k1_v0_4_0_memcmp_var(roundtrip_der, sig, siglen) == 0;
    }

    parsed_der_lax = rustsecp256k1_v0_4_0_ecdsa_signature_parse_der_lax(ctx, &sig_der_lax, sig, siglen);
    if (parsed_der_lax) {
        ret |= (!rustsecp256k1_v0_4_0_ecdsa_signature_serialize_compact(ctx, compact_der_lax, &sig_der_lax)) << 10;
        valid_der_lax = (rustsecp256k1_v0_4_0_memcmp_var(compact_der_lax, zeroes, 32) != 0) && (rustsecp256k1_v0_4_0_memcmp_var(compact_der_lax + 32, zeroes, 32) != 0);
    }
    if (valid_der_lax) {
        ret |= (!rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, roundtrip_der_lax, &len_der_lax, &sig_der_lax)) << 11;
        roundtrips_der_lax = (len_der_lax == siglen) && rustsecp256k1_v0_4_0_memcmp_var(roundtrip_der_lax, sig, siglen) == 0;
    }

    if (certainly_der) {
        ret |= (!parsed_der) << 2;
    }
    if (certainly_not_der) {
        ret |= (parsed_der) << 17;
    }
    if (valid_der) {
        ret |= (!roundtrips_der) << 3;
    }

    if (valid_der) {
        ret |= (!roundtrips_der_lax) << 12;
        ret |= (len_der != len_der_lax) << 13;
        ret |= ((len_der != len_der_lax) || (rustsecp256k1_v0_4_0_memcmp_var(roundtrip_der_lax, roundtrip_der, len_der) != 0)) << 14;
    }
    ret |= (roundtrips_der != roundtrips_der_lax) << 15;
    if (parsed_der) {
        ret |= (!parsed_der_lax) << 16;
    }

#ifdef ENABLE_OPENSSL_TESTS
    sig_openssl = ECDSA_SIG_new();
    sigptr = sig;
    parsed_openssl = (d2i_ECDSA_SIG(&sig_openssl, &sigptr, siglen) != NULL);
    if (parsed_openssl) {
        ECDSA_SIG_get0(sig_openssl, &r, &s);
        valid_openssl = !BN_is_negative(r) && !BN_is_negative(s) && BN_num_bits(r) > 0 && BN_num_bits(r) <= 256 && BN_num_bits(s) > 0 && BN_num_bits(s) <= 256;
        if (valid_openssl) {
            unsigned char tmp[32] = {0};
            BN_bn2bin(r, tmp + 32 - BN_num_bytes(r));
            valid_openssl = rustsecp256k1_v0_4_0_memcmp_var(tmp, max_scalar, 32) < 0;
        }
        if (valid_openssl) {
            unsigned char tmp[32] = {0};
            BN_bn2bin(s, tmp + 32 - BN_num_bytes(s));
            valid_openssl = rustsecp256k1_v0_4_0_memcmp_var(tmp, max_scalar, 32) < 0;
        }
    }
    len_openssl = i2d_ECDSA_SIG(sig_openssl, NULL);
    if (len_openssl <= 2048) {
        unsigned char *ptr = roundtrip_openssl;
        CHECK(i2d_ECDSA_SIG(sig_openssl, &ptr) == len_openssl);
        roundtrips_openssl = valid_openssl && ((size_t)len_openssl == siglen) && (rustsecp256k1_v0_4_0_memcmp_var(roundtrip_openssl, sig, siglen) == 0);
    } else {
        len_openssl = 0;
    }
    ECDSA_SIG_free(sig_openssl);

    ret |= (parsed_der && !parsed_openssl) << 4;
    ret |= (valid_der && !valid_openssl) << 5;
    ret |= (roundtrips_openssl && !parsed_der) << 6;
    ret |= (roundtrips_der != roundtrips_openssl) << 7;
    if (roundtrips_openssl) {
        ret |= (len_der != (size_t)len_openssl) << 8;
        ret |= ((len_der != (size_t)len_openssl) || (rustsecp256k1_v0_4_0_memcmp_var(roundtrip_der, roundtrip_openssl, len_der) != 0)) << 9;
    }
#endif
    return ret;
}

static void assign_big_endian(unsigned char *ptr, size_t ptrlen, uint32_t val) {
    size_t i;
    for (i = 0; i < ptrlen; i++) {
        int shift = ptrlen - 1 - i;
        if (shift >= 4) {
            ptr[i] = 0;
        } else {
            ptr[i] = (val >> shift) & 0xFF;
        }
    }
}

static void damage_array(unsigned char *sig, size_t *len) {
    int pos;
    int action = rustsecp256k1_v0_4_0_testrand_bits(3);
    if (action < 1 && *len > 3) {
        /* Delete a byte. */
        pos = rustsecp256k1_v0_4_0_testrand_int(*len);
        memmove(sig + pos, sig + pos + 1, *len - pos - 1);
        (*len)--;
        return;
    } else if (action < 2 && *len < 2048) {
        /* Insert a byte. */
        pos = rustsecp256k1_v0_4_0_testrand_int(1 + *len);
        memmove(sig + pos + 1, sig + pos, *len - pos);
        sig[pos] = rustsecp256k1_v0_4_0_testrand_bits(8);
        (*len)++;
        return;
    } else if (action < 4) {
        /* Modify a byte. */
        sig[rustsecp256k1_v0_4_0_testrand_int(*len)] += 1 + rustsecp256k1_v0_4_0_testrand_int(255);
        return;
    } else { /* action < 8 */
        /* Modify a bit. */
        sig[rustsecp256k1_v0_4_0_testrand_int(*len)] ^= 1 << rustsecp256k1_v0_4_0_testrand_bits(3);
        return;
    }
}

static void random_ber_signature(unsigned char *sig, size_t *len, int* certainly_der, int* certainly_not_der) {
    int der;
    int nlow[2], nlen[2], nlenlen[2], nhbit[2], nhbyte[2], nzlen[2];
    size_t tlen, elen, glen;
    int indet;
    int n;

    *len = 0;
    der = rustsecp256k1_v0_4_0_testrand_bits(2) == 0;
    *certainly_der = der;
    *certainly_not_der = 0;
    indet = der ? 0 : rustsecp256k1_v0_4_0_testrand_int(10) == 0;

    for (n = 0; n < 2; n++) {
        /* We generate two classes of numbers: nlow==1 "low" ones (up to 32 bytes), nlow==0 "high" ones (32 bytes with 129 top bits set, or larger than 32 bytes) */
        nlow[n] = der ? 1 : (rustsecp256k1_v0_4_0_testrand_bits(3) != 0);
        /* The length of the number in bytes (the first byte of which will always be nonzero) */
        nlen[n] = nlow[n] ? rustsecp256k1_v0_4_0_testrand_int(33) : 32 + rustsecp256k1_v0_4_0_testrand_int(200) * rustsecp256k1_v0_4_0_testrand_int(8) / 8;
        CHECK(nlen[n] <= 232);
        /* The top bit of the number. */
        nhbit[n] = (nlow[n] == 0 && nlen[n] == 32) ? 1 : (nlen[n] == 0 ? 0 : rustsecp256k1_v0_4_0_testrand_bits(1));
        /* The top byte of the number (after the potential hardcoded 16 0xFF characters for "high" 32 bytes numbers) */
        nhbyte[n] = nlen[n] == 0 ? 0 : (nhbit[n] ? 128 + rustsecp256k1_v0_4_0_testrand_bits(7) : 1 + rustsecp256k1_v0_4_0_testrand_int(127));
        /* The number of zero bytes in front of the number (which is 0 or 1 in case of DER, otherwise we extend up to 300 bytes) */
        nzlen[n] = der ? ((nlen[n] == 0 || nhbit[n]) ? 1 : 0) : (nlow[n] ? rustsecp256k1_v0_4_0_testrand_int(3) : rustsecp256k1_v0_4_0_testrand_int(300 - nlen[n]) * rustsecp256k1_v0_4_0_testrand_int(8) / 8);
        if (nzlen[n] > ((nlen[n] == 0 || nhbit[n]) ? 1 : 0)) {
            *certainly_not_der = 1;
        }
        CHECK(nlen[n] + nzlen[n] <= 300);
        /* The length of the length descriptor for the number. 0 means short encoding, anything else is long encoding. */
        nlenlen[n] = nlen[n] + nzlen[n] < 128 ? 0 : (nlen[n] + nzlen[n] < 256 ? 1 : 2);
        if (!der) {
            /* nlenlen[n] max 127 bytes */
            int add = rustsecp256k1_v0_4_0_testrand_int(127 - nlenlen[n]) * rustsecp256k1_v0_4_0_testrand_int(16) * rustsecp256k1_v0_4_0_testrand_int(16) / 256;
            nlenlen[n] += add;
            if (add != 0) {
                *certainly_not_der = 1;
            }
        }
        CHECK(nlen[n] + nzlen[n] + nlenlen[n] <= 427);
    }

    /* The total length of the data to go, so far */
    tlen = 2 + nlenlen[0] + nlen[0] + nzlen[0] + 2 + nlenlen[1] + nlen[1] + nzlen[1];
    CHECK(tlen <= 856);

    /* The length of the garbage inside the tuple. */
    elen = (der || indet) ? 0 : rustsecp256k1_v0_4_0_testrand_int(980 - tlen) * rustsecp256k1_v0_4_0_testrand_int(8) / 8;
    if (elen != 0) {
        *certainly_not_der = 1;
    }
    tlen += elen;
    CHECK(tlen <= 980);

    /* The length of the garbage after the end of the tuple. */
    glen = der ? 0 : rustsecp256k1_v0_4_0_testrand_int(990 - tlen) * rustsecp256k1_v0_4_0_testrand_int(8) / 8;
    if (glen != 0) {
        *certainly_not_der = 1;
    }
    CHECK(tlen + glen <= 990);

    /* Write the tuple header. */
    sig[(*len)++] = 0x30;
    if (indet) {
        /* Indeterminate length */
        sig[(*len)++] = 0x80;
        *certainly_not_der = 1;
    } else {
        int tlenlen = tlen < 128 ? 0 : (tlen < 256 ? 1 : 2);
        if (!der) {
            int add = rustsecp256k1_v0_4_0_testrand_int(127 - tlenlen) * rustsecp256k1_v0_4_0_testrand_int(16) * rustsecp256k1_v0_4_0_testrand_int(16) / 256;
            tlenlen += add;
            if (add != 0) {
                *certainly_not_der = 1;
            }
        }
        if (tlenlen == 0) {
            /* Short length notation */
            sig[(*len)++] = tlen;
        } else {
            /* Long length notation */
            sig[(*len)++] = 128 + tlenlen;
            assign_big_endian(sig + *len, tlenlen, tlen);
            *len += tlenlen;
        }
        tlen += tlenlen;
    }
    tlen += 2;
    CHECK(tlen + glen <= 1119);

    for (n = 0; n < 2; n++) {
        /* Write the integer header. */
        sig[(*len)++] = 0x02;
        if (nlenlen[n] == 0) {
            /* Short length notation */
            sig[(*len)++] = nlen[n] + nzlen[n];
        } else {
            /* Long length notation. */
            sig[(*len)++] = 128 + nlenlen[n];
            assign_big_endian(sig + *len, nlenlen[n], nlen[n] + nzlen[n]);
            *len += nlenlen[n];
        }
        /* Write zero padding */
        while (nzlen[n] > 0) {
            sig[(*len)++] = 0x00;
            nzlen[n]--;
        }
        if (nlen[n] == 32 && !nlow[n]) {
            /* Special extra 16 0xFF bytes in "high" 32-byte numbers */
            int i;
            for (i = 0; i < 16; i++) {
                sig[(*len)++] = 0xFF;
            }
            nlen[n] -= 16;
        }
        /* Write first byte of number */
        if (nlen[n] > 0) {
            sig[(*len)++] = nhbyte[n];
            nlen[n]--;
        }
        /* Generate remaining random bytes of number */
        rustsecp256k1_v0_4_0_testrand_bytes_test(sig + *len, nlen[n]);
        *len += nlen[n];
        nlen[n] = 0;
    }

    /* Generate random garbage inside tuple. */
    rustsecp256k1_v0_4_0_testrand_bytes_test(sig + *len, elen);
    *len += elen;

    /* Generate end-of-contents bytes. */
    if (indet) {
        sig[(*len)++] = 0;
        sig[(*len)++] = 0;
        tlen += 2;
    }
    CHECK(tlen + glen <= 1121);

    /* Generate random garbage outside tuple. */
    rustsecp256k1_v0_4_0_testrand_bytes_test(sig + *len, glen);
    *len += glen;
    tlen += glen;
    CHECK(tlen <= 1121);
    CHECK(tlen == *len);
}

void run_ecdsa_der_parse(void) {
    int i,j;
    for (i = 0; i < 200 * count; i++) {
        unsigned char buffer[2048];
        size_t buflen = 0;
        int certainly_der = 0;
        int certainly_not_der = 0;
        random_ber_signature(buffer, &buflen, &certainly_der, &certainly_not_der);
        CHECK(buflen <= 2048);
        for (j = 0; j < 16; j++) {
            int ret = 0;
            if (j > 0) {
                damage_array(buffer, &buflen);
                /* We don't know anything anymore about the DERness of the result */
                certainly_der = 0;
                certainly_not_der = 0;
            }
            ret = test_ecdsa_der_parse(buffer, buflen, certainly_der, certainly_not_der);
            if (ret != 0) {
                size_t k;
                fprintf(stderr, "Failure %x on ", ret);
                for (k = 0; k < buflen; k++) {
                    fprintf(stderr, "%02x ", buffer[k]);
                }
                fprintf(stderr, "\n");
            }
            CHECK(ret == 0);
        }
    }
}

/* Tests several edge cases. */
void test_ecdsa_edge_cases(void) {
    int t;
    rustsecp256k1_v0_4_0_ecdsa_signature sig;

    /* Test the case where ECDSA recomputes a point that is infinity. */
    {
        rustsecp256k1_v0_4_0_gej keyj;
        rustsecp256k1_v0_4_0_ge key;
        rustsecp256k1_v0_4_0_scalar msg;
        rustsecp256k1_v0_4_0_scalar sr, ss;
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 1);
        rustsecp256k1_v0_4_0_scalar_negate(&ss, &ss);
        rustsecp256k1_v0_4_0_scalar_inverse(&ss, &ss);
        rustsecp256k1_v0_4_0_scalar_set_int(&sr, 1);
        rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &keyj, &sr);
        rustsecp256k1_v0_4_0_ge_set_gej(&key, &keyj);
        msg = ss;
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 0);
    }

    /* Verify signature with r of zero fails. */
    {
        const unsigned char pubkey_mods_zero[33] = {
            0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xfe, 0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0,
            0x3b, 0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41,
            0x41
        };
        rustsecp256k1_v0_4_0_ge key;
        rustsecp256k1_v0_4_0_scalar msg;
        rustsecp256k1_v0_4_0_scalar sr, ss;
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 1);
        rustsecp256k1_v0_4_0_scalar_set_int(&msg, 0);
        rustsecp256k1_v0_4_0_scalar_set_int(&sr, 0);
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_parse(&key, pubkey_mods_zero, 33));
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 0);
    }

    /* Verify signature with s of zero fails. */
    {
        const unsigned char pubkey[33] = {
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01
        };
        rustsecp256k1_v0_4_0_ge key;
        rustsecp256k1_v0_4_0_scalar msg;
        rustsecp256k1_v0_4_0_scalar sr, ss;
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 0);
        rustsecp256k1_v0_4_0_scalar_set_int(&msg, 0);
        rustsecp256k1_v0_4_0_scalar_set_int(&sr, 1);
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_parse(&key, pubkey, 33));
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 0);
    }

    /* Verify signature with message 0 passes. */
    {
        const unsigned char pubkey[33] = {
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02
        };
        const unsigned char pubkey2[33] = {
            0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xfe, 0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0,
            0x3b, 0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41,
            0x43
        };
        rustsecp256k1_v0_4_0_ge key;
        rustsecp256k1_v0_4_0_ge key2;
        rustsecp256k1_v0_4_0_scalar msg;
        rustsecp256k1_v0_4_0_scalar sr, ss;
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 2);
        rustsecp256k1_v0_4_0_scalar_set_int(&msg, 0);
        rustsecp256k1_v0_4_0_scalar_set_int(&sr, 2);
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_parse(&key, pubkey, 33));
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_parse(&key2, pubkey2, 33));
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 1);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key2, &msg) == 1);
        rustsecp256k1_v0_4_0_scalar_negate(&ss, &ss);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 1);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key2, &msg) == 1);
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 1);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 0);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key2, &msg) == 0);
    }

    /* Verify signature with message 1 passes. */
    {
        const unsigned char pubkey[33] = {
            0x02, 0x14, 0x4e, 0x5a, 0x58, 0xef, 0x5b, 0x22,
            0x6f, 0xd2, 0xe2, 0x07, 0x6a, 0x77, 0xcf, 0x05,
            0xb4, 0x1d, 0xe7, 0x4a, 0x30, 0x98, 0x27, 0x8c,
            0x93, 0xe6, 0xe6, 0x3c, 0x0b, 0xc4, 0x73, 0x76,
            0x25
        };
        const unsigned char pubkey2[33] = {
            0x02, 0x8a, 0xd5, 0x37, 0xed, 0x73, 0xd9, 0x40,
            0x1d, 0xa0, 0x33, 0xd2, 0xdc, 0xf0, 0xaf, 0xae,
            0x34, 0xcf, 0x5f, 0x96, 0x4c, 0x73, 0x28, 0x0f,
            0x92, 0xc0, 0xf6, 0x9d, 0xd9, 0xb2, 0x09, 0x10,
            0x62
        };
        const unsigned char csr[32] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x45, 0x51, 0x23, 0x19, 0x50, 0xb7, 0x5f, 0xc4,
            0x40, 0x2d, 0xa1, 0x72, 0x2f, 0xc9, 0xba, 0xeb
        };
        rustsecp256k1_v0_4_0_ge key;
        rustsecp256k1_v0_4_0_ge key2;
        rustsecp256k1_v0_4_0_scalar msg;
        rustsecp256k1_v0_4_0_scalar sr, ss;
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 1);
        rustsecp256k1_v0_4_0_scalar_set_int(&msg, 1);
        rustsecp256k1_v0_4_0_scalar_set_b32(&sr, csr, NULL);
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_parse(&key, pubkey, 33));
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_parse(&key2, pubkey2, 33));
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 1);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key2, &msg) == 1);
        rustsecp256k1_v0_4_0_scalar_negate(&ss, &ss);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 1);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key2, &msg) == 1);
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 2);
        rustsecp256k1_v0_4_0_scalar_inverse_var(&ss, &ss);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 0);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key2, &msg) == 0);
    }

    /* Verify signature with message -1 passes. */
    {
        const unsigned char pubkey[33] = {
            0x03, 0xaf, 0x97, 0xff, 0x7d, 0x3a, 0xf6, 0xa0,
            0x02, 0x94, 0xbd, 0x9f, 0x4b, 0x2e, 0xd7, 0x52,
            0x28, 0xdb, 0x49, 0x2a, 0x65, 0xcb, 0x1e, 0x27,
            0x57, 0x9c, 0xba, 0x74, 0x20, 0xd5, 0x1d, 0x20,
            0xf1
        };
        const unsigned char csr[32] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x45, 0x51, 0x23, 0x19, 0x50, 0xb7, 0x5f, 0xc4,
            0x40, 0x2d, 0xa1, 0x72, 0x2f, 0xc9, 0xba, 0xee
        };
        rustsecp256k1_v0_4_0_ge key;
        rustsecp256k1_v0_4_0_scalar msg;
        rustsecp256k1_v0_4_0_scalar sr, ss;
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 1);
        rustsecp256k1_v0_4_0_scalar_set_int(&msg, 1);
        rustsecp256k1_v0_4_0_scalar_negate(&msg, &msg);
        rustsecp256k1_v0_4_0_scalar_set_b32(&sr, csr, NULL);
        CHECK(rustsecp256k1_v0_4_0_eckey_pubkey_parse(&key, pubkey, 33));
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 1);
        rustsecp256k1_v0_4_0_scalar_negate(&ss, &ss);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 1);
        rustsecp256k1_v0_4_0_scalar_set_int(&ss, 3);
        rustsecp256k1_v0_4_0_scalar_inverse_var(&ss, &ss);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sr, &ss, &key, &msg) == 0);
    }

    /* Signature where s would be zero. */
    {
        rustsecp256k1_v0_4_0_pubkey pubkey;
        size_t siglen;
        int32_t ecount;
        unsigned char signature[72];
        static const unsigned char nonce[32] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        };
        static const unsigned char nonce2[32] = {
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
            0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,
            0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x40
        };
        const unsigned char key[32] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        };
        unsigned char msg[32] = {
            0x86, 0x41, 0x99, 0x81, 0x06, 0x23, 0x44, 0x53,
            0xaa, 0x5f, 0x9d, 0x6a, 0x31, 0x78, 0xf4, 0xf7,
            0xb8, 0x12, 0xe0, 0x0b, 0x81, 0x7a, 0x77, 0x62,
            0x65, 0xdf, 0xdd, 0x31, 0xb9, 0x3e, 0x29, 0xa9,
        };
        ecount = 0;
        rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, counting_illegal_callback_fn, &ecount);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, key, precomputed_nonce_function, nonce) == 0);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, key, precomputed_nonce_function, nonce2) == 0);
        msg[31] = 0xaa;
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, key, precomputed_nonce_function, nonce) == 1);
        CHECK(ecount == 0);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, NULL, msg, key, precomputed_nonce_function, nonce2) == 0);
        CHECK(ecount == 1);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, NULL, key, precomputed_nonce_function, nonce2) == 0);
        CHECK(ecount == 2);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, NULL, precomputed_nonce_function, nonce2) == 0);
        CHECK(ecount == 3);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, key, precomputed_nonce_function, nonce2) == 1);
        CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, key) == 1);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, NULL, msg, &pubkey) == 0);
        CHECK(ecount == 4);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &sig, NULL, &pubkey) == 0);
        CHECK(ecount == 5);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &sig, msg, NULL) == 0);
        CHECK(ecount == 6);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &sig, msg, &pubkey) == 1);
        CHECK(ecount == 6);
        CHECK(rustsecp256k1_v0_4_0_ec_pubkey_create(ctx, &pubkey, NULL) == 0);
        CHECK(ecount == 7);
        /* That pubkeyload fails via an ARGCHECK is a little odd but makes sense because pubkeys are an opaque data type. */
        CHECK(rustsecp256k1_v0_4_0_ecdsa_verify(ctx, &sig, msg, &pubkey) == 0);
        CHECK(ecount == 8);
        siglen = 72;
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, NULL, &siglen, &sig) == 0);
        CHECK(ecount == 9);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, signature, NULL, &sig) == 0);
        CHECK(ecount == 10);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, signature, &siglen, NULL) == 0);
        CHECK(ecount == 11);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, signature, &siglen, &sig) == 1);
        CHECK(ecount == 11);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_der(ctx, NULL, signature, siglen) == 0);
        CHECK(ecount == 12);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_der(ctx, &sig, NULL, siglen) == 0);
        CHECK(ecount == 13);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_der(ctx, &sig, signature, siglen) == 1);
        CHECK(ecount == 13);
        siglen = 10;
        /* Too little room for a signature does not fail via ARGCHECK. */
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_der(ctx, signature, &siglen, &sig) == 0);
        CHECK(ecount == 13);
        ecount = 0;
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_normalize(ctx, NULL, NULL) == 0);
        CHECK(ecount == 1);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_compact(ctx, NULL, &sig) == 0);
        CHECK(ecount == 2);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_compact(ctx, signature, NULL) == 0);
        CHECK(ecount == 3);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_serialize_compact(ctx, signature, &sig) == 1);
        CHECK(ecount == 3);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_compact(ctx, NULL, signature) == 0);
        CHECK(ecount == 4);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_compact(ctx, &sig, NULL) == 0);
        CHECK(ecount == 5);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_compact(ctx, &sig, signature) == 1);
        CHECK(ecount == 5);
        memset(signature, 255, 64);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_signature_parse_compact(ctx, &sig, signature) == 0);
        CHECK(ecount == 5);
        rustsecp256k1_v0_4_0_context_set_illegal_callback(ctx, NULL, NULL);
    }

    /* Nonce function corner cases. */
    for (t = 0; t < 2; t++) {
        static const unsigned char zero[32] = {0x00};
        int i;
        unsigned char key[32];
        unsigned char msg[32];
        rustsecp256k1_v0_4_0_ecdsa_signature sig2;
        rustsecp256k1_v0_4_0_scalar sr[512], ss;
        const unsigned char *extra;
        extra = t == 0 ? NULL : zero;
        memset(msg, 0, 32);
        msg[31] = 1;
        /* High key results in signature failure. */
        memset(key, 0xFF, 32);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, key, NULL, extra) == 0);
        CHECK(is_empty_signature(&sig));
        /* Zero key results in signature failure. */
        memset(key, 0, 32);
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, key, NULL, extra) == 0);
        CHECK(is_empty_signature(&sig));
        /* Nonce function failure results in signature failure. */
        key[31] = 1;
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, key, nonce_function_test_fail, extra) == 0);
        CHECK(is_empty_signature(&sig));
        /* The retry loop successfully makes its way to the first good value. */
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig, msg, key, nonce_function_test_retry, extra) == 1);
        CHECK(!is_empty_signature(&sig));
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig2, msg, key, nonce_function_rfc6979, extra) == 1);
        CHECK(!is_empty_signature(&sig2));
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(&sig, &sig2, sizeof(sig)) == 0);
        /* The default nonce function is deterministic. */
        CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig2, msg, key, NULL, extra) == 1);
        CHECK(!is_empty_signature(&sig2));
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(&sig, &sig2, sizeof(sig)) == 0);
        /* The default nonce function changes output with different messages. */
        for(i = 0; i < 256; i++) {
            int j;
            msg[0] = i;
            CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig2, msg, key, NULL, extra) == 1);
            CHECK(!is_empty_signature(&sig2));
            rustsecp256k1_v0_4_0_ecdsa_signature_load(ctx, &sr[i], &ss, &sig2);
            for (j = 0; j < i; j++) {
                CHECK(!rustsecp256k1_v0_4_0_scalar_eq(&sr[i], &sr[j]));
            }
        }
        msg[0] = 0;
        msg[31] = 2;
        /* The default nonce function changes output with different keys. */
        for(i = 256; i < 512; i++) {
            int j;
            key[0] = i - 256;
            CHECK(rustsecp256k1_v0_4_0_ecdsa_sign(ctx, &sig2, msg, key, NULL, extra) == 1);
            CHECK(!is_empty_signature(&sig2));
            rustsecp256k1_v0_4_0_ecdsa_signature_load(ctx, &sr[i], &ss, &sig2);
            for (j = 0; j < i; j++) {
                CHECK(!rustsecp256k1_v0_4_0_scalar_eq(&sr[i], &sr[j]));
            }
        }
        key[0] = 0;
    }

    {
        /* Check that optional nonce arguments do not have equivalent effect. */
        const unsigned char zeros[32] = {0};
        unsigned char nonce[32];
        unsigned char nonce2[32];
        unsigned char nonce3[32];
        unsigned char nonce4[32];
        VG_UNDEF(nonce,32);
        VG_UNDEF(nonce2,32);
        VG_UNDEF(nonce3,32);
        VG_UNDEF(nonce4,32);
        CHECK(nonce_function_rfc6979(nonce, zeros, zeros, NULL, NULL, 0) == 1);
        VG_CHECK(nonce,32);
        CHECK(nonce_function_rfc6979(nonce2, zeros, zeros, zeros, NULL, 0) == 1);
        VG_CHECK(nonce2,32);
        CHECK(nonce_function_rfc6979(nonce3, zeros, zeros, NULL, (void *)zeros, 0) == 1);
        VG_CHECK(nonce3,32);
        CHECK(nonce_function_rfc6979(nonce4, zeros, zeros, zeros, (void *)zeros, 0) == 1);
        VG_CHECK(nonce4,32);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(nonce, nonce2, 32) != 0);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(nonce, nonce3, 32) != 0);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(nonce, nonce4, 32) != 0);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(nonce2, nonce3, 32) != 0);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(nonce2, nonce4, 32) != 0);
        CHECK(rustsecp256k1_v0_4_0_memcmp_var(nonce3, nonce4, 32) != 0);
    }


    /* Privkey export where pubkey is the point at infinity. */
    {
        unsigned char privkey[300];
        unsigned char seckey[32] = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
            0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
            0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x41,
        };
        size_t outlen = 300;
        CHECK(!ec_privkey_export_der(ctx, privkey, &outlen, seckey, 0));
        outlen = 300;
        CHECK(!ec_privkey_export_der(ctx, privkey, &outlen, seckey, 1));
    }
}

void run_ecdsa_edge_cases(void) {
    test_ecdsa_edge_cases();
}

#ifdef ENABLE_OPENSSL_TESTS
EC_KEY *get_openssl_key(const unsigned char *key32) {
    unsigned char privkey[300];
    size_t privkeylen;
    const unsigned char* pbegin = privkey;
    int compr = rustsecp256k1_v0_4_0_testrand_bits(1);
    EC_KEY *ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    CHECK(ec_privkey_export_der(ctx, privkey, &privkeylen, key32, compr));
    CHECK(d2i_ECPrivateKey(&ec_key, &pbegin, privkeylen));
    CHECK(EC_KEY_check_key(ec_key));
    return ec_key;
}

void test_ecdsa_openssl(void) {
    rustsecp256k1_v0_4_0_gej qj;
    rustsecp256k1_v0_4_0_ge q;
    rustsecp256k1_v0_4_0_scalar sigr, sigs;
    rustsecp256k1_v0_4_0_scalar one;
    rustsecp256k1_v0_4_0_scalar msg2;
    rustsecp256k1_v0_4_0_scalar key, msg;
    EC_KEY *ec_key;
    unsigned int sigsize = 80;
    size_t secp_sigsize = 80;
    unsigned char message[32];
    unsigned char signature[80];
    unsigned char key32[32];
    rustsecp256k1_v0_4_0_testrand256_test(message);
    rustsecp256k1_v0_4_0_scalar_set_b32(&msg, message, NULL);
    random_scalar_order_test(&key);
    rustsecp256k1_v0_4_0_scalar_get_b32(key32, &key);
    rustsecp256k1_v0_4_0_ecmult_gen(&ctx->ecmult_gen_ctx, &qj, &key);
    rustsecp256k1_v0_4_0_ge_set_gej(&q, &qj);
    ec_key = get_openssl_key(key32);
    CHECK(ec_key != NULL);
    CHECK(ECDSA_sign(0, message, sizeof(message), signature, &sigsize, ec_key));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_parse(&sigr, &sigs, signature, sigsize));
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sigr, &sigs, &q, &msg));
    rustsecp256k1_v0_4_0_scalar_set_int(&one, 1);
    rustsecp256k1_v0_4_0_scalar_add(&msg2, &msg, &one);
    CHECK(!rustsecp256k1_v0_4_0_ecdsa_sig_verify(&ctx->ecmult_ctx, &sigr, &sigs, &q, &msg2));

    random_sign(&sigr, &sigs, &key, &msg, NULL);
    CHECK(rustsecp256k1_v0_4_0_ecdsa_sig_serialize(signature, &secp_sigsize, &sigr, &sigs));
    CHECK(ECDSA_verify(0, message, sizeof(message), signature, secp_sigsize, ec_key) == 1);

    EC_KEY_free(ec_key);
}

void run_ecdsa_openssl(void) {
    int i;
    for (i = 0; i < 10*count; i++) {
        test_ecdsa_openssl();
    }
}
#endif

#ifdef ENABLE_MODULE_ECDH
# include "modules/ecdh/tests_impl.h"
#endif

#ifdef ENABLE_MODULE_RECOVERY
# include "modules/recovery/tests_impl.h"
#endif

#ifdef ENABLE_MODULE_EXTRAKEYS
# include "modules/extrakeys/tests_impl.h"
#endif

#ifdef ENABLE_MODULE_SCHNORRSIG
# include "modules/schnorrsig/tests_impl.h"
#endif

void run_rustsecp256k1_v0_4_0_memczero_test(void) {
    unsigned char buf1[6] = {1, 2, 3, 4, 5, 6};
    unsigned char buf2[sizeof(buf1)];

    /* rustsecp256k1_v0_4_0_memczero(..., ..., 0) is a noop. */
    memcpy(buf2, buf1, sizeof(buf1));
    rustsecp256k1_v0_4_0_memczero(buf1, sizeof(buf1), 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(buf1, buf2, sizeof(buf1)) == 0);

    /* rustsecp256k1_v0_4_0_memczero(..., ..., 1) zeros the buffer. */
    memset(buf2, 0, sizeof(buf2));
    rustsecp256k1_v0_4_0_memczero(buf1, sizeof(buf1) , 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(buf1, buf2, sizeof(buf1)) == 0);
}

void int_cmov_test(void) {
    int r = INT_MAX;
    int a = 0;

    rustsecp256k1_v0_4_0_int_cmov(&r, &a, 0);
    CHECK(r == INT_MAX);

    r = 0; a = INT_MAX;
    rustsecp256k1_v0_4_0_int_cmov(&r, &a, 1);
    CHECK(r == INT_MAX);

    a = 0;
    rustsecp256k1_v0_4_0_int_cmov(&r, &a, 1);
    CHECK(r == 0);

    a = 1;
    rustsecp256k1_v0_4_0_int_cmov(&r, &a, 1);
    CHECK(r == 1);

    r = 1; a = 0;
    rustsecp256k1_v0_4_0_int_cmov(&r, &a, 0);
    CHECK(r == 1);

}

void fe_cmov_test(void) {
    static const rustsecp256k1_v0_4_0_fe zero = SECP256K1_FE_CONST(0, 0, 0, 0, 0, 0, 0, 0);
    static const rustsecp256k1_v0_4_0_fe one = SECP256K1_FE_CONST(0, 0, 0, 0, 0, 0, 0, 1);
    static const rustsecp256k1_v0_4_0_fe max = SECP256K1_FE_CONST(
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL
    );
    rustsecp256k1_v0_4_0_fe r = max;
    rustsecp256k1_v0_4_0_fe a = zero;

    rustsecp256k1_v0_4_0_fe_cmov(&r, &a, 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &max, sizeof(r)) == 0);

    r = zero; a = max;
    rustsecp256k1_v0_4_0_fe_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &max, sizeof(r)) == 0);

    a = zero;
    rustsecp256k1_v0_4_0_fe_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &zero, sizeof(r)) == 0);

    a = one;
    rustsecp256k1_v0_4_0_fe_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &one, sizeof(r)) == 0);

    r = one; a = zero;
    rustsecp256k1_v0_4_0_fe_cmov(&r, &a, 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &one, sizeof(r)) == 0);
}

void fe_storage_cmov_test(void) {
    static const rustsecp256k1_v0_4_0_fe_storage zero = SECP256K1_FE_STORAGE_CONST(0, 0, 0, 0, 0, 0, 0, 0);
    static const rustsecp256k1_v0_4_0_fe_storage one = SECP256K1_FE_STORAGE_CONST(0, 0, 0, 0, 0, 0, 0, 1);
    static const rustsecp256k1_v0_4_0_fe_storage max = SECP256K1_FE_STORAGE_CONST(
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL
    );
    rustsecp256k1_v0_4_0_fe_storage r = max;
    rustsecp256k1_v0_4_0_fe_storage a = zero;

    rustsecp256k1_v0_4_0_fe_storage_cmov(&r, &a, 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &max, sizeof(r)) == 0);

    r = zero; a = max;
    rustsecp256k1_v0_4_0_fe_storage_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &max, sizeof(r)) == 0);

    a = zero;
    rustsecp256k1_v0_4_0_fe_storage_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &zero, sizeof(r)) == 0);

    a = one;
    rustsecp256k1_v0_4_0_fe_storage_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &one, sizeof(r)) == 0);

    r = one; a = zero;
    rustsecp256k1_v0_4_0_fe_storage_cmov(&r, &a, 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &one, sizeof(r)) == 0);
}

void scalar_cmov_test(void) {
    static const rustsecp256k1_v0_4_0_scalar zero = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 0);
    static const rustsecp256k1_v0_4_0_scalar one = SECP256K1_SCALAR_CONST(0, 0, 0, 0, 0, 0, 0, 1);
    static const rustsecp256k1_v0_4_0_scalar max = SECP256K1_SCALAR_CONST(
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL
    );
    rustsecp256k1_v0_4_0_scalar r = max;
    rustsecp256k1_v0_4_0_scalar a = zero;

    rustsecp256k1_v0_4_0_scalar_cmov(&r, &a, 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &max, sizeof(r)) == 0);

    r = zero; a = max;
    rustsecp256k1_v0_4_0_scalar_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &max, sizeof(r)) == 0);

    a = zero;
    rustsecp256k1_v0_4_0_scalar_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &zero, sizeof(r)) == 0);

    a = one;
    rustsecp256k1_v0_4_0_scalar_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &one, sizeof(r)) == 0);

    r = one; a = zero;
    rustsecp256k1_v0_4_0_scalar_cmov(&r, &a, 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &one, sizeof(r)) == 0);
}

void ge_storage_cmov_test(void) {
    static const rustsecp256k1_v0_4_0_ge_storage zero = SECP256K1_GE_STORAGE_CONST(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    static const rustsecp256k1_v0_4_0_ge_storage one = SECP256K1_GE_STORAGE_CONST(0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1);
    static const rustsecp256k1_v0_4_0_ge_storage max = SECP256K1_GE_STORAGE_CONST(
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL,
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL
    );
    rustsecp256k1_v0_4_0_ge_storage r = max;
    rustsecp256k1_v0_4_0_ge_storage a = zero;

    rustsecp256k1_v0_4_0_ge_storage_cmov(&r, &a, 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &max, sizeof(r)) == 0);

    r = zero; a = max;
    rustsecp256k1_v0_4_0_ge_storage_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &max, sizeof(r)) == 0);

    a = zero;
    rustsecp256k1_v0_4_0_ge_storage_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &zero, sizeof(r)) == 0);

    a = one;
    rustsecp256k1_v0_4_0_ge_storage_cmov(&r, &a, 1);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &one, sizeof(r)) == 0);

    r = one; a = zero;
    rustsecp256k1_v0_4_0_ge_storage_cmov(&r, &a, 0);
    CHECK(rustsecp256k1_v0_4_0_memcmp_var(&r, &one, sizeof(r)) == 0);
}

void run_cmov_tests(void) {
    int_cmov_test();
    fe_cmov_test();
    fe_storage_cmov_test();
    scalar_cmov_test();
    ge_storage_cmov_test();
}

int main(int argc, char **argv) {
    /* Disable buffering for stdout to improve reliability of getting
     * diagnostic information. Happens right at the start of main because
     * setbuf must be used before any other operation on the stream. */
    setbuf(stdout, NULL);
    /* Also disable buffering for stderr because it's not guaranteed that it's
     * unbuffered on all systems. */
    setbuf(stderr, NULL);

    /* find iteration count */
    if (argc > 1) {
        count = strtol(argv[1], NULL, 0);
    } else {
        const char* env = getenv("SECP256K1_TEST_ITERS");
        if (env) {
            count = strtol(env, NULL, 0);
        }
    }
    if (count <= 0) {
        fputs("An iteration count of 0 or less is not allowed.\n", stderr);
        return EXIT_FAILURE;
    }
    printf("test count = %i\n", count);

    /* find random seed */
    rustsecp256k1_v0_4_0_testrand_init(argc > 2 ? argv[2] : NULL);

    /* initialize */
    run_context_tests(0);
    run_context_tests(1);
    run_scratch_tests();
    ctx = rustsecp256k1_v0_4_0_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    if (rustsecp256k1_v0_4_0_testrand_bits(1)) {
        unsigned char rand32[32];
        rustsecp256k1_v0_4_0_testrand256(rand32);
        CHECK(rustsecp256k1_v0_4_0_context_randomize(ctx, rustsecp256k1_v0_4_0_testrand_bits(1) ? rand32 : NULL));
    }

    run_rand_bits();
    run_rand_int();

    run_sha256_tests();
    run_hmac_sha256_tests();
    run_rfc6979_hmac_sha256_tests();

#ifndef USE_NUM_NONE
    /* num tests */
    run_num_smalltests();
#endif

    /* scalar tests */
    run_scalar_tests();

    /* field tests */
    run_field_inv();
    run_field_inv_var();
    run_field_inv_all_var();
    run_field_misc();
    run_field_convert();
    run_sqr();
    run_sqrt();

    /* group tests */
    run_ge();
    run_group_decompress();

    /* ecmult tests */
    run_wnaf();
    run_point_times_order();
    run_ecmult_near_split_bound();
    run_ecmult_chain();
    run_ecmult_constants();
    run_ecmult_gen_blind();
    run_ecmult_const_tests();
    run_ecmult_multi_tests();
    run_ec_combine();

    /* endomorphism tests */
    run_endomorphism_tests();

    /* EC point parser test */
    run_ec_pubkey_parse_test();

    /* EC key edge cases */
    run_eckey_edge_case_test();

    /* EC key arithmetic test */
    run_eckey_negate_test();

#ifdef ENABLE_MODULE_ECDH
    /* ecdh tests */
    run_ecdh_tests();
#endif

    /* ecdsa tests */
    run_random_pubkeys();
    run_ecdsa_der_parse();
    run_ecdsa_sign_verify();
    run_ecdsa_end_to_end();
    run_ecdsa_edge_cases();
#ifdef ENABLE_OPENSSL_TESTS
    run_ecdsa_openssl();
#endif

#ifdef ENABLE_MODULE_RECOVERY
    /* ECDSA pubkey recovery tests */
    run_recovery_tests();
#endif

#ifdef ENABLE_MODULE_EXTRAKEYS
    run_extrakeys_tests();
#endif

#ifdef ENABLE_MODULE_SCHNORRSIG
    run_schnorrsig_tests();
#endif

    /* util tests */
    run_rustsecp256k1_v0_4_0_memczero_test();

    run_cmov_tests();

    rustsecp256k1_v0_4_0_testrand_finish();

    /* shutdown */
    rustsecp256k1_v0_4_0_context_destroy(ctx);

    printf("no problems found\n");
    return 0;
}