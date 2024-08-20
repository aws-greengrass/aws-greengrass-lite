#! /usr/bin/env python3
# make sure awsiotsdk is installed
# python3 -m pip install awsiotsdk

# AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT=/home/ubuntu/aws-greengrass-lite/ipc_socket SVCUID=AAAAAAAAAA ipc_updateConfiguration.py

import awsiot.greengrasscoreipc.clientv2 as clientv2
import datetime

client = clientv2.GreengrassCoreIPCClientV2()
keyPath = ["foo", "bar"]
timeStamp = datetime.datetime.now()
valueToMerge = {"baz": 43, "corge": True}
print("doing the update")
client.update_configuration(key_path=keyPath,
                            timestamp=timeStamp,
                            value_to_merge=valueToMerge)
print("update finished")

client.close()
print("test complete")
