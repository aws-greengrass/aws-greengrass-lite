#include <fcntl.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/digest.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/types.h>
#include <stddef.h>
#include <stdint.h>

GglDigest ggl_new_digest(GgError *error) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        GG_LOGE("OpenSSL new message digest context failed.");
        *error = GG_ERR_NOMEM;
    } else {
        EVP_MD_CTX_set_flags(ctx, EVP_MD_CTX_FLAG_REUSE);
        *error = GG_ERR_OK;
    }
    return (GglDigest) { .ctx = ctx };
}

GgError ggl_verify_sha256_digest(
    int dirfd, GgBuffer path, GgBuffer expected_digest, GglDigest digest_context
) {
    int file_fd;
    GgError ret = gg_file_openat(dirfd, path, O_RDONLY, 0, &file_fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_close, file_fd);
    if (digest_context.ctx == NULL) {
        return GG_ERR_INVALID;
    }
    EVP_MD_CTX *ctx = digest_context.ctx;
    if (!EVP_DigestInit(ctx, EVP_sha256())) {
        GG_LOGE("OpenSSL message digest init failed.");
        return GG_ERR_FAILURE;
    }

    uint8_t digest_buffer[SHA256_DIGEST_LENGTH];
    for (;;) {
        GgBuffer chunk = GG_BUF(digest_buffer);
        ret = gg_file_read(file_fd, &chunk);
        if (chunk.len == 0) {
            break;
        }
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to read from file.");
            break;
        }
        if (!EVP_DigestUpdate(ctx, chunk.data, chunk.len)) {
            GG_LOGE("OpenSSL digest update failed.");
            return GG_ERR_FAILURE;
        }
    }

    unsigned int size = sizeof(digest_buffer);
    if (!EVP_DigestFinal(ctx, digest_buffer, &size)) {
        GG_LOGE("OpenSSL digest finalize failed.");
        return GG_ERR_FAILURE;
    }

    if (!gg_buffer_eq(
            (GgBuffer) { .data = digest_buffer, .len = size }, expected_digest
        )) {
        GG_LOGE("Failed to verify digest.");
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

void ggl_free_digest(GglDigest *digest_context) {
    if (digest_context->ctx != NULL) {
        EVP_MD_CTX_free(digest_context->ctx);
        digest_context->ctx = NULL;
    }
}
