## 2024-08-23

- Please install fresh, or delete the configuration store config.db file (located at the ggconfigd service working directory). This is to avoid conflicts with configuration persisted by older versions

Features:

- ggconfigd and GetConfiguration supports reading nested configuration back in a single read call
- Fleet Provisioning will now manage its own new instance of iotcored

Bug Fixes:

- Fixed the bug to TES_server and now supports `http://` prefix in config

Known Issues:

- Fleet Provisioning does not terminate even after provisioning is complete

