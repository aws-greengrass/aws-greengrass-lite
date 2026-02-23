// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <curl/curl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/uri.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GgBuffer copy_curl_str_to_arena(GgArena *arena, char *str) {
    if (str == NULL) {
        return (GgBuffer) { 0 };
    }
    size_t len = strlen(str);
    if (len == 0) {
        return (GgBuffer) { 0 };
    }
    uint8_t *buf = gg_arena_alloc(arena, len, 1);
    if (buf == NULL) {
        return (GgBuffer) { 0 };
    }
    // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
    memcpy(buf, str, len);
    return (GgBuffer) { .data = buf, .len = len };
}

static GgBuffer extract_file_from_path(GgBuffer path) {
    if (path.len == 0) {
        return (GgBuffer) { 0 };
    }
    for (size_t i = path.len; i > 0; i--) {
        if (path.data[i - 1] == '/') {
            if (i < path.len) {
                return (GgBuffer) { .data = path.data + i,
                                    .len = path.len - i };
            }
            return (GgBuffer) { 0 };
        }
    }
    // No slash found - entire path is the filename
    return path;
}

// Parse non-standard URIs like "docker:path" (scheme:path without //)
static GgError parse_simple_uri(GgBuffer uri, GglUriInfo *info) {
    // Find the colon separating scheme from path
    size_t colon_pos = 0;
    for (size_t i = 0; i < uri.len; i++) {
        if (uri.data[i] == ':') {
            colon_pos = i;
            break;
        }
    }
    if (colon_pos == 0) {
        return GG_ERR_PARSE;
    }

    info->scheme = (GgBuffer) { .data = uri.data, .len = colon_pos };
    info->path = (GgBuffer) { .data = uri.data + colon_pos + 1,
                              .len = uri.len - colon_pos - 1 };
    info->file = extract_file_from_path(info->path);
    info->userinfo = (GgBuffer) { 0 };
    info->host = (GgBuffer) { 0 };
    info->port = (GgBuffer) { 0 };

    return GG_ERR_OK;
}

GgError gg_uri_parse(GgArena *arena, GgBuffer uri, GglUriInfo *info) {
    // Create null-terminated copy for libcurl
    char *uri_str = gg_arena_alloc(arena, uri.len + 1, 1);
    if (uri_str == NULL) {
        return GG_ERR_NOMEM;
    }
    memcpy(uri_str, uri.data, uri.len);
    uri_str[uri.len] = '\0';

    CURLU *url = curl_url();
    if (url == NULL) {
        return GG_ERR_NOMEM;
    }

    CURLUcode rc
        = curl_url_set(url, CURLUPART_URL, uri_str, CURLU_NON_SUPPORT_SCHEME);
    if (rc != CURLUE_OK) {
        curl_url_cleanup(url);
        // Fall back to simple scheme:path parsing for non-standard URIs
        GgError err = parse_simple_uri(uri, info);
        if (err == GG_ERR_OK) {
            GG_LOGD("Scheme: %.*s", (int) info->scheme.len, info->scheme.data);
            if (info->path.len > 0) {
                GG_LOGD("Path: %.*s", (int) info->path.len, info->path.data);
            }
        }
        return err;
    }

    char *scheme = NULL;
    char *user = NULL;
    char *host = NULL;
    char *port = NULL;
    char *path = NULL;

    curl_url_get(url, CURLUPART_SCHEME, &scheme, 0);
    curl_url_get(url, CURLUPART_USER, &user, 0);
    curl_url_get(url, CURLUPART_HOST, &host, 0);
    curl_url_get(url, CURLUPART_PORT, &port, 0);
    curl_url_get(url, CURLUPART_PATH, &path, 0);

    info->scheme = copy_curl_str_to_arena(arena, scheme);
    info->userinfo = copy_curl_str_to_arena(arena, user);
    info->host = copy_curl_str_to_arena(arena, host);
    info->port = copy_curl_str_to_arena(arena, port);
    info->path = copy_curl_str_to_arena(arena, path);
    // Strip leading slash from path to match uriparser behavior
    if ((info->path.len > 0) && (info->path.data[0] == '/')) {
        info->path.data++;
        info->path.len--;
    }
    info->file = extract_file_from_path(info->path);

    curl_free(scheme);
    curl_free(user);
    curl_free(host);
    curl_free(port);
    curl_free(path);
    curl_url_cleanup(url);

    if (info->scheme.len > 0) {
        GG_LOGD("Scheme: %.*s", (int) info->scheme.len, info->scheme.data);
    }
    if (info->userinfo.len > 0) {
        GG_LOGD("UserInfo: Present");
    }
    if (info->host.len > 0) {
        GG_LOGD("Host: %.*s", (int) info->host.len, info->host.data);
    }
    if (info->port.len > 0) {
        GG_LOGD("Port: %.*s", (int) info->port.len, info->port.data);
    }
    if (info->path.len > 0) {
        GG_LOGD("Path: %.*s", (int) info->path.len, info->path.data);
    }

    return GG_ERR_OK;
}

