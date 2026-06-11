// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/recipe2unit.h>
#include <grp.h>
#include <pwd.h>
#include <recipe2unit-test.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Forward declare structure for use in the function below.
struct stat;

// Placeholder component names kept intentionally generic.
#define TEST_COMPONENT_NAME "com.example.TestComponent"
#define TEST_COMPONENT_VERSION "1.0.0"
#define TEST_HARD_DEP "com.example.HardDep"
#define TEST_SOFT_DEP "com.example.SoftDep"

// A recipe with one HARD and one SOFT dependency plus a run lifecycle, so the
// generated run/startup unit file contains the [Unit] dependency directives.
static const char TEST_RECIPE[]
    = "---\n"
      "RecipeFormatVersion: \"2020-01-25\"\n"
      "ComponentName: " TEST_COMPONENT_NAME "\n"
      "ComponentVersion: \"" TEST_COMPONENT_VERSION "\"\n"
      "ComponentDescription: Dependency ordering test component.\n"
      "ComponentPublisher: Example\n"
      "ComponentDependencies:\n"
      "  " TEST_HARD_DEP ":\n"
      "    VersionRequirement: \">=1.0.0 <2.0.0\"\n"
      "    DependencyType: \"HARD\"\n"
      "  " TEST_SOFT_DEP ":\n"
      "    VersionRequirement: \">=1.0.0 <2.0.0\"\n"
      "    DependencyType: \"SOFT\"\n"
      "Manifests:\n"
      "  - Platform:\n"
      "      os: linux\n"
      "      runtime: \"*\"\n"
      "    Lifecycle:\n"
      "      run: \"true\"\n";

