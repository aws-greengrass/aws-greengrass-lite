#include "ggl/vector.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"

GglError ggl_obj_vec_push(GglObjVec *vector, GglObject object) {
    if (vector->list.len + 1 >= vector->capacity) {
        return GGL_ERR_NOMEM;
    }
    if (object.type == GGL_TYPE_BUF) {
        GGL_LOGI(
            "ggl_obj_vec_push",
            "Inserting BUF %.*s",
            (int) object.buf.len,
            (char *) object.buf.data
        );
    }
    vector->list.items[vector->list.len] = object;
    vector->list.len++;
    return GGL_ERR_OK;
}

GglError ggl_obj_vec_pop(GglObjVec *vector, GglObject * out) {
    if (vector->list.len == 0) {
        return GGL_ERR_RANGE;
    }
    if(out != NULL) {
        *out= vector->list.items[vector->list.len - 1];
    }
    if (last_item->type == GGL_TYPE_BUF) {
        GGL_LOGI(
            "ggl_obj_vec_push",
            "Popping BUF %.*s",
            (int) last_item->buf.len,
            (char *) last_item->buf.data
        );
    }
    vector->list.len--;
    return GGL_ERR_OK;
}