static GgError find_docker_uri_separators(
    GgBuffer uri,
    size_t slashes[static 2],
    size_t *slash_count,
    size_t colons[static 3],
    size_t *colon_count,
    size_t *at,
    bool *has_registry
) {
    if (uri.len == 0) {
        GG_LOGE("Docker URI length should not be zero");
        return GG_ERR_INVALID;
    }

    size_t at_count = 0;
    for (size_t position = uri.len; position > 0; position--) {
        if (uri.data[position - 1] == '/') {
            if (*slash_count < 2) {
                slashes[*slash_count] = position - 1;
                *slash_count += 1;
                GG_LOGT("Found a slash while parsing Docker URI");
                continue;
            }
            GG_LOGE(
                "More than two slashes found while parsing Docker URI, URI is "
                "invalid."
            );
            return GG_ERR_INVALID;
        }
        if (uri.data[position - 1] == ':') {
            if (*colon_count < 3) {
                colons[*colon_count] = position - 1;
                *colon_count += 1;
                GG_LOGT("Found a colon while parsing Docker URI");
                continue;
            }
            GG_LOGE(
                "More than three colons found while parsing Docker URI, URI is "
                "invalid."
            );
            return GG_ERR_INVALID;
        }
        if (uri.data[position - 1] == '@') {
            if (at_count == 0) {
                *at = position - 1;
                at_count += 1;
                GG_LOGT("Found an @ while parsing Docker URI");
                continue;
            }
            GG_LOGE(
                "More than one '@' symbol found while parsing Docker URI, URI "
                "is invalid."
            );
            return GG_ERR_INVALID;
        }
        if (uri.data[position - 1] == '.') {
            if (*slash_count == 0) {
                *has_registry = true;
            }
        }
    }
    return GG_ERR_OK;
}

static GgError parse_docker_registry_segment(
    GglDockerUriInfo *info,
    GgBuffer uri,
    size_t slashes[static 2],
    size_t slash_count,
    bool has_registry
) {
    assert(slash_count <= 2);
    if (slash_count == 0) {
        info->registry = GG_STR("docker.io");
        GG_LOGT(
            "Assuming official docker hub by default while parsing Docker URI "
            "as no registry is provided."
        );
    } else if (slash_count == 2) {
        info->username = gg_buffer_substr(uri, slashes[1] + 1, slashes[0]);
        GG_LOGT(
            "Read username from Docker URI as %.*s",
            (int) info->username.len,
            info->username.data
        );
        info->registry = gg_buffer_substr(uri, 0, slashes[1]);
        GG_LOGT(
            "Read registry from Docker URI as %.*s",
            (int) info->registry.len,
            info->registry.data
        );
    } else {
        if (has_registry) {
            GG_LOGT("No username provided in Docker URI");
            info->registry = gg_buffer_substr(uri, 0, slashes[0]);
            GG_LOGT(
                "Read registry from Docker URI as %.*s",
                (int) info->registry.len,
                info->registry.data
            );
        } else {
            GG_LOGT("No registry provided in Docker URI");
            info->username = gg_buffer_substr(uri, 0, slashes[0]);
            GG_LOGT(
                "Read username from Docker URI as %.*s",
                (int) info->username.len,
                info->username.data
            );
        }
    }

    return GG_ERR_OK;
}

