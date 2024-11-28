# Greengrass Nucleus Lite Design Overview

Greengrass Nucleus Lite is designed as a group of daemons with each daemon
providing specific functionality. Daemons are intended to be small single
purpose executables that enable a system to be composable by daemon selection.

## Daemon List

GG Daemons are small programs that run to provide a specific functionality to
greengrass.

| Daemon           | Provided functionality                                                                                   |
| ---------------- | -------------------------------------------------------------------------------------------------------- |
| gghealthd        | Collect GG daemon health information from the Platform and provide the information to the GG system      |
| ggconfigd        | Provide an interface to the configuration system that is accessable to all other GG components           |
| ggdeploymentd    | Executes a deployment that has been submitted to the deployment queue                                    |
| ggipcd           | Provides the legacy IPC interface to generic components and routes the IPC commands to suitable handlers |
| ggpubsubd        | Provides the local publish/subscribe interface between components                                        |
| iotcored         | Provides the MQTT interface to IoT Core                                                                  |
| gg-fleet-statusd | Produces status reports about all running GG components and sends the reports to IoT Core.               |

Please review the daemons specific documentation for more information on each
daemon.

## Libraries List

GG libraries are functions that get linked with daemons to provide the common
behaviors needed by all GG daemons. One example is interfacing the the GG
coreBus.

| Library  | Provided functionality                                                                              |
| -------- | --------------------------------------------------------------------------------------------------- |
| ggl-lib  | General datatypes and the corebus interface for communications between GGL components               |
| ggl-json | A basic JSON interface for conversion to/from JSON & corebus datatypes                              |
| ggl-exec | A Linux interface library to simplify starting/stopping/killing processes around the EXEC function. |
| ggl-http | A library to use HTTP to fetch a token and download a file. Suitable for S3 downloads.              |
| ggl-yaml | A basic YAML interface for conversion to/from YAML and corebus datatypes                            |
| ggl-file | A library for basic file operations needed for component installation and deployment operations.    |
