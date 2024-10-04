# TES Setup Doc

Using TES requires setting up a few policies and roles in the cloud first.

- Login to your aws account and go to IAM
- Within IAM click `Roles` from the left panel in the webpage
- Click `Create role` that's present on the top right page
- Select custom trust policy and replace the json with the following value

```
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": {
                "Service": [
                    "credentials.iot.amazonaws.com",
                ]
            },
            "Action": "sts:AssumeRole"
        }
    ]
}
```