static GgError parse_repo_with_digest(
    GglDockerUriInfo *info,
    GgBuffer uri,
    size_t slashes[static 2],
    size_t slash_count,
    size_t colons[static 3],
    size_t colon_count,
    size_t at
) {
    if (colon_count == 0 || colons[0] < at) {
        GG_LOGE(
            "Docker URI contains a digest but does not include a colon in the "
            "digest"
        );
        return GG_ERR_INVALID;
    }
    assert(colons[0] != SIZE_MAX);
    info->digest_algorithm = gg_buffer_substr(uri, at + 1, colons[0]);
    GG_LOGT(
        "Read digest algorithm from Docker URI as %.*s",
        (int) info->digest_algorithm.len,
        info->digest_algorithm.data
    );
    info->digest = gg_buffer_substr(uri, colons[0] + 1, SIZE_MAX);
    GG_LOGT(
        "Read digest from Docker URI as %.*s",
        (int) info->digest.len,
        info->digest.data
    );

    if (colon_count >= 2
        && colons[1] > (slash_count == 0 ? 0 : 1) * slashes[0]) {
        assert(colons[1] != SIZE_MAX);
        info->tag = gg_buffer_substr(uri, colons[1] + 1, at);
        GG_LOGT(
            "Read tag from Docker URI as %.*s",
            (int) info->tag.len,
            info->tag.data
        );
        info->repository = gg_buffer_substr(
            uri, slash_count == 0 ? 0 : slashes[0] + 1, colons[1]
        );
        GG_LOGT(
            "Read repository from Docker URI as %.*s",
            (int) info->repository.len,
            info->repository.data
        );
    } else {
        GG_LOGT("No tag found for Docker URI.");
        info->repository
            = gg_buffer_substr(uri, slash_count == 0 ? 0 : slashes[0] + 1, at);
        GG_LOGT(
            "Read repository from Docker URI as %.*s",
            (int) info->repository.len,
            info->repository.data
        );
    }

    return GG_ERR_OK;
}

static GgError parse_repo_without_digest(
    GglDockerUriInfo *info,
    GgBuffer uri,
    size_t slashes[static 2],
    size_t slash_count,
    size_t colons[static 3],
    size_t colon_count
) {
    if (colon_count == 2 + (slash_count == 0 ? 0 : 1)) {
        GG_LOGE("Docker URI has too many colons.");
        return GG_ERR_INVALID;
    }

    if (colons[0] > (slash_count == 0 ? 0 : 1) * slashes[0]) {
        info->tag = gg_buffer_substr(uri, colons[0] + 1, SIZE_MAX);
        GG_LOGT(
            "Read tag from Docker URI as %.*s",
            (int) info->tag.len,
            info->tag.data
        );
        info->repository = gg_buffer_substr(
            uri, slash_count == 0 ? 0 : slashes[0] + 1, colons[0]
        );
        GG_LOGT(
            "Read repository from Docker URI as %.*s",
            (int) info->repository.len,
            info->repository.data
        );
    } else {
        GG_LOGT("No tag or digest found for Docker URI.");
        info->repository = gg_buffer_substr(
            uri, slash_count == 0 ? 0 : slashes[0] + 1, SIZE_MAX
        );
        GG_LOGT(
            "Read repository from Docker URI as %.*s",
            (int) info->repository.len,
            info->repository.data
        );
    }

    return GG_ERR_OK;
}

static GgError parse_docker_repo_segment(
    GglDockerUriInfo *info,
    GgBuffer uri,
    size_t slashes[static 2],
    size_t slash_count,
    size_t colons[static 3],
    size_t colon_count,
    size_t at
) {
    GgError err;
    if (at != SIZE_MAX) {
        err = parse_repo_with_digest(
            info, uri, slashes, slash_count, colons, colon_count, at
        );
        if (err != GG_ERR_OK) {
            GG_LOGE(
                "Error while parsing Docker URI repository segment with digest"
            );
            return err;
        }
    } else {
        err = parse_repo_without_digest(
            info, uri, slashes, slash_count, colons, colon_count
        );
        if (err != GG_ERR_OK) {
            GG_LOGE("Error while parsing Docker URI repository segment without "
                    "digest");
            return err;
        }
    }

    return GG_ERR_OK;
}

GgError gg_docker_uri_parse(GgBuffer uri, GglDockerUriInfo *info) {
    size_t slashes[2] = { SIZE_MAX, SIZE_MAX };
    size_t slash_count = 0;
    size_t colons[3] = { SIZE_MAX, SIZE_MAX, SIZE_MAX };
    size_t colon_count = 0;
    size_t at = SIZE_MAX;
    bool has_registry = false;

    GgError err = find_docker_uri_separators(
        uri, slashes, &slash_count, colons, &colon_count, &at, &has_registry
    );
    if (err != GG_ERR_OK) {
        GG_LOGE("Error while parsing Docker URI");
        return err;
    }

    err = parse_docker_registry_segment(
        info, uri, slashes, slash_count, has_registry
    );
    if (err != GG_ERR_OK) {
        GG_LOGE("Error while parsing Docker URI Registry Segment");
        return err;
    }

    err = parse_docker_repo_segment(
        info, uri, slashes, slash_count, colons, colon_count, at
    );
    if (err != GG_ERR_OK) {
        GG_LOGE("Error while parsing Docker URI Registry Segment");
        return err;
    }

    return GG_ERR_OK;
}
