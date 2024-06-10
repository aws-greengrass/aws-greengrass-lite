#pragma once

#include <logging.hpp>
#include <plugin.hpp>

namespace iot_jobs_handler {
    class IotJobsHandler : public ggapi::Plugin {
        struct Keys {
            ggapi::StringOrd topicName{"topicName"};
            ggapi::Symbol qos{"qos"};
            ggapi::Symbol payload{"payload"};
            ggapi::Symbol channel{"channel"};
            ggapi::Symbol errorCode{"errorCode"};
            ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
            ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
        };
        const std::string UPDATE_JOB_TOPIC =
            "$aws/things/%s/jobs/%s/namespace-aws-gg-deployment/update";
        const std::string JOB_UPDATE_ACCEPTED_TOPIC =
            "$aws/things/%s/jobs/%s/namespace-aws-gg-deployment/update/accepted";
        const std::string JOB_UPDATE_REJECTED_TOPIC =
            "$aws/things/%s/jobs/%s/namespace-aws-gg-deployment/update/rejected";
        const std::string DESCRIBE_JOB_TOPIC =
            "$aws/things/%s/jobs/%s/namespace-aws-gg-deployment/get";
        const std::string JOB_DESCRIBE_REJECTED_TOPIC =
            "$aws/things/%s/jobs/%s/namespace-aws-gg-deployment/get/rejected";
        const std::string JOB_EXECUTIONS_CHANGED_TOPIC =
            "$aws/things/%s/jobs/notify-namespace-aws-gg-deployment";
        const std::string NEXT_JOB_LITERAL = "$next";

    private:
        std::atomic<int> _unprocessedJobs = 0;
        std::string _thingName;
        static const Keys keys;

    public:
        void updateJobStatus(std::string jobId, std::string status, std::string details);
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