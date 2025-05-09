#ifndef GGHTTPLIB_URI_H
#define GGHTTPLIB_URI_H

#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>

typedef struct GglUriInfo {
    GglBuffer scheme;
    GglBuffer userinfo;
    GglBuffer host;
    GglBuffer port;
    GglBuffer path;
    GglBuffer file;
} GglUriInfo;

typedef struct GglDockerUriInfo {
    GglBuffer registry;
    GglBuffer username;
    GglBuffer repository;
    GglBuffer tag;
    GglBuffer digest_algorithm;
    GglBuffer digest;
} GglDockerUriInfo;

GglError gg_uri_parse(GglArena *arena, GglBuffer uri, GglUriInfo *info);

GglError gg_docker_uri_parse(GglBuffer uri, GglDockerUriInfo *info);

#endif
