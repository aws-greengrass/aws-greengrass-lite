// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "dataplane_manager.h"
#include "deployment_configuration.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/http.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/vector.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static GglError generate_resolve_component_candidates_body(
    GglBuffer component_name,
    GglBuffer component_requirements,
    GglByteVec *body_vec,
    GglAlloc *alloc
) {
    GglObject architecture_detail_read_value;
    GglError ret = ggl_gg_config_read(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("platformOverride"),
            GGL_STR("architecture.detail")
        ),
        alloc,
        &architecture_detail_read_value
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGD("No architecture.detail found, so not including it in the "
                 "component candidates search.");
        architecture_detail_read_value = GGL_OBJ_BUF(GGL_STR(""));
    }

    if (architecture_detail_read_value.type != GGL_TYPE_BUF) {
        GGL_LOGD(
            "architecture.detail platformOverride in the config is not a "
            "buffer, so not including it in the component candidates search"
        );
        architecture_detail_read_value = GGL_OBJ_BUF(GGL_STR(""));
    }

    // TODO: Support platform attributes for platformOverride configuration
    GglMap platform_attributes = GGL_MAP(
        { GGL_STR("runtime"), GGL_OBJ_BUF(GGL_STR("aws_nucleus_lite")) },
        { GGL_STR("os"), GGL_OBJ_BUF(GGL_STR("linux")) },
        { GGL_STR("architecture"), GGL_OBJ_BUF(get_current_architecture()) },
    );

    if (architecture_detail_read_value.buf.len != 0) {
        platform_attributes = GGL_MAP(
            { GGL_STR("runtime"), GGL_OBJ_BUF(GGL_STR("aws_nucleus_lite")) },
            { GGL_STR("os"), GGL_OBJ_BUF(GGL_STR("linux")) },
            { GGL_STR("architecture"),
              GGL_OBJ_BUF(get_current_architecture()) },
            { GGL_STR("architecture.detail"), architecture_detail_read_value }
        );
    }

    GglMap platform_info = GGL_MAP(
        { GGL_STR("name"), GGL_OBJ_BUF(GGL_STR("linux")) },
        { GGL_STR("attributes"), GGL_OBJ_MAP(platform_attributes) }
    );

    GglMap version_requirements_map
        = GGL_MAP({ GGL_STR("requirements"),
                    GGL_OBJ_BUF(component_requirements) });

    GglMap component_map = GGL_MAP(
        { GGL_STR("componentName"), GGL_OBJ_BUF(component_name) },
        { GGL_STR("versionRequirements"),
          GGL_OBJ_MAP(version_requirements_map) }
    );

    GglList candidates_list = GGL_LIST(GGL_OBJ_MAP(component_map));

    GglMap request_body = GGL_MAP(
        { GGL_STR("componentCandidates"), GGL_OBJ_LIST(candidates_list) },
        { GGL_STR("platform"), GGL_OBJ_MAP(platform_info) }
    );

    static uint8_t rcc_buf[4096];
    GglBuffer rcc_body = GGL_BUF(rcc_buf);
    ret = ggl_json_encode(GGL_OBJ_MAP(request_body), &rcc_body);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error while encoding body for ResolveComponentCandidates call"
        );
        return ret;
    }

    GglError byte_vec_ret = GGL_ERR_OK;
    ggl_byte_vec_chain_append(&byte_vec_ret, body_vec, rcc_body);
    ggl_byte_vec_chain_push(&byte_vec_ret, body_vec, '\0');

    GGL_LOGD("Body for call: %s", body_vec->buf.data);

    return GGL_ERR_OK;
}

GglError make_dataplane_call(
    GglBuffer uri_path, char *body, GglBuffer *response
) {
    GglByteVec data_endpoint = GGL_BYTE_VEC(config.data_endpoint);
    GglError ret
        = read_nucleus_config(GGL_STR("iotDataEndpoint"), &data_endpoint.buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get dataplane endpoint.");
        return ret;
    }

    GglByteVec region = GGL_BYTE_VEC(config.region);
    ret = read_nucleus_config(GGL_STR("awsRegion"), &region.buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get region.");
        return ret;
    }

    GglByteVec port = GGL_BYTE_VEC(config.port);
    ret = read_nucleus_config(GGL_STR("greengrassDataPlanePort"), &port.buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get dataplane port.");
        return ret;
    }

    GglByteVec pkey_path = GGL_BYTE_VEC(config.pkey_path);
    ret = read_system_config(GGL_STR("privateKeyPath"), &pkey_path.buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get private key path.");
        return ret;
    }

    GglByteVec cert_path = GGL_BYTE_VEC(config.cert_path);
    ret = read_system_config(GGL_STR("certificateFilePath"), &cert_path.buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get certificate path.");
        return ret;
    }

    GglByteVec rootca_path = GGL_BYTE_VEC(config.rootca_path);
    ret = read_system_config(GGL_STR("rootCaPath"), &rootca_path.buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get certificate path.");
        return ret;
    }

    CertificateDetails cert_details
        = { .gghttplib_cert_path = config.cert_path,
            .gghttplib_root_ca_path = config.rootca_path,
            .gghttplib_p_key_path = config.pkey_path };

    ret = gg_dataplane_call(
        data_endpoint.buf, port.buf, uri_path, cert_details, body, response
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

GglError resolve_component_with_cloud(
    GglBuffer component_name,
    GglBuffer version_requirements,
    GglBuffer *response
) {
    static char resolve_candidates_body_buf[2048];
    GglByteVec body_vec = GGL_BYTE_VEC(resolve_candidates_body_buf);
    static uint8_t rcc_body_config_read_mem[128];
    GglBumpAlloc rcc_balloc
        = ggl_bump_alloc_init(GGL_BUF(rcc_body_config_read_mem));
    GglError ret = generate_resolve_component_candidates_body(
        component_name, version_requirements, &body_vec, &rcc_balloc.alloc
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to generate body for resolveComponentCandidates call");
        return ret;
    }

    ret = make_dataplane_call(
        GGL_STR("greengrass/v2/resolveComponentCandidates"),
        resolve_candidates_body_buf,
        response
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Cloud resolution for the component failed with response %.*s.",
            (int) response->len,
            response->data
        );
        return ret;
    }

    GGL_LOGD(
        "Received response from resolveComponentCandidates: %.*s",
        (int) response->len,
        response->data
    );

    return GGL_ERR_OK;
}

GglError get_device_thing_groups(GglBuffer *response) {
    uint8_t thing_name_arr[128];
    GglBuffer thing_name = GGL_BUF(thing_name_arr);
    GglError ret = read_system_config(GGL_STR("thingName"), &thing_name);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to get thing name.");
        return ret;
    }

    static uint8_t uri_path_buf[PATH_MAX];
    GglByteVec uri_path_vec = GGL_BYTE_VEC(uri_path_buf);
    ret = ggl_byte_vec_append(
        &uri_path_vec, GGL_STR("greengrass/v2/coreDevices/")
    );
    ggl_byte_vec_chain_append(&ret, &uri_path_vec, thing_name);
    ggl_byte_vec_chain_append(&ret, &uri_path_vec, GGL_STR("/thingGroups"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create thing groups call uri.");
        return ret;
    }

    ret = make_dataplane_call(uri_path_vec.buf, NULL, response);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "The listThingGroupsForCoreDevice call failed with response %.*s.",
            (int) response->len,
            response->data
        );
        return ret;
    }

    GGL_LOGD(
        "Received response from thingGroups dataplane call: %.*s",
        (int) response->len,
        response->data
    );

    return GGL_ERR_OK;
}
