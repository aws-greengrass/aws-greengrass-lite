import boto3


def fetch_s3_bucket_list():
    # Create an S3 client
    s3 = boto3.client('s3')
    # List S3 buckets
    response = s3.list_buckets()
    # Print the names of all buckets
    print("S3 Bucket Names:")
    for bucket in response['Buckets']:
        print(bucket['Name'])


def main():
    print("HELLO WORLD")
    fetch_s3_bucket_list()


if __name__ == "__main__":
    main()
