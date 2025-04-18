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

GglError gg_uri_parse(GglArena *arena, GglBuffer uri, GglUriInfo *info);

#endif
