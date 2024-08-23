## 2024-08-23

- Please install fresh, or delete the configuration store config.db file
  (located at the ggconfigd service working directory). This is to avoid
  conflicts with configuration persisted by older versions

Features:

- ggconfigd and GetConfiguration supports reading nested configuration back in a
  single read call
- Fleet Provisioning will now manage its own new instance of iotcored

Bug Fixes:

- TES_server will now support `http://` prefix in config

Known Issues:

- Fleet Provisioning does not terminate even after provisioning is complete
- iotcored may have TLS issues. Let us know if you experience problems
