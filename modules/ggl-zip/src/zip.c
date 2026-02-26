// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <fcntl.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/zip.h>
#include <inttypes.h>
#include <sys/types.h>
#include <zip.h>
#include <zipconf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline void cleanup_zip_fclose(zip_file_t **zip_entry) {
    if (*zip_entry != NULL) {
        zip_fclose(*zip_entry);
    }
}

static inline void cleanup_zip_close(zip_t **zip_archive) {
    if (*zip_archive != NULL) {
        zip_close(*zip_archive);
    }
}

static GgError write_entry_to_fd(zip_file_t *entry, int fd) {
    uint8_t read_buffer[32];
    for (;;) {
        zip_int64_t bytes_read
            = zip_fread(entry, read_buffer, sizeof(read_buffer));
        // end of file
        if (bytes_read == 0) {
            return GG_ERR_OK;
        }
        if (bytes_read < 0) {
            int err = zip_error_code_zip(zip_file_get_error(entry));
            GG_LOGE("Failed to read from zip file with error %d.", err);
            return GG_ERR_FAILURE;
        }
        GgBuffer bytes
            = (GgBuffer) { .data = read_buffer, .len = (size_t) bytes_read };
        GgError ret = gg_file_write(fd, bytes);
        if (ret != GG_ERR_OK) {
            return GG_ERR_FAILURE;
        }
    }
}

static bool validate_path(GgBuffer path) {
    if (path.len == 0) {
        GG_LOGW("Skipping empty path");
        return false;
    }

    if (path.data[0] == '/') {
        GG_LOGW(
            "Skipping absolute path in \"%.*s\"", (int) path.len, path.data
        );
        return false;
    }

    for (size_t i = 0; i + 2 < path.len; ++i) {
        if (gg_buffer_has_prefix(
                gg_buffer_substr(path, i, SIZE_MAX), GG_STR("../")
            )) {
            GG_LOGW(
                "Skipping path with \"../\" component(s) in \"%.*s\"",
                (int) path.len,
                path.data
            );
            return false;
        }
    }

    return true;
}

GgError ggl_zip_unarchive(
    int source_dest_dir_fd, GgBuffer zip_path, int dest_dir_fd, mode_t mode
) {
    zip_t *zip;
    {
        int zip_fd;
        GgError ret = gg_file_openat(
            source_dest_dir_fd, zip_path, O_RDONLY, 0, &zip_fd
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
        int err = -1;
        zip = zip_fdopen(zip_fd, ZIP_RDONLY, &err);
        if (zip == NULL) {
            GG_LOGE("Failed to open zip file with error %d.", err);
            return GG_ERR_FAILURE;
        }
    }
    GG_CLEANUP(cleanup_zip_close, zip);

    zip_uint64_t num_entries = (zip_uint64_t) zip_get_num_entries(zip, 0);
    for (zip_uint64_t i = 0; i < num_entries; i++) {
        const char *name = zip_get_name(zip, i, 0);
        if (name == NULL) {
            int err = zip_error_code_zip(zip_get_error(zip));
            GG_LOGE(
                "Failed to get the name of entry %" PRIu64 " with error %d.",
                (uint64_t) i,
                err
            );
            return GG_ERR_FAILURE;
        }

        GgBuffer name_buf = gg_buffer_from_null_term((char *) name);
        if (!validate_path(name_buf)) {
            continue;
        }

        zip_file_t *entry = zip_fopen_index(zip, i, 0);
        if (entry == NULL) {
            int err = zip_error_code_zip(zip_get_error(zip));
            GG_LOGE(
                "Failed to open file \"%s\" (index %" PRIu64
                ") from zip with error %d.",
                name_buf.data,
                i,
                err
            );
            return GG_ERR_FAILURE;
        }
        GG_CLEANUP(cleanup_zip_fclose, entry);

        GgError ret;
        int dest_file_fd;
        if (gg_buffer_has_suffix(name_buf, GG_STR("/"))) {
            ret = gg_dir_openat(
                dest_dir_fd, name_buf, O_PATH, mode, &dest_file_fd
            );
        } else {
            ret = gg_file_openat(
                dest_dir_fd,
                name_buf,
                O_WRONLY | O_CREAT | O_TRUNC,
                mode,
                &dest_file_fd
            );
        }
        if (ret != GG_ERR_OK) {
            return ret;
        }
        GG_CLEANUP(cleanup_close, dest_file_fd);

        ret = write_entry_to_fd(entry, dest_file_fd);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}
