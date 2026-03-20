// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <recipe-runner-test.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

// Replicate the exec insertion logic from process_lifecycle_phase in
// runner.c. This writes a script to a memfd the same way recipe-runner
// does, then reads it back and checks the output.
static GgError write_script(int fd, GgBuffer phase, GgBuffer script_content) {
    GgError ret = gg_file_write(fd, GG_STR("#!/bin/sh\n"));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    size_t last_newline = SIZE_MAX;
    if (gg_buffer_eq(phase, GG_STR("run"))
        || gg_buffer_eq(phase, GG_STR("startup"))) {
        last_newline = 0;
        for (size_t idx = 0; idx < script_content.len; idx++) {
            if (script_content.data[idx] == '\n') {
                last_newline = idx + 1;
            }
        }
    }

    for (size_t pos = 0; pos < script_content.len; pos++) {
        if (pos == last_newline) {
            ret = gg_file_write(fd, GG_STR("exec "));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            last_newline = SIZE_MAX;
        }
        ret = gg_file_write(fd, (GgBuffer) { &script_content.data[pos], 1 });
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}

static GgError read_memfd(int fd, uint8_t *buf, size_t buf_len, size_t *out) {
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return GG_ERR_FAILURE;
    }
    ssize_t n = read(fd, buf, buf_len - 1);
    if (n < 0) {
        return GG_ERR_FAILURE;
    }
    buf[n] = '\0';
    *out = (size_t) n;
    return GG_ERR_OK;
}

static GgError check(
    const char *name,
    GgBuffer phase,
    GgBuffer script_content,
    const char *expected
) {
    int fd = memfd_create("test", 0);
    if (fd < 0) {
        GG_LOGE("memfd_create failed for %s", name);
        return GG_ERR_FAILURE;
    }

    GgError ret = write_script(fd, phase, script_content);
    if (ret != GG_ERR_OK) {
        GG_LOGE("%s: write_script failed", name);
        close(fd);
        return ret;
    }

    uint8_t buf[4096];
    size_t len;
    ret = read_memfd(fd, buf, sizeof(buf), &len);
    close(fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("%s: read_memfd failed", name);
        return ret;
    }

    if (strcmp((char *) buf, expected) != 0) {
        GG_LOGE("%s: FAIL", name);
        GG_LOGE("  expected: %s", expected);
        GG_LOGE("  got:      %s", buf);
        return GG_ERR_FAILURE;
    }

    GG_LOGI("%s: PASS", name);
    return GG_ERR_OK;
}

GgError run_recipe_runner_test(void) {
    GgError ret;
    GgError result = GG_ERR_OK;

    // Single command, run phase: exec inserted
    ret = check(
        "single_cmd_run",
        GG_STR("run"),
        GG_STR("/path/to/component"),
        "#!/bin/sh\nexec /path/to/component"
    );
    if (ret != GG_ERR_OK) {
        result = ret;
    }

    // Single command, startup phase: exec inserted
    ret = check(
        "single_cmd_startup",
        GG_STR("startup"),
        GG_STR("/path/to/component"),
        "#!/bin/sh\nexec /path/to/component"
    );
    if (ret != GG_ERR_OK) {
        result = ret;
    }

    // Multi command, run phase: exec on last line only
    ret = check(
        "multi_cmd_run",
        GG_STR("run"),
        GG_STR("echo setup\n/path/to/component"),
        "#!/bin/sh\necho setup\nexec /path/to/component"
    );
    if (ret != GG_ERR_OK) {
        result = ret;
    }

    // Multi command with three lines: exec on last only
    ret = check(
        "three_cmd_run",
        GG_STR("run"),
        GG_STR("echo one\necho two\n/path/to/component"),
        "#!/bin/sh\necho one\necho two\nexec /path/to/component"
    );
    if (ret != GG_ERR_OK) {
        result = ret;
    }

    // Install phase: no exec inserted
    ret = check(
        "install_no_exec",
        GG_STR("install"),
        GG_STR("/path/to/setup.sh"),
        "#!/bin/sh\n/path/to/setup.sh"
    );
    if (ret != GG_ERR_OK) {
        result = ret;
    }

    // Shutdown phase: no exec inserted
    ret = check(
        "shutdown_no_exec",
        GG_STR("shutdown"),
        GG_STR("/path/to/cleanup.sh"),
        "#!/bin/sh\n/path/to/cleanup.sh"
    );
    if (ret != GG_ERR_OK) {
        result = ret;
    }

    // Multi command install: no exec inserted
    ret = check(
        "multi_cmd_install_no_exec",
        GG_STR("install"),
        GG_STR("echo setup\n/path/to/install.sh"),
        "#!/bin/sh\necho setup\n/path/to/install.sh"
    );
    if (ret != GG_ERR_OK) {
        result = ret;
    }

    return result;
}
