## 2024-02-22

- We have now added support for fetching thingName from environment variable.
- Example plugins are not installed by default now

### Known bug

- We now only support a debian linux environment for the project
- The local http server may not shut down properly and you might face the
  `mutex error` for that you need to find the process using the port. In linux
  environment you can use:

```
$ sudo ss -lptn 'sport = :8090'
$ kill <pid>
```

The first command tells you the process id as `("sh",pid=194410,fd=44))` and the
second one can kill the process.

## 2024-02-13

We have added support for TES LPC commands and the TES HTTP server. Components
started from local deployments which are using aws-device-sdk will be able to
use more AWS functionality such as S3 downloads.

Recipe parsing has been improved. Scripts are now passed to the shell which
parses and runs them. Recipes depending on shell features should now function.

Processes started for recipes are now managed by the gglite nucleus.

Recipe run keys are now case-insensitive.
