#include "iot_jobs_handler.hpp"

#include <chrono>
#include <cpp_api.hpp>
#include <cstring>
#include <ctime>
#include <string>
#include <temp_module.hpp>

const static auto LOG = ggapi::Logger::of("IotJobsHandler");

namespace iot_jobs_handler {
    const IotJobsHandler::Keys IotJobsHandler::keys{};

    void IotJobsHandler::onStart(ggapi::Struct data) {
        _thingName = data.getValue<std::string>({"system", "thingName"});

        LOG.atDebug("jobs-handler-start-subscriptions")
            .log("Subscribing to Iot Jobs related Greengrass topics...");

        // TODO: unsubscribe and resubscribe if thing name changes
        // (subscriptions with old name need to be removed)
        try {
            SubscribeToDescribeJobExecutionAccepted();
            SubscribeToDescribeJobExecutionRejected();
            SubscribeToJobExecutionsChangedEvents();
            PublishDescribeJobExecution();
        } catch(const MqttException &e) {
            LOG.atError("jobs-handler-start-subscriptions")
                .kv("ErrorReason", e.what())
                .log("Failed to subscribe to Iot jobs related Greengrass topics");
        }
    }

    // TODO: Wrap all update job execution subscripts and publishes into 1 method the DM can call
    // Call wrapper, subscribe to jobid updates for confirmations, publish jobid update, unsubscribe
    // to job id updates

    // TODO: Deployment manager would publish to a topic created here which would trigger job status
    // updates.

    // https://github.com/aws-greengrass/aws-greengrass-nucleus/blob/b563193ed52d6abfcc76d65feeed3b2377cbe07c/src/main/java/com/aws/greengrass/deployment/IotJobsHelper.java#L381
    void IotJobsHandler::updateJobStatus(
        const std::string &jobId, const std::string &status, const std::string &details) {
        SubscribeToUpdateJobExecutionAccepted();
        SubscribeToUpdateJobExecutionRejected();
        PublishUpdateJobExecution();
        // unsubscribe
    }

    void IotJobsHandler::SubscribeToUpdateJobExecutionAccepted() {
    }

    void IotJobsHandler::SubscribeToUpdateJobExecutionRejected() {
    }

    void IotJobsHandler::PublishUpdateJobExecution() {
    }

    void IotJobsHandler::PublishDescribeJobExecution() {
        util::TempModule tempModule(getModule());
        LOG.atDebug("jobs-handler-mqtt-publish").log("Publishing to describe job execution...");

        if(_thingName.empty()) {
            throw MqttException("DescribeJobExecutionRequest must have a non-null thingName");
        }

        std::string json;
        auto buf = ggapi::Struct::create()
                       .put(
                           {{"jobId", NEXT_JOB_LITERAL},
                            {"thingName", _thingName},
                            {"includeJobDocument", true}})
                       .toJson();
        json.resize(buf.size());
        buf.get(0, json);

        auto value = ggapi::Struct::create().put(
            {{keys.topicName,
              "$aws/things/" + _thingName + "/jobs/" + NEXT_JOB_LITERAL
                  + "/namespace-aws-gg-deployment/get"},
             {keys.qos, 1},
             {keys.payload, json}});

        auto responseFuture =
            ggapi::Subscription::callTopicFirst(keys.publishToIoTCoreTopic, value);
        if(!responseFuture) {
            LOG.atError("jobs-handler-mqtt-publish")
                .log("Failed to publish to describe job topic.");
        } else {
            responseFuture.whenValid([](const ggapi::Future &completedFuture) {
                try {
                    auto response = ggapi::Struct(completedFuture.getValue());
                    if(response.get<int>(keys.errorCode) == 0) {
                        LOG.atInfo("jobs-handler-mqtt-publish")
                            .log("Successfully sent to get next job description.");
                    } else {
                        LOG.atError("jobs-handler-mqtt-publish")
                            .log("Error sending to get next job description.");
                    }
                    LOG.atDebug("jobs-handler-mqtt-publish").log("Requesting the next deployment");
                } catch(const ggapi::GgApiError &error) {
                    LOG.atError("jobs-handler-mqtt-message-received")
                        .cause(error)
                        .log("Failed to receive accepted deployment job execution description.");
                }
            });
        }
    }

