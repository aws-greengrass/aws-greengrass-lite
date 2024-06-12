#pragma once

#include <logging.hpp>
#include <plugin.hpp>

namespace iot_jobs_handler {
    class MqttException : public ggapi::GgApiError {
    public:
        explicit MqttException(const std::string &str) : ggapi::GgApiError("MqttException", str) {
        }
    };
    class IotJobsHandler : public ggapi::Plugin {
        struct Keys {
            ggapi::StringOrd topicName{"topicName"};
            ggapi::Symbol qos{"qos"};
            ggapi::Symbol payload{"payload"};
            ggapi::Symbol channel{"channel"};
            ggapi::Symbol errorCode{"errorCode"};
            ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
            ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
            ggapi::Symbol CREATE_DEPLOYMENT_TOPIC_NAME{"aws.greengrass.deployment.Offer"};
        };

        const std::string NEXT_JOB_LITERAL = "$next";

    private:
        std::atomic<int> _unprocessedJobs = 0;
        std::string _thingName;
        static const Keys keys;

        static ggapi::Struct jsonToStruct(std::string json);

    public:
        void updateJobStatus(
            const std::string &jobId, const std::string &status, const std::string &details);
        static bool createAndSendDeployment(const ggapi::Struct &deploymentExecutionData);
        void PublishUpdateJobExecution();
        void PublishDescribeJobExecution();
        void SubscribeToUpdateJobExecutionAccepted();
        void SubscribeToUpdateJobExecutionRejected();
        void SubscribeToDescribeJobExecutionAccepted();
        void SubscribeToDescribeJobExecutionRejected();
        void SubscribeToJobExecutionsChangedEvents();

        void onStart(ggapi::Struct data) override;

        static IotJobsHandler &get() {
            static IotJobsHandler instance{};
            return instance;
        }
    };
} // namespace iot_jobs_handler
