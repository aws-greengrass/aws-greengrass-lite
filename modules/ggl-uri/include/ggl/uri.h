#ifndef GGHTTPLIB_URI_H
#define GGHTTPLIB_URI_H

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>

typedef struct GglUriInfo {
    GgBuffer scheme;
    GgBuffer userinfo;
    GgBuffer host;
    GgBuffer port;
    GgBuffer path;
    GgBuffer file;
} GglUriInfo;

typedef struct GglDockerUriInfo {
    GgBuffer registry;
    GgBuffer username;
    GgBuffer repository;
    GgBuffer tag;
    GgBuffer digest_algorithm;
    GgBuffer digest;
} GglDockerUriInfo;

GgError gg_uri_parse(GgArena *arena, GgBuffer uri, GglUriInfo *info);

GgError gg_docker_uri_parse(GgBuffer uri, GglDockerUriInfo *info);

#endif
