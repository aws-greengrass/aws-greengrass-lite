## Log Manager

The log manager plugin uploads logs from the Greengrass core device to the
Amazon CloudWatch Logs service. Currently, it attempts to upload the
greengrass.log file every 5 minutes, under the
/aws/greengrass/GreengrassSystemComponent/us-west-2/System log group with a log
stream corresponding to each upload. Log manager will create the log group and
log streams in addition to uploading log events from the log file.

## Requirements

Log manager is available as a plugin for Greengrass Lite. It has hard
dependencies on the cloud_downloader and iot_broker plugins which must complete
their onStart lifecycle process before log manager can be started. This is to
ensure that TES is running on the device and can provide credentials to log
manager.

Because of the hard dependency on TES, the provided nucleus config should
include the correct paths to the private key and certificates, as well as the
correct iotCredEndpoint. Log manager currently does not rely on any other
provided configuration values and simply inherits any requirements from TES. The
IoT policy associated with the IoT Thing certificate should include the
`iot:AssumeRoleWithCertificate` permission so that TES is able to request
credentials from the IoT credential endpoint.

The device service role (typically `GreengrassV2TokenExchangeRole`) associated
with the Greengrass device should have sufficient permissions to CloudWatch
Logs: `logs:CreateLogGroup`, `logs:CreateLogStream`, and `logs:PutLogEvents` are
currently required. Future iterations of log manager may also require the
`logs:DescribeLogStreams` action.

Log manager must be able to perform outbound requests to the
`logs.region.amazonaws.com` endpoint.

## Running

Log manager runs on Greengrass Lite startup as a plugin and will begin its
lifecycle after its two dependencies have finished starting. To verify that log
manager is successfully started, the `local.plugins.discovered.log_manager.log`
component log file on the Greengrass device can be checked for relevant logging.
A successful run should expect to see logging messages at the INFO level for
signing the HTTP requests and detailed HTTP request info at the DEBUG level for
all of three HTTP requests per log upload attempt. A 200 response code indicates
a successful HTTP request and a 400 response code may not necessarily be a
concern.

## Possible Issues

The following issues may occur:

1. `Could not retrieve credentials from TES. Skipping this upload attempt.` -
   Log manager was not able to get TES credentials. Double check TES related
   configuration and that TES has properly started up.
2. CreateLogGroup HTTP request returns 400 error with
   `ResourceAlreadyExistsException` - This is a common but non-issue that may be
   seen if the log group already exists.
3. PutLogEvents HTTP request returns 400 error - Double check the exact error
   with DEBUG level logs. Currently, there is a known gap in device side
   validation on outgoing log events, and the CloudWatch Logs API may reject the
   upload attempt. This will be fixed in the future.
