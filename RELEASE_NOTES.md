## 2024-02-13

We have added support for TES LPC commands and the TES HTTP server. Components
started from local deployments which are using aws-device-sdk will be able to
use more AWS functionality such as S3 downloads.

Recipe parsing has been improved. Scripts are now passed to the shell which
parses and runs them. Recipes depending on shell features should now function.

Processes started for recipes are now managed by the gglite nucleus.

Recipe run keys are now case-insensitive.