static GgError write_text_file(const char *path, const char *content) {
    FILE *file = fopen(path, "we");
    if (file == NULL) {
        GG_LOGE("Failed to open %s for writing: %d.", path, errno);
        return GG_ERR_FAILURE;
    }
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);
    int close_ret = fclose(file);
    if ((written != len) || (close_ret != 0)) {
        GG_LOGE("Failed to write %s.", path);
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError read_text_file(
    const char *path, char *out, size_t out_cap, size_t *out_len
) {
    FILE *file = fopen(path, "re");
    if (file == NULL) {
        GG_LOGE("Failed to open %s for reading: %d.", path, errno);
        return GG_ERR_FAILURE;
    }
    size_t total = fread(out, 1, out_cap - 1, file);
    int close_ret = fclose(file);
    if (close_ret != 0) {
        return GG_ERR_FAILURE;
    }
    out[total] = '\0';
    *out_len = total;
    return GG_ERR_OK;
}

static GgError assert_contains(const char *haystack, const char *needle) {
    if (strstr(haystack, needle) == NULL) {
        GG_LOGE(
            "Generated unit file is missing expected directive: %s", needle
        );
        return GG_ERR_FAILURE;
    }
    GG_LOGI("Found expected directive: %s", needle);
    return GG_ERR_OK;
}

// Looks up the current process's user and group names so the generated unit
// owns its working directory as the caller. This keeps the test hermetic: the
// fchown() performed during generation targets the caller's own uid/gid and so
// succeeds without elevated privileges.
static GgError current_user_and_group(
    const char **user_out, const char **group_out
) {
    struct passwd *pw = getpwuid(getuid());
    if ((pw == NULL) || (pw->pw_name == NULL)) {
        GG_LOGE("Failed to resolve current user name.");
        return GG_ERR_FAILURE;
    }
    struct group *gr = getgrgid(getgid());
    if ((gr == NULL) || (gr->gr_name == NULL)) {
        GG_LOGE("Failed to resolve current group name.");
        return GG_ERR_FAILURE;
    }
    *user_out = pw->pw_name;
    *group_out = gr->gr_name;
    return GG_ERR_OK;
}

// nftw callback that removes a single path. Returns 0 so traversal continues
// even if an individual removal fails, mirroring the recursive-delete idiom
// used elsewhere in this project.
static int unlink_cb(
    const char *path, const struct stat *sb, int type_flag, struct FTW *ftw_buf
) {
    (void) sb;
    (void) type_flag;
    (void) ftw_buf;
    if (remove(path) != 0) {
        GG_LOGW("Failed to remove %s: %d.", path, errno);
    }
    return 0;
}

// Recursively removes the temporary directory tree (recipe, generated unit
// files, the work directory, and the directories themselves) so the test
// leaves nothing behind under /tmp. Registered with GG_CLEANUP so it runs on
// every exit path, including early error returns.
static void cleanup_remove_tree(char **path) {
    if ((path == NULL) || (*path == NULL)) {
        return;
    }
    // FTW_DEPTH visits directory contents before the directory itself so the
    // tree empties bottom-up; FTW_PHYS avoids following symlinks.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    (void) nftw(*path, unlink_cb, 16, FTW_DEPTH | FTW_PHYS);
}

GgError run_recipe2unit_test(void) {
    // Build a temporary root directory laid out like a Greengrass root:
    //   <root>/packages/recipes/<name>-<version>.yml
    char root_dir[] = "/tmp/recipe2unit-test-XXXXXX";
    if (mkdtemp(root_dir) == NULL) {
        GG_LOGE("Failed to create temp dir: %d.", errno);
        return GG_ERR_FAILURE;
    }
    // Ensure the entire temp tree is removed on every exit path below.
    GG_CLEANUP(cleanup_remove_tree, (char *) root_dir);

    char path_buf[4096];
    int printed = snprintf(path_buf, sizeof(path_buf), "%s/packages", root_dir);
    if ((printed < 0) || ((size_t) printed >= sizeof(path_buf))) {
        return GG_ERR_FAILURE;
    }
    if ((mkdir(path_buf, 0755) != 0) && (errno != EEXIST)) {
        GG_LOGE("Failed to create %s: %d.", path_buf, errno);
        return GG_ERR_FAILURE;
    }
    printed
        = snprintf(path_buf, sizeof(path_buf), "%s/packages/recipes", root_dir);
    if ((printed < 0) || ((size_t) printed >= sizeof(path_buf))) {
        return GG_ERR_FAILURE;
    }
    if ((mkdir(path_buf, 0755) != 0) && (errno != EEXIST)) {
        GG_LOGE("Failed to create %s: %d.", path_buf, errno);
        return GG_ERR_FAILURE;
    }

    printed = snprintf(
        path_buf,
        sizeof(path_buf),
        "%s/packages/recipes/%s-%s.yml",
        root_dir,
        TEST_COMPONENT_NAME,
        TEST_COMPONENT_VERSION
    );
    if ((printed < 0) || ((size_t) printed >= sizeof(path_buf))) {
        return GG_ERR_FAILURE;
    }
    GgError ret = write_text_file(path_buf, TEST_RECIPE);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    const char *user = NULL;
    const char *group = NULL;
    ret = current_user_and_group(&user, &group);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static Recipe2UnitArgs args = { 0 };
    GgBuffer recipe_runner_path = GG_STR("/bin/sh");

    int root_path_fd;
    ret = gg_dir_open(
        gg_buffer_from_null_term(root_dir), O_PATH, false, &root_path_fd
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open root dir.");
        return ret;
    }
    GG_CLEANUP(cleanup_close, root_path_fd);
    args.root_path_fd = root_path_fd;

    memcpy(args.root_dir, root_dir, strlen(root_dir) + 1);
    args.user = user;
    args.group = group;
    memcpy(
        args.recipe_runner_path,
        recipe_runner_path.data,
        recipe_runner_path.len + 1
    );
    args.component_name = GG_STR(TEST_COMPONENT_NAME);
    args.component_version = GG_STR(TEST_COMPONENT_VERSION);

    GgObject recipe_map;
    GgObject *component_name_obj;
    static uint8_t big_buffer_for_bump[50000];
    GgArena alloc = gg_arena_init(GG_BUF(big_buffer_for_bump));
    HasPhase phases = { 0 };

    ret = convert_to_unit(
        &args, &alloc, &recipe_map, &component_name_obj, &phases
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("convert_to_unit failed.");
        return ret;
    }
    if (!phases.has_run_startup) {
        GG_LOGE("Expected a run/startup unit file to be generated.");
        return GG_ERR_FAILURE;
    }

    // Read back the generated run/startup unit file.
    printed = snprintf(
        path_buf,
        sizeof(path_buf),
        "%s/ggl.%s.service",
        root_dir,
        TEST_COMPONENT_NAME
    );
    if ((printed < 0) || ((size_t) printed >= sizeof(path_buf))) {
        return GG_ERR_FAILURE;
    }

    static char unit_text[8192];
    size_t unit_len = 0;
    ret = read_text_file(path_buf, unit_text, sizeof(unit_text), &unit_len);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_LOGI("Generated unit file (%zu bytes):\n%s", unit_len, unit_text);

    // A HARD dependency must produce BindsTo= AND a matching After= for
    // ordering. A SOFT dependency must produce Wants= AND a matching After=.
    GgError check = GG_ERR_OK;
    GgError step;
    step = assert_contains(unit_text, "BindsTo=ggl." TEST_HARD_DEP ".service");
    check = (check == GG_ERR_OK) ? step : check;
    step = assert_contains(unit_text, "After=ggl." TEST_HARD_DEP ".service");
    check = (check == GG_ERR_OK) ? step : check;
    step = assert_contains(unit_text, "Wants=ggl." TEST_SOFT_DEP ".service");
    check = (check == GG_ERR_OK) ? step : check;
    step = assert_contains(unit_text, "After=ggl." TEST_SOFT_DEP ".service");
    check = (check == GG_ERR_OK) ? step : check;

    if (check != GG_ERR_OK) {
        GG_LOGE("Dependency ordering assertions failed.");
        return check;
    }

    GG_LOGI("All dependency ordering assertions passed.");
    return GG_ERR_OK;
}
