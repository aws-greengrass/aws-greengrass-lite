#! /usr/bin/env python3
# make sure awsiotsdk is installed
# python3 -m pip install awsiotsdk

# AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT=/home/ubuntu/aws-greengrass-lite/run/gg-ipc.socket  SVCUID=testcomponent configuration.py

import awsiot.greengrasscoreipc.clientv2 as clientv2
import datetime

client = clientv2.GreengrassCoreIPCClientV2()
key_path = ["foo", "bar"]
timestamp = datetime.datetime.now()
value_to_merge = {"baz": 43, "corge": True}
print("doing the update")
client.update_configuration(key_path=key_path,
                            timestamp=timestamp,
                            value_to_merge=value_to_merge)
print("update finished")

print("doing the get")
response = client.get_configuration(key_path=key_path,
                                    component_name="testcomponent")
print("get finished with component name: " + str(response.component_name) +
      " and value: " + str(response.value))

client.close()
print("test complete")
