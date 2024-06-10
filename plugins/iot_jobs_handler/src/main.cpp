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

        LOG.atInfo("Jobs-START-subscriptions")
            .log("Subscribing to Iot Jobs related Greengrass topics...");

        // TODO: unsubscribe and resubscribe if thing name changes
        // (subscriptions with old name need to be removed)

        SubscribeToDescribeJobExecutionAccepted();
        // SubscribeToDescribeJobExecutionRejected();
        SubscribeToJobExecutionsChangedEvents();
        PublishDescribeJobExecution();
    }

    // TODO: Wrap all update job execution subscripts and publishes into 1 method the DM can call
    // Call wrapper, subscribe to jobid updates for confirmations, publish jobid update, unsubscribe
    // to job id updates

    // https://github.com/aws-greengrass/aws-greengrass-nucleus/blob/b563193ed52d6abfcc76d65feeed3b2377cbe07c/src/main/java/com/aws/greengrass/deployment/IotJobsHelper.java#L381
    void IotJobsHandler::updateJobStatus(
        std::string jobId, std::string status, std::string details) {
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
        // TODO: make these info calls DEBUG level
        LOG.atInfo("Jobs-MQTT-publish").log("Publishing to describe job execution...");

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
            LOG.atError("Jobs-MQTT-publish-failed").log("Failed to publish to describe job topic.");
        } else {
            responseFuture.whenValid([](const ggapi::Future &completedFuture) {
                try {
                    auto response = ggapi::Struct(completedFuture.getValue());
                    if(response.get<int>(keys.errorCode) == 0) {
                        LOG.atInfo("Jobs-MQTT-publish-success")
                            .log("Successfully sent to get next job description.");
                    } else {
                        LOG.atError("Jobs-MQTT-publish-error")
                            .log("Error sending to get next job description.");
                    }
                } catch(const ggapi::GgApiError &error) {
                    LOG.atError("Jobs-MQTT-message-received-throw")
                        .cause(error)
                        .log("Failed to receive accepted deployment job execution description.");
                }
            });
        }
    }

    void IotJobsHandler::SubscribeToDescribeJobExecutionAccepted() {
        util::TempModule tempModule(getModule());
        // TODO: make these info calls DEBUG level
        LOG.atInfo("Jobs-MQTT-subscribe").log("Subscribing to deployment job execution update...");

        auto value = ggapi::Struct::create().put(
            {{keys.topicName,
              "$aws/things/" + _thingName + "/jobs/" + NEXT_JOB_LITERAL
                  + "/namespace-aws-gg-deployment/get/accepted"},
             {keys.qos, 1}});

        auto responseFuture =
            ggapi::Subscription::callTopicFirst(keys.subscribeToIoTCoreTopic, value);
        if(!responseFuture) {
            LOG.atError("Jobs-MQTT-subscribe-failed").log("Failed to subscribe.");
        } else {
            responseFuture.whenValid([&](const ggapi::Future &completedFuture) {
                try {
                    auto response = ggapi::Struct(completedFuture.getValue());
                    auto channel = response.get<ggapi::Channel>(keys.channel);
                    channel.addListenCallback(ggapi::ChannelListenCallback::of<ggapi::Struct>(
                        [&](const ggapi::Struct &packet) {
                            auto topic = packet.get<std::string>(keys.topicName);
                            auto payload = packet.get<std::string>(keys.payload);

                            // TODO: Retry request for next pending if unprocessed > 0

                            if(_unprocessedJobs.load() > 0) {
                                _unprocessedJobs.fetch_sub(1);
                            }
                            LOG.atInfo("Jobs-MQTT-message-received")
                                .log("Received Iot job description.");

                            std::cout << "PAYLOAD STRING 👏:: " << std::endl;
                            std::cout << payload << std::endl;

                            // TODO: convert jsonstring of payload to ggapi::struct
                            // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-https-api.html#jobs-mqtt-job-execution-data

                            // TODO: add to deployment queue -> send to deployment manager as
                            // ggapi::struct over LPC
                            // TODO: evaluate cancellation and cancel deployment if needed (maybe
                            // done in DM?, update sent here)
                        }));
                } catch(const ggapi::GgApiError &error) {
                    LOG.atError("Jobs-MQTT-message-received-throw")
                        .cause(error)
                        .log("Failed to receive accepted deployment job execution description.");
                }
            });
        }
    }

    void IotJobsHandler::SubscribeToDescribeJobExecutionRejected() {
        /*
        auto failureHandler = [&](Aws::Iotjobs::RejectedError *rejectedError, int ioErr) {
            if(ioErr) {
                std::cout << "Error " << ioErr << " occurred" << std::endl;
                return;
            }
            if(rejectedError) {
                std::cout << "Service Error " << (int) rejectedError->Code.value() << " occurred"
                          << std::endl;
                return;
            }
        };
        */
    }

    void IotJobsHandler::SubscribeToJobExecutionsChangedEvents() {
        util::TempModule tempModule(getModule());

        LOG.atInfo("Jobs-MQTT-subscribe")
            .log("Subscribing to deployment job event notifications...");
        auto value = ggapi::Struct::create().put(
            {{keys.topicName,
              "$aws/things/" + _thingName + "/jobs/notify-namespace-aws-gg-deployment"},
             {keys.qos, 1}});

        auto responseFuture =
            ggapi::Subscription::callTopicFirst(keys.subscribeToIoTCoreTopic, value);
        if(!responseFuture) {
            LOG.atError("Jobs-MQTT-subscribe-failed").log("Failed to subscribe.");
        } else {
            responseFuture.whenValid([&](const ggapi::Future &completedFuture) {
                try {
                    auto response = ggapi::Struct(completedFuture.getValue());
                    auto channel = response.get<ggapi::Channel>(keys.channel);
                    channel.addListenCallback(ggapi::ChannelListenCallback::of<ggapi::Struct>(
                        [&](const ggapi::Struct &packet) {
                            auto topic = packet.get<std::string>(keys.topicName);
                            auto payload = packet.get<std::string>(keys.payload);

                            LOG.atInfo("Jobs-MQTT-message-received")
                                .log("Received accepted deployment job execution description.");

                            std::cout << "PAYLOAD STRING 👏:: " << std::endl;
                            std::cout << payload << std::endl;

                            // TODO: convert jsonstring of payload to ggapi::object/struct
                            // https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-https-api.html#jobs-mqtt-job-execution-data

                            /*
                            if(!payload->Jobs.has_value() || payload->Jobs.value().empty()) {
                                std::cout << "Received empty jobs in notification." << std::endl;
                                _unprocessedJobs.store(0);
                                return;
                            }
                            auto jobs = payload->Jobs.value();
                            if(jobs.count(Aws::Iotjobs::JobStatus::QUEUED)) {
                                // We will get one notification per each new job QUEUED
                                auto jobVec = jobs.at(Aws::Iotjobs::JobStatus::QUEUED);
                                Aws::Crt::String jobId;
                                if(jobVec.size() > 0) {
                                    std::cout
                                        << "Received new jobs in notification. Getting first
                            jobId..."
                                        << std::endl;
                                    auto job = jobVec.front();
                                    if(job.JobId.has_value()) {
                                        jobId = job.JobId.value();
                                        std::cout << jobId << std::endl;
                                    }
                                }
                                _unprocessedJobs.fetch_add(1);
                                std::cout << "Received new deployment notification. Requesting
                            details."
                                          << std::endl;
                                // requestNextPendingJobDocument();
                                return;
                            }
                            std::cout << "Received other deployment notification. Not supported
                            yet."
                                      << std::endl;
                            */
                            _unprocessedJobs.fetch_add(1);
                            std::cout << "Received new deployment notification. Requesting details."
                                      << std::endl;
                            PublishDescribeJobExecution();
                        }));
                } catch(const ggapi::GgApiError &error) {
                    LOG.atError("Jobs-MQTT-message-received-throw")
                        .cause(error)
                        .log("Failed to receive accepted deployment job execution description.");
                }
            });
        }
    }

} // namespace iot_jobs_handler

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return iot_jobs_handler::IotJobsHandler::get().lifecycle(moduleHandle, phase, data);
}
