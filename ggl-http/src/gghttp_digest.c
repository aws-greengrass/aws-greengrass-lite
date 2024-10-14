#include "ggl/digest.h"
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/types.h>
#include <stddef.h>

GglDigest ggl_new_digest(GglError *error) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        GGL_LOGE("OpenSSL new message digest context failed.");
        *error = GGL_ERR_NOMEM;
    } else {
        EVP_MD_CTX_set_flags(ctx, EVP_MD_CTX_FLAG_REUSE);
        *error = GGL_ERR_OK;
    }
    return (GglDigest) { .ctx = ctx };
}

GglError ggl_verify_sha256_digest(
    int dirfd,
    GglBuffer path,
    GglBuffer expected_digest,
    GglDigest digest_context
) {
    int file_fd;
    GglError ret = ggl_file_openat(dirfd, path, O_RDONLY, 0, &file_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(ggl_close, file_fd);
    if (digest_context.ctx == NULL) {
        return GGL_ERR_INVALID;
    }
    EVP_MD_CTX *ctx = digest_context.ctx;
    if (!EVP_DigestInit(ctx, EVP_sha256())) {
        GGL_LOGE("OpenSSL message digest init failed.");
        return GGL_ERR_FAILURE;
    }

    unsigned char digest_buffer[SHA256_DIGEST_LENGTH];
    for (;;) {
        GglBuffer chunk = GGL_BUF(digest_buffer);
        ret = ggl_read(file_fd, &chunk);
        if (ret == GGL_ERR_NOCONN) {
            break;
        }
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read from file.");
            break;
        }
        size_t bytes_read = sizeof(digest_buffer) - chunk.len;
        if (!EVP_DigestUpdate(ctx, digest_buffer, bytes_read)) {
            GGL_LOGE("OpenSSL digest update failed.");
            return GGL_ERR_FAILURE;
        }
    }

    unsigned int size = sizeof(digest_buffer);
    if (!EVP_DigestFinal(ctx, digest_buffer, &size)) {
        GGL_LOGE("OpenSSL digest finalize failed.");
        return GGL_ERR_FAILURE;
    }

    if (!ggl_buffer_eq(
            (GglBuffer) { .data = digest_buffer, .len = size }, expected_digest
        )) {
        GGL_LOGE("Failed to verify digest.");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

void ggl_free_digest(GglDigest *digest_context) {
    if (digest_context->ctx != NULL) {
        EVP_MD_CTX_free(digest_context->ctx);
        digest_context->ctx = NULL;
    }
}
