// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include <assert.h>
#include <cli-runner.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/io.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/exec.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct RunnerEntry {
    const char *arg_list[5];
    GgBuffer expected_output;
    bool successful;
} RunnerEntry;

typedef struct InputEntry {
    const char *arg_list[5];
    GgObject input;
    bool successful;
} InputEntry;

GgError run_cli_runner(void) {
    const RunnerEntry ENTRIES[] = {
        { .arg_list = { "ls", "-z", NULL },
          .successful = false,
          .expected_output = GG_STR(
              "ls: invalid option -- 'z'\nTry 'ls --help' for more information.\n"
          ) },
        { .arg_list = { "echo", "hello", NULL },
          .successful = true,
          .expected_output = GG_STR("hello\n") },
        { .arg_list = { "ls-l", NULL },
          .successful = false,
          .expected_output = GG_STR("") },
        { .arg_list = { "ls", "-l", NULL },
          .successful = true,
          .expected_output = { 0 } }
    };
    uint8_t output_buf[256];

    for (size_t i = 0; i < sizeof(ENTRIES) / sizeof(*ENTRIES); ++i) {
        const RunnerEntry *entry = &ENTRIES[i];
        GgError err = ggl_exec_command(entry->arg_list);
        bool successful = err == GG_ERR_OK;
        GG_LOGI("Success: %s", successful ? "true" : "false");
        assert(entry->successful == successful);
    }

    for (size_t i = 0; i < sizeof(ENTRIES) / sizeof(*ENTRIES); ++i) {
        const RunnerEntry *entry = &ENTRIES[i];
        GgBuffer output = GG_BUF(output_buf);

        GgError err = ggl_exec_command_with_output(
            entry->arg_list, gg_buf_writer(&output)
        );
        bool successful = (err == GG_ERR_OK) || (err == GG_ERR_NOMEM);
        output.len = (size_t) (output.data - output_buf);
        output.data = output_buf;
        GG_LOGI(
            "Success: %s\n%.*s",
            successful ? "true" : "false",
            (int) output.len,
            output.data
        );
        assert(entry->successful == successful);
        assert(output.len <= sizeof(output_buf));
        if (entry->expected_output.data != NULL) {
            assert(gg_buffer_eq(entry->expected_output, output));
        }
    }

    InputEntry inputs[] = {
        { .arg_list = { "cat", NULL },
          .input = gg_obj_buf(GG_STR("cat says hello\n")),
          .successful = true },
        { .arg_list = { "cat", NULL },
          .input = gg_obj_map(GG_MAP(
              gg_kv(GG_STR("Something"), gg_obj_buf(GG_STR("or other"))),
              gg_kv(GG_STR("Nothing"), GG_OBJ_NULL),
              gg_kv(GG_STR("Anything"), gg_obj_i64(64))
          )),
          .successful = true },
    };

    for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); ++i) {
        GgError err
            = ggl_exec_command_with_input(inputs[i].arg_list, inputs[i].input);
        bool successful = (err == GG_ERR_OK);
        assert(inputs[i].successful == successful);
    }

    return GG_ERR_OK;
}
