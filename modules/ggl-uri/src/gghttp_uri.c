#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/uri.h>
#include <string.h>
#include <uriparser/Uri.h>
#include <uriparser/UriBase.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

static GgBuffer buffer_from_text_range(UriTextRangeA range) {
    return (GgBuffer) { .data = (uint8_t *) range.first,
                        .len = (size_t) (range.afterLast - range.first) };
}

static GgBuffer buffer_from_linked_list(
    UriPathSegmentA *head, UriPathSegmentA *tail
) {
    if (head == NULL) {
        return GG_STR("");
    }
    return (GgBuffer) { .data = (uint8_t *) head->text.first,
                        .len
                        = (size_t) (tail->text.afterLast - head->text.first) };
}

static void *ggl_uri_malloc(struct UriMemoryManagerStruct *mem, size_t size) {
    return gg_arena_alloc((GgArena *) mem->userData, size, alignof(size_t));
}

static void *ggl_uri_calloc(
    struct UriMemoryManagerStruct *mem, size_t count, size_t size_per_element
) {
    void *block = ggl_uri_malloc(mem, count * size_per_element);
    if (block != NULL) {
        memset(block, 0, count * size_per_element);
    }
    return block;
}

static void *ggl_uri_realloc(
    struct UriMemoryManagerStruct *mem, void *block, size_t size
) {
    (void) mem;
    (void) block;
    (void) size;
    return NULL;
}

static void *ggl_uri_reallocarray(
    struct UriMemoryManagerStruct *mem,
    void *block,
    size_t count,
    size_t size_per_element
) {
    (void) mem;
    (void) block;
    (void) count;
    (void) size_per_element;
    return NULL;
}

static void ggl_uri_free(struct UriMemoryManagerStruct *mem, void *block) {
    (void) mem;
    (void) block;
}

static GgError convert_uriparser_error(int error) {
    switch (error) {
    case URI_SUCCESS:
        return GG_ERR_OK;
    case URI_ERROR_SYNTAX:
        return GG_ERR_PARSE;
    case URI_ERROR_NULL:
        return GG_ERR_INVALID;
    case URI_ERROR_MALLOC:
        return GG_ERR_NOMEM;
    default:
        return GG_ERR_FAILURE;
    }
}

GgError gg_uri_parse(GgArena *arena, GgBuffer uri, GglUriInfo *info) {
    UriUriA result = { 0 };

    // TODO: can we patch uriparser to not need to allocate memory for a linked
    // list?
    UriMemoryManager mem = { .calloc = ggl_uri_calloc,
                             .realloc = ggl_uri_realloc,
                             .reallocarray = ggl_uri_reallocarray,
                             .malloc = ggl_uri_malloc,
                             .free = ggl_uri_free,
                             .userData = arena };
    const char *error_pos = NULL;

    int uri_error = uriParseSingleUriExMmA(
        &result,
        (const char *) uri.data,
        (const char *) (&uri.data[uri.len]),
        &error_pos,
        &mem
    );
    if (uri_error != URI_SUCCESS) {
        GG_LOGE("Failed to parse URI %.*s", (int) uri.len, uri.data);
        return convert_uriparser_error(uri_error);
    }

    info->scheme = buffer_from_text_range(result.scheme);
    info->userinfo = buffer_from_text_range(result.userInfo);
    info->host = buffer_from_text_range(result.hostText);
    info->port = buffer_from_text_range(result.portText);
    info->path = buffer_from_linked_list(result.pathHead, result.pathTail);
    if (result.pathTail != NULL) {
        info->file = buffer_from_text_range(result.pathTail->text);
    } else {
        info->file = GG_STR("");
    }
    uriFreeUriMembersMmA(&result, &mem);

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
                "More than two slashes found while parsing Docker URI, URI is invalid."
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
                "More than three colons found while parsing Docker URI, URI is invalid."
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
                "More than one '@' symbol found while parsing Docker URI, URI is invalid."
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
    // Parse the registry segment that looks like
    // [registry-host][:port]/[username/]...
    assert(slash_count <= 2);
    if (slash_count == 0) {
        // URI has no registry segment. Default to official docker hub for
        // registry.
        info->registry = GG_STR("docker.io");
        GG_LOGT(
            "Assuming official docker hub by default while parsing Docker URI as no registry is provided."
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
        // URI only has either username or registry.
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
            "Docker URI contains a digest but does not include a colon in the digest"
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
    // Parse repo segment that looks like
    // ...repository[:tag][@algo:digest]
    GgError err;
    if (at != SIZE_MAX) {
        // Digest is included, so there should be a : after @.
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
        // No digest provided
        err = parse_repo_without_digest(
            info, uri, slashes, slash_count, colons, colon_count
        );
        if (err != GG_ERR_OK) {
            GG_LOGE(
                "Error while parsing Docker URI repository segment without digest"
            );
            return err;
        }
    }

    return GG_ERR_OK;
}

GgError gg_docker_uri_parse(GgBuffer uri, GglDockerUriInfo *info) {
    // [registry-host][:port]/[username/]repository[:tag][@algo:digest]
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
