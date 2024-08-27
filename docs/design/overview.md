# Greengrass Lite Design Overview

Greengrass is designed from executable modules.  In unix land, greengrass is build from a number of daemons with each daemon providing specific functionality.  Daemons are intended to be small single purpose executables that enable a system to be composable by daemon selection.

## Daemon List

| Daemon           | Provided functionality                                                                                   |
|------------------|----------------------------------------------------------------------------------------------------------|
| gghealthd        | Collect GG daemon health information from the Platform and provide the information to the GG system      |
| ggconfigd        | Provide an interface to the configuration system that is accessable to all other GG components           |
| ggdeploymentd    | Executes a deployment that has been submitted to the deployment queue                                    |
| ggipcd           | Provides the legacy IPC interface to generic components and routes the IPC commands to suitable handlers |
| ggpubsubd        | Provides the local publish/subscribe interface between components                                        |
| iotcored         | Provides the MQTT interface to IoT Core                                                                  |
| gg-fleet-statusd | Produces status reports about all running GG components and sends the reports to IoT Core.               |

Please review the daemons specific documentation for more information on each daemon.
