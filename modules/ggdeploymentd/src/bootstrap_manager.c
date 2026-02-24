// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bootstrap_manager.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include "stale_component.h"
#include <assert.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

bool component_bootstrap_phase_completed(GgBuffer component_name) {
    // check config to see if component bootstrap steps have already been
    // completed
    uint8_t resp_mem[128] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(resp_mem));
    GgBuffer resp;
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("DeploymentService"),
            GG_STR("deploymentState"),
            GG_STR("bootstrapComponents"),
            component_name
        ),
        &alloc,
        &resp
    );
    if (ret == GG_ERR_OK) {
        GG_LOGD(
            "Bootstrap steps have already been run for %.*s.",
            (int) component_name.len,
            component_name.data
        );
        return true;
    }
    return false;
}

GgError save_component_info(
    GgBuffer component_name, GgBuffer component_version, GgBuffer type
) {
    GG_LOGD(
        "Saving component name and version for %.*s as type %.*s to the config to track deployment state.",
        (int) component_name.len,
        component_name.data,
        (int) type.len,
        type.data
    );

    if (gg_buffer_eq(type, GG_STR("completed"))) {
        GgError ret = ggl_gg_config_write(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("DeploymentService"),
                GG_STR("deploymentState"),
                GG_STR("components"),
                component_name
            ),
            gg_obj_buf(component_version),
            &(int64_t) { 3 }
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to write component info for %.*s to config.",
                (int) component_name.len,
                component_name.data
            );
            return ret;
        }
    } else if (gg_buffer_eq(type, GG_STR("bootstrap"))) {
        GgError ret = ggl_gg_config_write(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("DeploymentService"),
                GG_STR("deploymentState"),
                GG_STR("bootstrapComponents"),
                component_name
            ),
            gg_obj_buf(component_version),
            &(int64_t) { 3 }
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to write component info for %.*s to config.",
                (int) component_name.len,
                component_name.data
            );
            return ret;
        }
    } else {
        GG_LOGE(
            "Invalid component type of %.*s received. Expected type 'bootstrap' or 'completed'.",
            (int) type.len,
            type.data
        );
        return GG_ERR_INVALID;
    }

    return GG_ERR_OK;
}

GgError save_iot_jobs_id(GgBuffer jobs_id) {
    GG_LOGD(
        "Saving IoT Jobs ID %.*s in case of bootstrap.",
        (int) jobs_id.len,
        jobs_id.data
    );

    GgError ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("DeploymentService"),
            GG_STR("deploymentState"),
            GG_STR("jobsID")
        ),
        gg_obj_buf(jobs_id),
        &(int64_t) { 3 }
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write IoT Jobs ID to config.");
        return ret;
    }
    return GG_ERR_OK;
}

GgError save_deployment_info(GglDeployment *deployment) {
    GG_LOGD(
        "Encountered component requiring bootstrap. Saving deployment state to config."
    );

    GgObject deployment_doc = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("deployment_id"), gg_obj_buf(deployment->deployment_id)),
        gg_kv(
            GG_STR("recipe_directory_path"),
            gg_obj_buf(deployment->recipe_directory_path)
        ),
        gg_kv(
            GG_STR("artifacts_directory_path"),
            gg_obj_buf(deployment->artifacts_directory_path)
        ),
        gg_kv(
            GG_STR("configuration_arn"),
            gg_obj_buf(deployment->configuration_arn)
        ),
        gg_kv(GG_STR("thing_group"), gg_obj_buf(deployment->thing_group)),
        gg_kv(GG_STR("components"), gg_obj_map(deployment->components))
    ));

    GgError ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("DeploymentService"),
            GG_STR("deploymentState"),
            GG_STR("deploymentDoc")
        ),
        deployment_doc,
        &(int64_t) { 3 }
    );

    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write deployment document to config.");
        return ret;
    }

    uint8_t deployment_type_mem[24] = { 0 };
    GgBuffer deployment_type = GG_BUF(deployment_type_mem);
    if (deployment->type == LOCAL_DEPLOYMENT) {
        deployment_type = GG_STR("LOCAL_DEPLOYMENT");
    } else if (deployment->type == THING_GROUP_DEPLOYMENT) {
        deployment_type = GG_STR("THING_GROUP_DEPLOYMENT");
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("DeploymentService"),
            GG_STR("deploymentState"),
            GG_STR("deploymentType")
        ),
        gg_obj_buf(deployment_type),
        &(int64_t) { 3 }
    );

    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write deployment type to config.");
        return ret;
    }

    return GG_ERR_OK;
}

