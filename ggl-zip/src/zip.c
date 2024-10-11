// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/zip.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <inttypes.h>
#include <zip.h>
#include <zipconf.h>
#include <stddef.h>

GGL_DEFINE_DEFER(
    zip_fclose,
    zip_file_t *,
    zip_entry,
    if (*zip_entry != NULL) zip_fclose(*zip_entry)
)

GGL_DEFINE_DEFER(
    zip_close,
    zip_t *,
    zip_archive,
    if (*zip_archive != NULL) zip_close(*zip_archive)
)

static GglError write_entry_to_fd(zip_file_t *entry, int fd) {
    uint8_t read_buffer[32];
    for (;;) {
        zip_int64_t bytes_read
            = zip_fread(entry, read_buffer, sizeof(read_buffer));
        // end of file
        if (bytes_read == 0) {
            return GGL_ERR_OK;
        }
        if (bytes_read < 0) {
            int err = zip_error_code_zip(zip_file_get_error(entry));
            GGL_LOGE("Failed to read from zip file with error %d.", err);
            return GGL_ERR_FAILURE;
        }
        GglBuffer bytes
            = (GglBuffer) { .data = read_buffer, .len = (size_t) bytes_read };
        GglError ret = ggl_write_exact(fd, bytes);
        if (ret != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }
    }
}

GglError ggl_zip_unarchive(
    int source_dest_dir_fd, GglBuffer zip_path, int dest_dir_fd, mode_t mode
) {
    zip_t *zip;
    {
        int zip_fd;
        GglError ret = ggl_file_openat(
            source_dest_dir_fd, zip_path, O_RDONLY, 0, &zip_fd
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        int err;
        zip = zip_fdopen(zip_fd, ZIP_RDONLY, &err);
        if (zip == NULL) {
            GGL_LOGE("Failed to open zip file with error %d.", err);
            return GGL_ERR_FAILURE;
        }
    }
    GGL_DEFER(zip_close, zip);

    GglBuffer init_name;
    zip_uint64_t num_entries = (zip_uint64_t) zip_get_num_entries(zip, 0);
    for (zip_uint64_t i = 0; i < num_entries; i++) {
        const char *name = zip_get_name(zip, i, 0);
        if (name == NULL) {
            int err = zip_error_code_zip(zip_get_error(zip));
            GGL_LOGE(
                "Failed to get the name of entry %" PRIu64 " with error %d.",
                (uint64_t) i,
                err
            );
            return GGL_ERR_FAILURE;
        }

        // Avoid creating duplicate zip file name
        if (i == 0) {
            init_name = ggl_buffer_from_null_term((char *) name);
            continue;
        }

        // Remove the first segment from the path
        GglBuffer name_buf = ggl_buffer_from_null_term((char *) name);
        GglBuffer trunc_name = name_buf;
        if (ggl_buffer_has_prefix(name_buf, init_name)) {
            trunc_name
                = ggl_buffer_substr(name_buf, init_name.len, name_buf.len);
        };

        zip_file_t *entry = zip_fopen_index(zip, i, 0);
        if (entry == NULL) {
            int err = zip_error_code_zip(zip_get_error(zip));
            GGL_LOGE(
                "Failed to open file \"%s\" (index %" PRIu64
                ") from zip with error %d.",
                trunc_name.data,
                i,
                err
            );
            return GGL_ERR_FAILURE;
        }
        GGL_DEFER(zip_fclose, entry);

        GglError ret;
        int dest_file_fd;
        if (ggl_buffer_has_suffix(trunc_name, GGL_STR("/"))) {
            ret = ggl_dir_openat(
                dest_dir_fd, trunc_name, O_PATH, mode, &dest_file_fd
            );
        } else {
            ret = ggl_file_openat(
                dest_dir_fd,
                trunc_name,
                O_WRONLY | O_CREAT | O_TRUNC,
                mode,
                &dest_file_fd
            );
        }
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        GGL_DEFER(ggl_close, dest_file_fd);

        ret = write_entry_to_fd(entry, dest_file_fd);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}
