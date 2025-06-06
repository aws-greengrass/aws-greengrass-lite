// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "cli-runner.h"
#include "ggl/io.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/log.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct RunnerEntry {
    const char *arg_list[5];
    GglBuffer expected_output;
    bool successful;
} RunnerEntry;

GglError run_cli_runner(void) {
    const RunnerEntry ENTRIES[]
        = { { .arg_list = { "ls", "-z", NULL },
              .successful = false,
              .expected_output = GGL_STR("ls: invalid option -- 'z'\nTry 'ls "
                                         "--help' for more information.\n") },
            { .arg_list = { "echo", "hello", NULL },
              .successful = true,
              .expected_output = GGL_STR("hello\n") },
            { .arg_list = { "ls-l", NULL },
              .successful = false,
              .expected_output = GGL_STR("") },
            { .arg_list = { "ls", "-l", NULL },
              .successful = true,
              .expected_output = { 0 } } };
    uint8_t output_buf[256];

    for (size_t i = 0; i < sizeof(ENTRIES) / sizeof(*ENTRIES); ++i) {
        const RunnerEntry *entry = &ENTRIES[i];
        GglError err = ggl_exec_command(entry->arg_list);
        bool successful = err == GGL_ERR_OK;
        GGL_LOGI("Success: %s", successful ? "true" : "false");
        assert(entry->successful == successful);
    }

    for (size_t i = 0; i < sizeof(ENTRIES) / sizeof(*ENTRIES); ++i) {
        const RunnerEntry *entry = &ENTRIES[i];
        GglBuffer output = GGL_BUF(output_buf);

        GglError err = ggl_exec_command_with_output(
            entry->arg_list, ggl_buf_writer(&output)
        );
        bool successful = (err == GGL_ERR_OK) || (err == GGL_ERR_NOMEM);
        output.len = (size_t) (output.data - output_buf);
        output.data = output_buf;
        GGL_LOGI(
            "Success: %s\n%.*s",
            successful ? "true" : "false",
            (int) output.len,
            output.data
        );
        assert(entry->successful == successful);
        assert(output.len <= sizeof(output_buf));
        if (entry->expected_output.data != NULL) {
            assert(ggl_buffer_eq(entry->expected_output, output));
        }
    }

    return GGL_ERR_OK;
}