GgError retrieve_in_progress_deployment(
    GglDeployment *deployment, GgBuffer *jobs_id
) {
    GG_LOGD("Searching config for any in progress deployment.");

    GgBuffer config_mem = GG_BUF((uint8_t[2500]) { 0 });
    GgArena alloc = gg_arena_init(config_mem);
    GgObject deployment_config;

    GgError ret = ggl_gg_config_read(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("DeploymentService"),
            GG_STR("deploymentState")
        ),
        &alloc,
        &deployment_config
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    if (gg_obj_type(deployment_config) != GG_TYPE_MAP) {
        GG_LOGE("Retrieved config not a map.");
        return GG_ERR_INVALID;
    }

    GgObject *jobs_id_obj;
    ret = gg_map_validate(
        gg_obj_into_map(deployment_config),
        GG_MAP_SCHEMA(
            { GG_STR("jobsID"), GG_REQUIRED, GG_TYPE_BUF, &jobs_id_obj }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    assert(gg_obj_into_buf(*jobs_id_obj).len < 64);
    assert(jobs_id->len >= 64);

    memcpy(
        jobs_id->data,
        gg_obj_into_buf(*jobs_id_obj).data,
        gg_obj_into_buf(*jobs_id_obj).len
    );

    GgObject *deployment_type;
    ret = gg_map_validate(
        gg_obj_into_map(deployment_config),
        GG_MAP_SCHEMA({ GG_STR("deploymentType"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &deployment_type })
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (gg_buffer_eq(
            gg_obj_into_buf(*deployment_type), GG_STR("LOCAL_DEPLOYMENT")
        )) {
        deployment->type = LOCAL_DEPLOYMENT;
    } else if (gg_buffer_eq(
                   gg_obj_into_buf(*deployment_type),
                   GG_STR("THING_GROUP_DEPLOYMENT")
               )) {
        deployment->type = THING_GROUP_DEPLOYMENT;
    }

    GgObject *deployment_doc;
    ret = gg_map_validate(
        gg_obj_into_map(deployment_config),
        GG_MAP_SCHEMA({ GG_STR("deploymentDoc"),
                        GG_REQUIRED,
                        GG_TYPE_MAP,
                        &deployment_doc })
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgObject *deployment_id;
    ret = gg_map_validate(
        gg_obj_into_map(*deployment_doc),
        GG_MAP_SCHEMA({ GG_STR("deployment_id"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &deployment_id })
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->deployment_id = gg_obj_into_buf(*deployment_id);

    GgObject *recipe_directory_path;
    ret = gg_map_validate(
        gg_obj_into_map(*deployment_doc),
        GG_MAP_SCHEMA({ GG_STR("recipe_directory_path"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &recipe_directory_path })
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->recipe_directory_path = gg_obj_into_buf(*recipe_directory_path);

    GgObject *artifacts_directory_path;
    ret = gg_map_validate(
        gg_obj_into_map(*deployment_doc),
        GG_MAP_SCHEMA({ GG_STR("artifacts_directory_path"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &artifacts_directory_path })
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->artifacts_directory_path
        = gg_obj_into_buf(*artifacts_directory_path);

    GgObject *configuration_arn;
    ret = gg_map_validate(
        gg_obj_into_map(*deployment_doc),
        GG_MAP_SCHEMA({ GG_STR("configuration_arn"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &configuration_arn })
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->configuration_arn = gg_obj_into_buf(*configuration_arn);

    GgObject *thing_group;
    ret = gg_map_validate(
        gg_obj_into_map(*deployment_doc),
        GG_MAP_SCHEMA(
            { GG_STR("thing_group"), GG_REQUIRED, GG_TYPE_BUF, &thing_group }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->thing_group = gg_obj_into_buf(*thing_group);

    GgObject *components;
    ret = gg_map_validate(
        gg_obj_into_map(*deployment_doc),
        GG_MAP_SCHEMA(
            { GG_STR("components"), GG_REQUIRED, GG_TYPE_MAP, &components }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->components = gg_obj_into_map(*components);

    static uint8_t deployment_deep_copy_mem[5000] = { 0 };
    GgArena deployment_balloc = gg_arena_init(GG_BUF(deployment_deep_copy_mem));
    ret = deep_copy_deployment(deployment, &deployment_balloc);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to deep copy deployment.");
        return ret;
    }

    return GG_ERR_OK;
}

GgError delete_saved_deployment_from_config(void) {
    GG_LOGD("Deleting previously saved deployment from config.");

    GgError ret = ggl_gg_config_delete(GG_BUF_LIST(
        GG_STR("services"),
        GG_STR("DeploymentService"),
        GG_STR("deploymentState")
    ));

    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to delete previously saved deployment state from config."
        );
        return ret;
    }

    return GG_ERR_OK;
}

GgError process_bootstrap_phase(
    GgMap components,
    GgBuffer root_path,
    GgBufVec *bootstrap_comp_name_buf_vec,
    GglDeployment *deployment
) {
    int bootstrap_component_count = 0;
    GG_MAP_FOREACH (component, components) {
        GgBuffer component_name = gg_kv_key(*component);

        // check config to see if component bootstrap steps have already been
        // completed
        if (component_bootstrap_phase_completed(component_name)) {
            GG_LOGD("Bootstrap processed. Skipping component.");
            continue;
        }

        static uint8_t bootstrap_service_file_path_buf[PATH_MAX];
        GgByteVec bootstrap_service_file_path_vec
            = GG_BYTE_VEC(bootstrap_service_file_path_buf);
        GgError ret
            = gg_byte_vec_append(&bootstrap_service_file_path_vec, root_path);
        gg_byte_vec_chain_append(
            &ret, &bootstrap_service_file_path_vec, GG_STR("/")
        );
        gg_byte_vec_chain_append(
            &ret, &bootstrap_service_file_path_vec, GG_STR("ggl.")
        );
        gg_byte_vec_chain_append(
            &ret, &bootstrap_service_file_path_vec, component_name
        );
        gg_byte_vec_chain_append(
            &ret, &bootstrap_service_file_path_vec, GG_STR(".bootstrap.service")
        );
        if (ret == GG_ERR_OK) {
            // check if the current component name has relevant bootstrap
            // service file created
            int fd = -1;
            ret = gg_file_open(
                bootstrap_service_file_path_vec.buf, O_RDONLY, 0, &fd
            );
            if (ret != GG_ERR_OK) {
                GG_LOGD(
                    "Component %.*s does not have the relevant bootstrap "
                    "service file",
                    (int) component_name.len,
                    component_name.data
                );
            } else { // relevant bootstrap service file exists
                GG_CLEANUP(cleanup_close, fd);
                ret = disable_and_unlink_service(&component_name, BOOTSTRAP);
                if (ret != GG_ERR_OK) {
                    return ret;
                }
                GG_LOGI(
                    "Found bootstrap service file for %.*s. Processing.",
                    (int) component_name.len,
                    component_name.data
                );

                // add relevant component name into the vector
                ret = gg_buf_vec_push(
                    bootstrap_comp_name_buf_vec, component_name
                );
                if (ret != GG_ERR_OK) {
                    GG_LOGE("Failed to add the bootstrap component name "
                            "into vector");
                    return ret;
                }
                bootstrap_component_count++;

                // initiate link command for 'bootstrap'
                static uint8_t link_command_buf[PATH_MAX];
                GgByteVec link_command_vec = GG_BYTE_VEC(link_command_buf);
                ret = gg_byte_vec_append(
                    &link_command_vec, GG_STR("systemctl link ")
                );
                gg_byte_vec_chain_append(
                    &ret, &link_command_vec, bootstrap_service_file_path_vec.buf
                );
                gg_byte_vec_chain_push(&ret, &link_command_vec, '\0');
                if (ret != GG_ERR_OK) {
                    GG_LOGE(
                        "Failed to create systemctl link command for:%.*s",
                        (int) bootstrap_service_file_path_vec.buf.len,
                        bootstrap_service_file_path_vec.buf.data
                    );
                    return ret;
                }

                GG_LOGD(
                    "Command to execute: %.*s",
                    (int) link_command_vec.buf.len,
                    link_command_vec.buf.data
                );

                // NOLINTBEGIN(concurrency-mt-unsafe)
                int system_ret = system((char *) link_command_vec.buf.data);
                if (WIFEXITED(system_ret)) {
                    if (WEXITSTATUS(system_ret) != 0) {
                        GG_LOGE(
                            "systemctl link failed for:%.*s",
                            (int) bootstrap_service_file_path_vec.buf.len,
                            bootstrap_service_file_path_vec.buf.data
                        );
                        return ret;
                    }
                    GG_LOGI(
                        "systemctl link exited for %.*s with child status "
                        "%d\n",
                        (int) bootstrap_service_file_path_vec.buf.len,
                        bootstrap_service_file_path_vec.buf.data,
                        WEXITSTATUS(system_ret)
                    );
                } else {
                    GG_LOGE(
                        "systemctl link did not exit normally for %.*s",
                        (int) bootstrap_service_file_path_vec.buf.len,
                        bootstrap_service_file_path_vec.buf.data
                    );
                    return ret;
                }

                // initiate start command for 'bootstrap'
                static uint8_t start_command_buf[PATH_MAX];
                GgByteVec start_command_vec = GG_BYTE_VEC(start_command_buf);
                ret = gg_byte_vec_append(
                    &start_command_vec, GG_STR("systemctl start ")
                );
                gg_byte_vec_chain_append(
                    &ret, &start_command_vec, GG_STR("ggl.")
                );
                gg_byte_vec_chain_append(
                    &ret, &start_command_vec, component_name
                );
                gg_byte_vec_chain_append(
                    &ret, &start_command_vec, GG_STR(".bootstrap.service\0")
                );

                GG_LOGD(
                    "Command to execute: %.*s",
                    (int) start_command_vec.buf.len,
                    start_command_vec.buf.data
                );
                if (ret != GG_ERR_OK) {
                    GG_LOGE(
                        "Failed to create systemctl start command for %.*s",
                        (int) bootstrap_service_file_path_vec.buf.len,
                        bootstrap_service_file_path_vec.buf.data
                    );
                    return ret;
                }

                // save component to config to avoid rerunning bootstrap steps
                ret = save_component_info(
                    component_name,
                    gg_obj_into_buf(*gg_kv_val(component)),
                    GG_STR("bootstrap")
                );
                if (ret != GG_ERR_OK) {
                    GG_LOGE("Failed to save component info to config after "
                            "completing bootstrap steps.");
                    return ret;
                }

                system_ret = system((char *) start_command_vec.buf.data);
                // NOLINTEND(concurrency-mt-unsafe)
                if (WIFEXITED(system_ret)) {
                    if (WEXITSTATUS(system_ret) != 0) {
                        GG_LOGE(
                            "systemctl start failed for%.*s",
                            (int) bootstrap_service_file_path_vec.buf.len,
                            bootstrap_service_file_path_vec.buf.data
                        );
                        return ret;
                    }
                    GG_LOGI(
                        "systemctl start exited with child status %d\n",
                        WEXITSTATUS(system_ret)
                    );
                } else {
                    GG_LOGE(
                        "systemctl start did not exit normally for %.*s",
                        (int) bootstrap_service_file_path_vec.buf.len,
                        bootstrap_service_file_path_vec.buf.data
                    );
                    return ret;
                }
            }
        }
    }

    if (bootstrap_component_count > 0) {
        // save deployment state and restart
        GgError ret = save_deployment_info(deployment);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to save deployment state for bootstrap.");
            return ret;
        }

        GG_LOGI("Rebooting device for bootstrap.");
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        int system_ret = system("systemctl reboot");
        if (WIFEXITED(system_ret)) {
            if (WEXITSTATUS(system_ret) != 0) {
                GG_LOGE("systemctl reboot failed");
            }
            GG_LOGI(
                "systemctl reboot exited with child status %d\n",
                WEXITSTATUS(system_ret)
            );
        } else {
            GG_LOGE("systemctl reboot did not exit normally");
        }
    }

    return GG_ERR_OK;
}