    void IotJobsHandler::SubscribeToDescribeJobExecutionAccepted() {
        util::TempModule tempModule(getModule());
        LOG.atDebug("jobs-handler-mqtt-subscribe")
            .log("Subscribing to deployment job execution update...");

        if(_thingName.empty()) {
            throw MqttException(
                "DescribeJobExecutionSubscriptionRequest must have a non-null thingName");
        }

        auto value = ggapi::Struct::create().put(
            {{keys.topicName,
              "$aws/things/" + _thingName + "/jobs/" + NEXT_JOB_LITERAL
                  + "/namespace-aws-gg-deployment/get/accepted"},
             {keys.qos, 1}});

        auto responseFuture =
            ggapi::Subscription::callTopicFirst(keys.subscribeToIoTCoreTopic, value);
        if(!responseFuture) {
            LOG.atError("jobs-handler-mqtt-subscribe").log("Failed to subscribe.");
        } else {
            responseFuture.whenValid([&](const ggapi::Future &completedFuture) {
                try {
                    auto response = ggapi::Struct(completedFuture.getValue());
                    auto channel = response.get<ggapi::Channel>(keys.channel);
                    channel.addListenCallback(ggapi::ChannelListenCallback::of<ggapi::Struct>(
                        [&](const ggapi::Struct &packet) {
                            auto topic = packet.get<std::string>(keys.topicName);
                            auto payloadStr = packet.get<std::string>(keys.payload);

                            auto payloadStruct = jsonToStruct(payloadStr);

                            auto execution = payloadStruct.get<ggapi::Struct>("execution");
                            if(execution.empty()) {
                                LOG.atInfo("jobs-handler-mqtt-message-received")
                                    .log("No deployment job found");
                                if(_unprocessedJobs.load() > 0) {
                                    LOG.atDebug("jobs-handler-mqtt-message-received")
                                        .log("Retry requesting next pending job document");
                                    PublishDescribeJobExecution();
                                }
                                return;
                            }

                            LOG.atInfo("jobs-handler-mqtt-message-received")
                                .log("Received accepted Iot job description.");

                            if(_unprocessedJobs.load() > 0) {
                                _unprocessedJobs.fetch_sub(1);
                            }

                            if(createAndSendDeployment(execution)) {
                                auto jobId = execution.get<std::string>("jobId");
                                LOG.atInfo("jobs-handler-mqtt-message-received")
                                    .kv("JobId", jobId)
                                    .log("Added the job to the queue");
                            }
                        }));
                } catch(const ggapi::GgApiError &error) {
                    LOG.atError("jobs-handler-mqtt-message-received-throw")
                        .cause(error)
                        .log("Failed to receive accepted deployment job execution description.");
                }
            });
        }
    }

    void IotJobsHandler::SubscribeToDescribeJobExecutionRejected() {
        util::TempModule tempModule(getModule());
        LOG.atDebug("jobs-handler-mqtt-subscribe")
            .log("Subscribing to deployment job execution update...");

        if(_thingName.empty()) {
            throw MqttException(
                "DescribeJobExecutionSubscriptionRequest must have a non-null thingName");
        }

        auto value = ggapi::Struct::create().put(
            {{keys.topicName,
              "$aws/things/" + _thingName + "/jobs/" + NEXT_JOB_LITERAL
                  + "/namespace-aws-gg-deployment/get/rejected"},
             {keys.qos, 1}});

        auto responseFuture =
            ggapi::Subscription::callTopicFirst(keys.subscribeToIoTCoreTopic, value);
        if(!responseFuture) {
            LOG.atError("jobs-handler-mqtt-subscribe-failed").log("Failed to subscribe.");
        } else {
            responseFuture.whenValid([&](const ggapi::Future &completedFuture) {
                try {
                    auto response = ggapi::Struct(completedFuture.getValue());
                    auto channel = response.get<ggapi::Channel>(keys.channel);
                    channel.addListenCallback(ggapi::ChannelListenCallback::of<ggapi::Struct>(
                        [&](const ggapi::Struct &packet) {
                            auto payloadStr = packet.get<std::string>(keys.payload);
                            LOG.atError("obs-handler-mqtt-message-received")
                                .kv("payload", payloadStr)
                                .log("Job subscription got rejected");
                        }));
                } catch(const ggapi::GgApiError &error) {
                    LOG.atError("jobs-handler-mqtt-message-received-throw")
                        .cause(error)
                        .log("Failed to receive rejected deployment job execution description.");
                }
            });
        }
    }

