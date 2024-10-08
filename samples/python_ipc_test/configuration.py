#! /usr/bin/env python3
# make sure awsiotsdk is installed
# python3 -m pip install awsiotsdk

# AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT=/home/ubuntu/aws-greengrass-lite/run/gg-ipc.socket  SVCUID=testcomponent configuration.py

import awsiot.greengrasscoreipc.clientv2 as clientv2
import datetime

client = clientv2.GreengrassCoreIPCClientV2()
key_path = ["foo", "bar"]
print("doing an update")
client.update_configuration(key_path=key_path,
                            timestamp=datetime.datetime.now(),
                            value_to_merge={
                                "baz": 43,
                                "corge": True
                            })
print("update finished")

print("doing a get")
response = client.get_configuration(key_path=key_path,
                                    component_name="testcomponent")
print("get finished with component name: " + str(response.component_name) +
      " and value: " + str(response.value))

print("subscribing to all configuration updates for this component")
client.subscribe_to_configuration_update(
    on_stream_event=lambda response: print(
        "(subscripton for any updates on this) got update: " + str(response)),
    on_stream_error=lambda e: print(
        "(subscripton for any updates on this) got error: " + str(e)),
)

print("subscribing to configuration updates for foo/bar/baz in testcomponent")
client.subscribe_to_configuration_update(
    key_path=["foo", "bar", "baz"],
    component_name="testcomponent",
    on_stream_event=lambda response: print(
        "(subscripton for updates to foo/bar/baz in testcomponent) got update: "
        + str(response)),
    on_stream_error=lambda e: print(
        "(subscripton for updates to foo/bar/baz in testcomponent) got error: "
        + str(e)),
)

print("doing an update")
client.update_configuration(key_path=key_path,
                            timestamp=datetime.datetime.now(),
                            value_to_merge={
                                "baz": 44,
                                "corge": False
                            })
print("update finished")

print("doing a get")
response = client.get_configuration(key_path=key_path,
                                    component_name="testcomponent")
print("get finished with component name: " + str(response.component_name) +
      " and value: " + str(response.value))

client.close()
print("test complete")
