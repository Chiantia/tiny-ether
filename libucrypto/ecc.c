#include "ecc.h"

// clang-format off
#define IF_ERR_EXIT(f)                    \
    do {                                  \
        if ((err = (f)) != 0) goto EXIT;  \
    } while (0)
#define IF_NEG_EXIT(val, f)               \
    do {                                  \
        if ((val = (f)) < 0) goto EXIT;   \
    } while (0)
// clang-format off

int
ucrypto_ecc_key_init(ucrypto_ecc_ctx* ctx, const ucrypto_mpi* d)
{
    return d ? ucrypto_ecc_key_init_binary(ctx, d)
             : ucrypto_ecc_key_init_new(ctx);
}

int
ucrypto_ecc_key_init_string(ucrypto_ecc_ctx* ctx, int radix, const char* s)
{
    int err = -1;
    ucrypto_mpi d;
    ucrypto_mpi_init(&d);
    err = ucrypto_mpi_read_string(&d, radix, s);
    if (!(err == 0)) goto EXIT;
    err = ucrypto_ecc_key_init_binary(ctx, &d);
    if (!(err == 0)) goto EXIT;
    err = 0;
EXIT:
    ucrypto_mpi_free(&d);
    return err;
}

int
ucrypto_ecc_key_init_binary(ucrypto_ecc_ctx* ctx, const ucrypto_mpi* d)
{
    int ret;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;

    // initialize stack variables and callers ecdh context.
    mbedtls_ecdh_init(ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);

    // Seed rng
    ret = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, NULL, 0);
    if (!(ret == 0)) goto EXIT;

    // Load curve parameters
    ret = mbedtls_ecp_group_load(&ctx->grp, MBEDTLS_ECP_DP_SECP256K1);
    if (!(ret == 0)) goto EXIT;

    // Copy our key
    ret = mbedtls_mpi_copy(&ctx->d, d);
    if (!(ret == 0)) goto EXIT;

    // Get public key from private key?
    ret = mbedtls_ecp_mul(&ctx->grp, &ctx->Q, &ctx->d, &ctx->grp.G,
                          mbedtls_entropy_func, &entropy);
    if (!(ret == 0)) goto EXIT;

EXIT:
    if (ret) mbedtls_ecdh_free(ctx);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    return ret;
}

int
ucrypto_ecc_key_init_new(ucrypto_ecc_ctx* ctx)
{
    int ret;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;

    // initialize stack variables and callers ecc context.
    mbedtls_ecdh_init(ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);

    // Seed rng
    ret = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, NULL, 0);
    if (!(ret == 0)) goto EXIT;

    // Load curve parameters
    ret = mbedtls_ecp_group_load(&ctx->grp, MBEDTLS_ECP_DP_SECP256K1);
    if (!(ret == 0)) goto EXIT;

    // Create ecc public/private key pair
    ret = mbedtls_ecdh_gen_public(&ctx->grp, &ctx->d, &ctx->Q,
                                  mbedtls_ctr_drbg_random, &rng);
    if (!(ret == 0)) goto EXIT;

EXIT:
    if (ret) mbedtls_ecdh_free(ctx);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    return ret;
}

void
ucrypto_ecc_key_deinit(ucrypto_ecc_ctx* ctx)
{
    mbedtls_ecdh_free(ctx);
}

int
ucrypto_ecc_atop(const char* str, int rdx, ucrypto_ecp_point* q)
{
    int err = -1;
    uint8_t buff[65];
    size_t l;
    mbedtls_ecp_group grp;
    mbedtls_mpi bin;

    // init stack
    mbedtls_mpi_init(&bin);
    mbedtls_ecp_group_init(&grp);
    err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1);
    if (!(err == 0)) goto EXIT;

    // Read in string
    mbedtls_mpi_read_string(&bin, rdx, str);
    l = mbedtls_mpi_size(&bin);
    if (!(l == 65)) goto EXIT;
    mbedtls_mpi_write_binary(&bin, buff, l);
    err = mbedtls_ecp_point_read_binary(&grp, q, buff, l);
    if (!(err == 0)) goto EXIT;

    err = 0;

EXIT:
    // Free
    mbedtls_mpi_free(&bin);
    mbedtls_ecp_group_free(&grp);
    return err;
}

int
ucrypto_ecc_ptob(ucrypto_ecp_point* p, ucrypto_ecc_public_key* b)
{
    int err;
    size_t len = 65;
    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);
    err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1);
    if (!err) {
        err = mbedtls_ecp_point_write_binary(
            &grp, p, MBEDTLS_ECP_PF_UNCOMPRESSED, &len, b->b, 65);
    }
    mbedtls_ecp_group_free(&grp);
    return err ? -1 : 0;
}

int
ucrypto_ecc_btop(ucrypto_ecc_public_key* k, ucrypto_ecp_point* p)
{
    int err = -1;
    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);
    err = mbedtls_ecp_point_read_binary(&grp, p, k->b, 65);
    mbedtls_ecp_group_free(&grp);
    return err;
}

int
ucrypto_ecc_point_copy(const ucrypto_ecp_point* src, ucrypto_ecp_point* dst)
{
    return mbedtls_ecp_copy(dst, src) == 0 ? 0 : -1;
}

int
ucrypto_ecc_agree(ucrypto_ecc_ctx* ctx, const ucrypto_ecc_public_key* key)
{
    int err;
    ucrypto_ecp_point point;
    mbedtls_ecp_point_init(&point);

    /**
     * @brief note mbedtls_ecp_point_read_binary only accepts types of points
     * where key[0] is 0x04...
     */
    mbedtls_ecp_point_read_binary(&ctx->grp, &point, (uint8_t*)key,
                                  sizeof(ucrypto_ecc_public_key));

    err = ucrypto_ecc_agree_point(ctx, &point);
    mbedtls_ecp_point_free(&point);
    return err;
}