    void IotJobsHandler::SubscribeToJobExecutionsChangedEvents() {
        util::TempModule tempModule(getModule());

        LOG.atDebug("jobs-handler-mqtt-subscribe")
            .log("Subscribing to deployment job event notifications...");

        if(_thingName.empty()) {
            throw MqttException(
                "JobExecutionsChangedSubscriptionRequest must have a non-null thingName");
        }

        auto value = ggapi::Struct::create().put(
            {{keys.topicName,
              "$aws/things/" + _thingName + "/jobs/notify-namespace-aws-gg-deployment"},
             {keys.qos, 1}});

        auto responseFuture =
            ggapi::Subscription::callTopicFirst(keys.subscribeToIoTCoreTopic, value);
        if(!responseFuture) {
            LOG.atError("jobs-handler-mqtt-subscribe-failed").log("Failed to subscribe.");
        } else {
            responseFuture.whenValid([&](const ggapi::Future &completedFuture) {
                try {
                    auto response = ggapi::Struct(completedFuture.getValue());
                    auto channel = response.get<ggapi::Channel>(keys.channel);
                    channel.addListenCallback(ggapi::ChannelListenCallback::of<
                                              ggapi::Struct>([&](const ggapi::Struct &packet) {
                        auto topic = packet.get<std::string>(keys.topicName);
                        auto payloadStr = packet.get<std::string>(keys.payload);

                        _unprocessedJobs.fetch_add(1);

                        LOG.atInfo("jobs-handler-mqtt-message-received")
                            .log("Received accepted deployment job execution description.");

                        auto payloadStruct = jsonToStruct(payloadStr);

                        auto jobs = payloadStruct.get<ggapi::Struct>("jobs");
                        if(jobs.empty()) {
                            LOG.atInfo("jobs-handler-mqtt-message-received")
                                .log("Received empty jobs in notification ");
                            _unprocessedJobs.store(0);
                            // TODO: evaluate cancellation and cancel deployment if needed
                            return;
                        }

                        auto v = jobs.keys().toVector<std::string>();

                        if(std::find(v.begin(), v.end(), "QUEUED") != v.end()) {
                            _unprocessedJobs.fetch_add(1);
                            LOG.atInfo("jobs-handler-mqtt-message-received")
                                .log("Received new deployment notification. Requesting details.");
                            PublishDescribeJobExecution();
                        }
                        LOG.atInfo("jobs-handler-mqtt-message-received")
                            .log("Received other deployment notification. Not supported yet");
                    }));
                } catch(const ggapi::GgApiError &error) {
                    LOG.atError("jobs-handler-mqtt-message-received-throw")
                        .cause(error)
                        .log("Failed to receive accepted deployment job execution description.");
                }
            });
        }
    }

    bool IotJobsHandler::createAndSendDeployment(const ggapi::Struct &deploymentExecutionData) {

        auto jobDocument = deploymentExecutionData.get<ggapi::Struct>("jobDocument");
        if(jobDocument.empty()) {
            LOG.atError("jobs-handler-create-deployment").log("Job document is empty");
            return false;
        }
        auto deploymentId = jobDocument.get<std::string>("deploymentId");
        auto jobDocumentJson = jobDocument.toJson();
        auto jobDeploymentVec =
            jobDocumentJson.get<std::vector<uint8_t>>(0, jobDocumentJson.size());
        auto jobDocumentString = std::string{jobDeploymentVec.begin(), jobDeploymentVec.end()};

        auto deployment = ggapi::Struct::create();
        deployment.put("deploymentDocumentobj", jobDocument);
        deployment.put("deploymentDocument", jobDocumentString);
        deployment.put("deploymentType", "IOT_JOBS");
        deployment.put("id", deploymentId);
        deployment.put("isCancelled", false);
        deployment.put("deploymentStage", "DEFAULT");
        deployment.put("stageDetails", 0);
        deployment.put("errorStack", 0);
        deployment.put("errorTypes", 0);

        auto resultFuture =
            ggapi::Subscription::callTopicFirst(keys.CREATE_DEPLOYMENT_TOPIC_NAME, deployment);
        if(!resultFuture) {
            return false;
        }
        ggapi::Struct result{resultFuture.waitAndGetValue()};
        if(!result.getValue<bool>({"status"})) {
            LOG.atError("jobs-handler-create-deployment").log("Deployment failed");
        }
        return result.getValue<bool>({"status"});
    }

    ggapi::Struct IotJobsHandler::jsonToStruct(std::string json) {
        auto container = ggapi::Buffer::create().insert(-1, util::Span{json}).fromJson();
        return ggapi::Struct{container};
    };
} // namespace iot_jobs_handler

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return iot_jobs_handler::IotJobsHandler::get().lifecycle(moduleHandle, phase, data);
}