int
ucrypto_ecc_agree_point(ucrypto_ecc_ctx* ctx, const ucrypto_ecp_point* qp)
{
    int err;
    mbedtls_ctr_drbg_context rng;
    mbedtls_entropy_context entropy;

    // initialize stack content
    mbedtls_ctr_drbg_init(&rng);
    mbedtls_entropy_init(&entropy);

    // seed RNG
    err = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, NULL, 0);
    if (!(err == 0)) goto EXIT;

    // Create shared secret with other guys Q
    err = mbedtls_ecdh_compute_shared(&ctx->grp, &ctx->z, qp, &ctx->d,
                                      mbedtls_ctr_drbg_random, &rng);
    if (!(err == 0)) goto EXIT;
EXIT:
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    return err == 0 ? err : -1;
}

int
ucrypto_ecc_sign(ucrypto_ecc_ctx* ctx,
                 const uint8_t* b,
                 uint32_t sz,
                 ucrypto_ecc_signature* sig_p)
{
    int err, ret = -1;
    uint8_t* sig = sig_p->b;
    ucrypto_mpi r, s;
    mbedtls_ctr_drbg_context rng;
    mbedtls_entropy_context entropy;
    for (int i = 0; i < 65; i++) sig[0] = 0;

    // Init stack content
    mbedtls_ctr_drbg_init(&rng);
    mbedtls_entropy_init(&entropy);
    ucrypto_mpi_init(&r);
    ucrypto_mpi_init(&s);

    // Seed RNG
    err = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, NULL, 0);
    if (!(err == 0)) goto EXIT;

    // Sign message
    err = mbedtls_ecdsa_sign(&ctx->grp, &r, &s, &ctx->d, b, sz,
                             mbedtls_ctr_drbg_random, &rng);
    if (!(err == 0)) goto EXIT;
    ret = 0;

    // Print Keys
    err = mbedtls_mpi_write_binary(&r, &sig[0], 32);
    if (!(err == 0)) goto EXIT;

    err = mbedtls_mpi_write_binary(&s, &sig[32], 32);
    if (!(err == 0)) goto EXIT;

    // TODO: ``\_("/)_/``
    // No idea if this is right. (Lowest bit of public keys Y coordinate)
    sig[64] = mbedtls_mpi_get_bit(&ctx->Q.Y, 0) ? 1 : 0;

EXIT:
    ucrypto_mpi_free(&r);
    ucrypto_mpi_free(&s);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    return ret;
}

int
ucrypto_ecc_verify(const ucrypto_ecp_point* q,
                   const uint8_t* b,
                   uint32_t sz,
                   ucrypto_ecc_signature* sig_p)
{
    int err, ret = -1;
    uint8_t* sig = sig_p->b;
    mbedtls_ecp_group grp;
    ucrypto_mpi r, s;

    // Init stack content
    mbedtls_ecp_group_init(&grp);
    ucrypto_mpi_init(&r);
    ucrypto_mpi_init(&s);
    err = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1);
    if (!(err == 0)) goto EXIT;

    err = mbedtls_mpi_read_binary(&r, sig, 32);
    if (!(err == 0)) goto EXIT;

    err = mbedtls_mpi_read_binary(&s, &sig[32], 32);
    if (!(err == 0)) goto EXIT;

    // Verify signature of content
    err = mbedtls_ecdsa_verify(&grp, b, sz, q, &r, &s);
    if (!(err == 0)) goto EXIT;

    ret = 0;
EXIT:
    mbedtls_ecp_group_free(&grp);
    ucrypto_mpi_free(&r);
    ucrypto_mpi_free(&s);
    return ret;
}
int
ucrypto_ecc_recover(const ucrypto_ecc_signature* sig,
                    const uint8_t* digest,
                    int recid,
                    ucrypto_ecc_public_key* key)
{
    int err = 0;
    ucrypto_mpi r, s, e;
    ucrypto_ecp_point cp, cp2;
    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_init(&e);
    mbedtls_ecp_point_init(&cp);
    mbedtls_ecp_point_init(&cp2);

    // external/trezor-crypto/ecdsa.c
    IF_ERR_EXIT(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1));
    IF_ERR_EXIT(mbedtls_mpi_read_binary(&r, sig->b, 32));
    IF_ERR_EXIT(mbedtls_mpi_read_binary(&s, &sig->b[32], 32));
    IF_NEG_EXIT(err, mbedtls_mpi_cmp_mpi(&r, &grp.N));
    IF_NEG_EXIT(err, mbedtls_mpi_cmp_mpi(&s, &grp.N));

    // cp = R = k * G (k is secret nonce when signing)
    mbedtls_mpi_copy(&cp.X, &r);
    if (recid & 2) {
        mbedtls_mpi_add_mpi(&cp.X, &cp.X, &grp.N);
        IF_NEG_EXIT(err, mbedtls_mpi_cmp_mpi(&cp.X, &grp.N));
    }

// TODO -

// compute y from x
// uncompress_coords(curve, recid & 1, &cp.x, &cp.y);
// y is x
// y is x^2
// y is x^2+a
// y is x^3+ax
// Y is x^3+ax+b
// y = sqrt(y)
// e = -digest
// r := r^-1
// cp := s * R = s * k *G
// cp2 := -digest * G
// cp := (s * k - digest) * G = (r*priv) * G = r * Pub
// cp := r^{-1} * r * Pub = Pub

EXIT:
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&e);
    mbedtls_ecp_point_free(&cp);
    mbedtls_ecp_point_free(&cp2);
    return err;
}

//
//
//
