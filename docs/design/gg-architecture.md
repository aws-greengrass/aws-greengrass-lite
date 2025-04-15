# Greengrass Lite Architecture

## Introduction and Goals

Greengrass Lite is a size constrained version of Greengrass v2. The intent is to
reduce the memory footprint below 10MB with smaller being better. The potential
applications increase dramatically as size reduces so there is no maximum
“acceptable” size defined.

## Requirements Overview

The most important customer requirements are listed below:

1. Reduce size below 30/15MB RAM/FLASH, Much lower would be better
2. Support unchanged, “generic components" and their recipes.
3. Support embedded Linux first but do not prevent other operating systems.
4. Share the MQTT connection and “things” with all GG elements
5. Provide OTA and deployment from GG console
6. forward logs to AWS Cloudwatch
7. Ensure proper startup/shutdown of all components
8. Provide tokens from the AWS Token Exchange Service

### Reduce Size

Target a small size to allow integration into IoT gateways, home routers, and
virtual machines. A scaleable greengrass lite system that can be “right-sized”
to a customer application will open many opportunities. Every MB used by
Greengrass reduces the memory available to the target application.

### Support Generic Components

Generic Components are the external applications typically written by the
customer and deployed via the Greengrass console. These components can be
developed in any language and are often deployed as binaries specific to the
target platform. Generic components communicate with the Greengrass system via
the IPC. Support for generic components requires support for the legacy IPC
system.

### Platform Support

The immediate customer need for Greengrass lite is to run on embedded Linux
systems. However it is anticipated that non-linux systems may be targeted. The
architecture will need to isolate platform issues to simplify adding additional
platforms. It is acceptable to have multiple versions of platform specific
services. Different platforms are referred to as hosts in this document.

### Shared MQTT

AWS IoT Core connections requires that all connected things have a unique
identity. Two applications running on the same device but seperately connecting
to AWS Iot Core will require unique identities have a limited number of topic
subscriptions (50). Greengrass devices track the number of subscriptions and
open additional connections as required. Tracking subscriptions and managing
connections requires that all connections go through a central service
(Greengrass).

### OTA & Deployments

A key function of Greengrass is the managed method of updating the software at
the edge. Deployments allow new software and any dependencies to be added to a
device while OTA allows new software and updates to be delivered from the cloud
connection.

### SSH Access

SSH Access allows a console user to get a direct SSH connection to the console
of a remote Greengrass device. This connection is very useful for more difficult
debugging sessions of remote systems. This function assumes the host OS is some
form of Unix.

### Cloudwatch forwarding

Cloudwatch forwarding is the task of forwarding the host logs to cloudwatch for
review. It is important to include the entire host logging system in this
forwarding to be sure all health/debugging information is captured.

### Orchestration

Orchestration is the task of starting the system services in the correct order,
monitoring the health and shutting down the services as required.

### Token Exchange Service

The Token Exchange Service (TES) interacts with the AWS Token Exchange Service
and provides the tokens via an HTTP proxy to the local system.

### Quality Goals

The quality of the system will be measured against these goals:

| metric            | qualitative evaluator                                       |
| ----------------- | ----------------------------------------------------------- |
| Size              | Smaller is better                                           |
| Reliability       | long up-times. Longer is better                             |
| Compatibility     | Works with existing generic components                      |
| On-boarding speed | Fast on-boarding for new developers (internal and external) |
| Low CPU load      | Smaller is better.                                          |

## Architecture Constraints

### Single Responsibility Design

The system must be designed so components are independent processes. This will
ensure the following characteristics:

1. Faults in one component cannot leak into other components.
2. Data transfer between components is explicit and performed by copy.
3. Features are added by adding independent components
4. Components are isolatable, simplifying some security concerns.

### Process Orchestration

Orchestration is the process of starting/stopping processes in a coordinated and
deliberate fashion so the system starts-up/shutdown correctly. Many
orchestration systems also have provisions for restarting and recovering from
failures.

Process orchestration is performed by the host for all anticipated Greengrass
systems. Where possible, Greengrass will use the host orchestrator by adapting
the Greengrass recipe file to a suitable format for the host system. This
adaptation could happen in the cloud or in the device. This constraint does not
eliminate the possibility of a custom orchestrator. This constraint expresses a
preference to use the host native orchestrator.

### Host Logging

All hosts have a logging system available to all applications. This logging
system handles log rotation, log viewing and other log related activities in a
host specific manner. Leveraging the host specific features allows the customer
to use what they are already familiar with.

NOTE: This assumes the customer is familiar with the host level logging system
and chose the host OS for specific features. Greengrass is not a complete
operating system so there will always be a tradeoff with user knowledge of
greengrass and user knowledge of the host. My assumption is the host log
analysis tools are very rich for that host and the cloudwatch tools are also
very rich. The greengrass features cover configuration of log coverage but not
of log analysis/processing other than forwarding them to the cloud and rotating
them on the device.

## System Scope and Context

### Business Context

Business users of the Greengrass system. Consider these users as features are
designed.

![Business Context](./gg-architecture-drawings/business%20context.png)

### Technical Context

Greengrass consists of core components (the nucleus) and some number of generic
components. Greengrass is customized by adding/removing core components to
provide the system functions. Generic components are typically developed by
customers to provide the business logic for a greengrass edge device. Greengrass
is responsible for ensuring all components startup, providing system status to
the cloud and maintaining the core services required by the generic components.

![Technical Context](./gg-architecture-drawings/technical%20context.png)

### Solution Strategy

The strategy proposed for Greengrass light is to separate each of the Greengrass
functions into small independent processes. These processes will communicate
over an internal Greengrass databus. The databus implementation is isolated into
a shared library that is dynamically linked at runtime to ensure all components
are operating with the same version of the databus. Where possible the host
environment features will be used to implement basic behavior like
orchestration, logging, rollback and permissions.

### Building Block View

## Open-Box Overall System

![building block view](./gg-architecture-drawings/building%20block%20view.png)

### Motivation

| Tenet                 | Definition                                                                                          | Rational                                                                                                                                                        |
| --------------------- | --------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Compatibility         | Existing generic components must communicate via IPC                                                | Add a single IPC interface component that bridges the IPC to the internal databus.                                                                              |
| Single responsibility | Keep components as separate processes and focused on single tasks                                   | Keeping the components in separate processes forces the components to be fully independent.                                                                     |
| Use host services     | Reduce complexity by integrating with host provided services for process orchestration and logging. | Reduce customer integration training as they are already familiar with the host capabilities. There is no need to learn new Greengrass versions of the systems. |
| Size & Compatibility  | the databus and logging interfaces must be common across all components                             | Sharing a library for the host specific elements ensures all components in a system use the same mechanisms.                                                    |

### Contained Building Blocks

The Greengrass core is comprised of components that provide the required
functionality for a specific system. The key core components include:

1. Greengrass Core Databus shared library
2. IoT Core Interface (MQTT)
3. Orchestration Host Adapter
4. Log Forwarding
5. Status and Telemetry
6. Deployment Interface
7. Remote SSH
8. Token Exchange Service (TES)

These components connect to each other through the databus and provide function
specific interfaces. These blocks will be discussed in detail below.

#### Important Interfaces

The databus interface, logging interface and configuration interface are used by
all the core components and connect the Greengrass core component to the
underlying host systems. The recipe is the interface to orchestration and it is
critical to launching a Greengrass system.

##### Greengrass databus (coreBus)

The Greengrass databus is used to connect the core components directly together.
This interface can also be used by customer developed components if they don’t
need the compatibility provided by the Greengrass IPC interface. The Greengrass
databus can be implemented in a variety of ways but the usage model for the bus
is a 1-1 connection between two components. While the model is a 1-1 connection,
the actual implementation is up to the host interface library.

![Greengrass Databus](./gg-architecture-drawings/interface%20options.png)

#### Databus Interface

##### Access Functions

The databus is accessed with the following 4 API functions.

| <!-- --> | <!-- -->                                                                      |
| -------- | ----------------------------------------------------------------------------- |
| connect  | Open a connection to the named service                                        |
| close    | Close the specified connection                                                |
| call     | Make a blocking RPC call on the specified connection. Return with the result. |
| notify   | Make an RPC notification. No response. (equivalent to QoS 0 publish)          |

Service names are converted to appropriate addressing of the destination databus
channel by the databus interface library. The details of how the addressing
works is an implementation detail but one example of a point-to-point topology
could work by mapping each service to the path for a named pipe. Connecting to
the service would open a connection on the pipe. This mechanism would allow a
fully connected network topology which minimizes the demands on the databus
bandwidth.

#### Databus subscriptions

There are some operations that require long running commands where data is
received asynchronously. There are three basic strategies for these commands and
it is up to the API designer for a component to decide which is best.

1. Long running command. The calling component makes a call for the data and
   handles data as it arrives... forever or until the connection is closed.
2. Polling. The calling component make a call to start the data. The data is
   cached by the receiving component. Later the calling component makes a new
   call to retrieve any data that is available. This mechanism could have longer
   latency.
3. Reverse data channel. The calling component makes a call for the data and
   provides the name of a reverse data channel. The receiving component connects
   to the reverse data channel and sends the data as it arrives.

Data types

The databus interface normalizes the data sent by providing a limited number of
simple datatypes.

| Data type               | Description                                                          |
| ----------------------- | -------------------------------------------------------------------- |
| boolean                 |                                                                      |
| 64 bit signed integer   |                                                                      |
| 64 bit floating point   |                                                                      |
| buffer                  | array of 8-bit values (bytes) (this includes strings)                |
| map                     | A set of key-value pairs.                                            |
| list                    | A sequence of identically typed items. could be maps or other lists. |
| 64-bit unsigned integer | (Consider this, but hopefully it is not required)                    |

These data types are provided to simplify mapping of data into a host specific
serialization format.

#### Serialization

The serialization mechanism is not specified by the architecture but it must be
consistent within a host implementation. The data types were chosen to be simple
to map to popular serializers such as messagepack or JSON.

#### Communications Path

The communications path should be chosen from the forms of IPC (not to be
confused with the Greengrass IPC) available to the host operating system. Some
options are:

1. Unix Domain Sockets
2. Named Pipes
3. Queues
4. FIFO’s
5. COM
6. DDE
7. WM_COPYDATA
8. File Maps
9. UDP/TCP to a local port
10. Binders
11. Bundles

This list is not exclusive and some options are specific to specific operating
systems. The databus design is not dependent upon the choice of IPC so long as
the performance requirements are met. Validate the databus choices with
benchmarks to ensure the system performance is sufficient.

### Quality Characteristics for the databus

| <!-- -->     | <!-- -->                                                                                                                                                                                                                                                                    |
| ------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Speed        | GG IPC is expected to require ~100msg/sec. 1 IPC may require a few (10?) databus messages which puts a lower bound for the databus at 1000 msg/sec.                                                                                                                         |
| Simplicity   | The serialization is NOT specified in the architecture so choose something simple for your host. One suggestion is messagepack because libraries are readily available in many languages and the final data is small which helps speed.                                     |
| Topology     | The physical topology is not specified. Choose something simple and fast. There are additional speed consequences of some topologies. The star configuration will reduce the speed of the databus by ~50% so make sure there is sufficient performance to handle that load. |
| Debugability | The star topology is slower but has advantages for bus monitoring & debugging.                                                                                                                                                                                              |

### Error Handling

Components will generate and receive errors as conditions require it. When a
component encounters an error, it must log all the known data before returning a
basic error number upstream. Consider the following:

1. Component A calls component B to perform some task.
2. component B fails a the task (The internet is down)
3. component B returns ERROR_RETRY (or similar)
4. component B logs details like the internet being down, the endpoint being
   connected, any host os errors, etc..
5. component A logs that it received ERROR_RETRY and what steps it will take
6. component A waits a bit and retries.

No error data is lost, but importantly, component A only receives an error that
informs about the recovery options. Component A lacks context to fully evaluate
all the available options and must quickly pick one. Additional information is
for the engineer and not for the program. This information is for the log file
and must be logged by the entity with the most complete knowledge (component B)

### Some data

A databus implementation using Unix Domain Sockets + messagepack in a
point-to-point topology has been tested at ~100k messages/second. On the same
system, the GG IPC performance also achieved ~100k messages/second. This system
would be considered acceptable since the GG IPC performance requirement is ~100
messages/second.

### Logging

Logging is intended to use the host logging services. In a unix system, it is
expected to simply use stdout/stderr and rely upon the underlying orchestration
system to direct these channels to the system logs. Using stdout/stderr (where
possible) allows components to be run by the user and the messages are reported
to the user in the terminal. While running the same component through the
orchestrator will send the logs to syslog and by virtue of the log forwarding
components, the messages will appear in cloudwatch. The logging is handled
through a simplistic logging interface which allows different hosts to map the
log messages to their logging services in the most appropriate manner. For
example, windows system events are not an exact match to syslog so it will be
necessary for a windows specific logging library to be used. The logging
interface is expected to be statically linked to the component. This is
acceptable because it is not necessary that all components share the same
logging interface so long as they have the same logging destination. For example
a customer may choose to develop a windows specific component and directly use
the system events while another customer may want a more generic component and
use an interface to allow different hosts to be supported by linking to
different libraries. Two components on the same host may produce log messages
with unique formatting and as long as the messages appear in the host logging
system, they customer will be successful in debugging.

> NOTE: The log forwarder will be host specific as it captures the host logs and
> forwards them to cloudwatch. The forwarding strategy is critical as it allows
> 100% of the system logs to appear in cloudwatch so long as there is a cloud
> connection.

> NOTE: Greengrass has log configuration. This configuration must be translated
> to modify the host logging behavior and it must be used by the component
> logging interface to modify a single components log behavior.

### Orchestration

Orchestration is the process of starting the components in the correct order and
ensuring that they stay running. Greengrass components provide a recipe document
that informs the orchestrator how to start the component and what the
dependencies are. Greengrass lite will use the recipe document as the input and
translate the document into the host native orchestrator format. For example,
the recipe can be processed into a systemd unit file or windows system service
registry entry. The translation process must be developed for each host. The
translator can be performed by the customer during component development, in the
GG cloud console or in the GG edge device. Recipe documents contain descriptions
of each phase of a generic component operation and any dependencies needed by
the component. These documents can be translated during deployment into systemd
or initd documents and then the translated documents will be used by the host to
automatically start Greengrass. A Greengrass system monitor can keep track of
the component health by using host native interfaces (systemd/windows has an API
for this). This strategy requires 2 simple host native components.

1. Recipe translation
2. Component Health monitoring

Each host type will require these components but the component functions are
well defined which will simplify their code and maintenance.

### Component Health Monitoring

This component provides the subscription data for IPC for monitoring the system
state of the components.

#### Example

SystemD allows processes to collect process information over the “dbus”. A
health monitor component can collect the greengrass component information and
provide that to subscribers over the databus and through the IPC bridge.
Alternatively, a file could be kept up to date with a list of components and
their current state. This file would be updated by orchestration operations
(could be a script) and read by the health monitor.

### Configuration

Greengrass has a global configuration system for all components. This global
configuration allows the system state to be backed up and restored outside of
the component operation to enable state rollback (not system wide rollback) and
system state. All Greengrass hosts have some form of lightweight database
available to them. These databases have transaction logging, security and
rollback features. The configuration interface provides each component a direct
access to the host database. A configuration translator can update the database
from GGv2 T-Log files for migration support.

#### Example

SQLite (https://www.sqlite.org/index.html) is one popular small database that
offers small size, speed, and reliability. Putting the configuration in an
sqlite database would provide fast direct access and the sqlite features can be
used for backups, restore and security. An open question is if components should
use direct access to sqlite or if they should go through a configuration
abstraction. The abstraction allows the database to be changed without affecting
the components but sqlite is already cross platform and widely supported on all
the hosts being considered for Greengrass lite.

## Runtime View

Show a “typical” generic component publishing over MQTT and receiving
subscription data from MQTT.

### Simple publish by generic component to IoT Core

The sequence diagram shows a basic MQTT publish from a generic component.
![MQTT Publish Sequence Diagram](./gg-architecture-drawings/sequence%20diagram.png)

#### Sequence of operation

1. The generic component sends an publish message on the IPC socket where it is
   caught by the IPC bridge.
2. The IPC bridge determines that the publish message is intended for the IoT
   Core Component and sends a connect message to the IoT Core Component on the
   databus. The connect message is needed because this is the first time the IPC
   bridge has connected to the IoT Core component.
3. The IPC bridge translates the publish command into a databus call command and
   issues the call on the channel made in step 2.
4. The IoT core component issues the publish and returns. Databus call’s are
   synchronous so the IPC bridge was waiting for the response.
5. The IPC bridge places the response into the correct event stream on the
   Greengrass IPC system. Greengrass IPC can handle multiple simultaneous
   asynchronous data channels (event streams) so the IPC bridge must maintain
   state about the publish command and place the response in the correct
   channel.
6. Later, the generic component sends a new publish command to the IPC bridge.
7. The publish is immediately translated into a call because the previously
   opened channel (step 2) is still open. It is possible for a single channel
   between the IPC bridge and IoT Core component to support many different
   generic components.

### Observations

#### IPC bridge knowledge dependency upon IPC message handlers

The IPC bridge routes IPC messages to the component that can process the
message. To eliminate a dependency between the IPC bridge and every IPC
processing component, IPC processing components must register with the IPC
bridge at startup. The IPC bridge will maintain a routing table for each command
and will forward the command unchanged to the handling component. The handling
component must understand the IPC form of the command. The bridge will perform
minimal translating of the incoming message and send that message over the
databus to the processing component.

#### Minimizing IPC bridge state

The synchronous nature of databus commands allows the IPC bridge to maintain
ZERO state for each message. The message will arrive on IPC, it will be routed
to a dashboard component and the message handler will block waiting for the
dashboard component response. The response will then be routed back to the IPC
source in the same function that received the message. Incoming IPC messages
will need worker threads or similar mechanisms to ensure sufficient parallelism
in the IPC bridge system to minimize latency.

#### Long running IPC commands (subscriptions)

A subscription on IPC is an example of a long running IPC command that will be
periodically updated as new information becomes available. The IPC bridge will
use one of the the databus subscription strategies (above). The exact choice
depends upon the size/performance tradeoffs of that IPC bridge and it would be
possible to write different bridges that optimize for different use-cases.

#### IPC Security

Generic components have access permissions for IPC commands. These are conveyed
though configuration settings. The IPC bridge component must validate
permissions before honoring a request.

## Deployment View

Show how a deployment will work from receiving a job, downloading the artifacts,
executing the recipe

### Basic Deployment

A basic component deployment follows the steps in the diagram below.

![deployment process](./gg-architecture-drawings/deployment%20view.png)

1. A job document is sent from the job service into Greengrass to be processed
   by the deployment service.
2. The deployment service downloads all the artifacts into the artifact store.
   The source is usually from S3.
3. The deployment service consults with the Greengrass service to ensure the
   correct versions of the job specified components are being downloaded
4. The recipe is fetched from the artifact store and converted to the host
   specific orchestrator data.

After the orchestration data has been delivered to the host, the deployment
system can command the host to activate the new component.

### Example

In a linux systemd environment, the orchestration data arrives as a one or more
unit files. A typical example of a systemd unit file is shown below.

```
[Unit]
Description=A greengrass component
StartLimitIntervalSec=3600
StartLimitBurst=3
#HARD dependency
Requires= ggl.core.ggipcd.service
#WEAK Dependency
Wants=TES.service
#For Version
Conflicts= TES@1.2.service

[Service]
User=GreengrassUser
WorkingDirectory=/opt/local/greengrass
Environment=<some nice environment variables>
ExecStartPre=/opt/local/greengrass/componentdir/componentStart.sh
ExecStart=/opt/local/greengrass/componentdir/componentRun.sh
OnFailure=greengrass-recovery.service
Restart=on-failure
RestartSec=10s

[Install]
WantedBy=GreengrassCore.target
```

After this file is created the component can be started manually by: systemctl
enable <greengrasscomponent>.service and it can be configured to start at boot
by: systemctl start <greengrasscomponent>.service Of course it can also by
started programmatically through libsystemd and the dbus API.

### Motivation

The deployment motivation is to reuse the host system to the extent possible.
Greengrass V2 builds a unique greengrass orchestration system so that it will be
portable. That system is very complex, the initial POC indicates that supporting
multiple translating systems for recipe files is less work than a single common
system (at least for a systemd target).

### Quality and/or Performance Features

Deployment happens once so the key quality/performance attributes are:

1. Accuracy - Ensure that the correct behavior is translated to the underlying
   host orchestrator
2. Simplicity - Ensure that the deployment process is clear, debuggable and
   maintainable.
3. Traceable - Ensure every step of the process is logged and auditable.

## Rollback

Rollback is a feature used by Greengrass to restore a previously operating
condition. Typically the system state will be captured before a deployment and
if the deployment fails, the state can be restored. That state can be quite deep
as the deployments can affect the entire filesystem.

#### Constraints on Rollback

| <!-- -->   | <!-- -->                                                                  |
| ---------- | ------------------------------------------------------------------------- |
| Size/Scope | A deployment is capable of modifying the entire filesystem.               |
| Filesystem | Different filesystems have different features that can affect a rollback. |

#### Basic Rollback

Rollback is implemented in a device specific way by a failure component. The
failure component has the task of correcting a failed component or rolling back
to a previous state. Detecting a failed component is the responsibility of the
host orchestrator.

##### Example:

A service FOO is installed on a systemd host. The recipe is translated to the
following systemd unit file.

```
[Unit]
Description=FOO
StartLimitIntervalSec=3600
StartLimitBurst=3
OnFailure=greengrass-recovery.service

[Service]
ExecStart=/usr/local/sbin/foo.sh
Restart=on-failure
```

This unit file will restart FOO 3 times within an hour should FOO fail in any
way. After 3 failures in an hour it will execute the
greengrass-recovery.service. The `greengrass-recovery.service` is described in
the following unit file.

```
[Unit]
Description=Greengrass Recovery
[Service]
Type=oneshot
ExecStart=/usr/local/sbin/greengrass-recovery.sh
```

This unit will launch the recovery task ONCE. The recovery task will take the
actions appropriate for this host. That could be attempting repair or it could
be rolling back to a previous version.

Note: Windows also has a similar mechanism for system services. After a failure,
the windows service manager can perform actions specified in the service
registry.
https://learn.microsoft.com/en-us/windows/win32/api/winsvc/ns-winsvc-service_failure_actionsa

## Rollback Strategies

Different filesystems have special options that can simplify roll-back. There
are 3 different scopes to a roll-back.

| <!-- -->   | <!-- -->                                                          |
| ---------- | ----------------------------------------------------------------- |
| Component  | Just the failing component needs corrective action or a roll-back |
| Greengrass | All of greengrass needs corrective action or a roll-back          |
| System     | The entire OS needs corrective action or a roll-back              |

These scopes need to be considered when creating the recovery process. It may be
necessary to partition the filesystem to limit the scope of some rollback
technologies.

#### Example 1 - ZFS

ZFS supports filesystem snapshots. These are named points in the life of the
filesystem and it is possible to revert the filesystem to a snapshot and all
data newer than the snapshot is lost. This is very useful to roll back a
greengrass device but it will destroy log data that might be needed to diagnose
the cause of the rollback. Using a separate data partition on the filesystem to
hold the log data is one solution. In this way, the Greengrass system can be
rolled back but the logs are retained. This is not the only solution but it
points out that some care must be taken with filesystem rollback features.

#### Example 2 OSTREE

The ostree (https://github.com/ostreedev/ostree) system allows “gitlike” access
to artifacts downloaded into a running system. The system uses hardlinks on the
downloaded artifacts to do transactional upgrades and rollbacks all the way to
the root filesystem level. i.e. it is possible to replace the entire operating
system.

#### Example 3 SQLite

For a component level or a greengrass level rollback it may be sufficient to
move the configuration database to an earlier point and restart greengrass. This
can be easily accomplished in SQLite because it supports rollback journals and
the state can be restored to previous points. The rollback process would move
the SQLite to the previous point and then restart greengrass.

### Advanced options

#### per-component failure response

Greengrass Lite consists of core components that are each started by host
orchestration files. It is possible per-component failure recovery processes to
take place. This level of detail is not currently defined in the recipe files so
the current recommendation is a global roll-back. However, a future update to
recipes could allow corrective action on each component before a global
roll-back takes places.

#### graceful degradation

Greengrass Lite consists of core components that may not all fail in the event
of a problem. In this case, some services may be able to continue operation
allowing a “limp-home” mode. This behavior can be created in the recovery
handling.

#### Logging

It is critical that the cloudwatch log forwarding task be as independent as
possible to maximize the chances of cloudwatch getting a complete set of logs
during a failure. The host logging system is the log path in all cases (before
forwarding) so even the case of a full roll-back to an earlier state, the logs
must remain intact. If a filesystem snapshot is used for roll-back, the logs
must be in a different partition to avoid being affected and loosing log data.

## Architecture Decisions

The architectural strategy of this Greengrass lite is to be as simple as
possible and letting the host native services handle processes to the extent
possible. Memory safety will be handled by using industry standard tools such as
CBMC, valgrind, and fuzzers or by language choices such as RUST. These decisions
can be made on a component by component basis because each component is an
independent application. Dependencies between components are minimized by their
isolation with any dependency caused by RPC needs across the databus. This type
of dependency can be easily mocked for testing allowing components to be
developed and validated in isolation.

## Risks and Technical Debts

| Risk                                                    | Mitigation                                                                                                                                                                        | Impact                                                                                                                                                                                                                                |
| ------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Host default orchestration insufficient to run a recipe | Write a custom orchestrator                                                                                                                                                       | Only orchestration component is affected                                                                                                                                                                                              |
| databus performance insufficient for some tasks         | 1. Databus can be customized behind the abstraction <br/> 2. Re-evaluate the component functional division of effort <br/> 3. Some components can make a direct connection to AWS | 1. Only the databus library is affected.<br/> 2. Multiple components may be affected as the new function division is implemented.<br/> 3. Only the performance components are affected.                                               |
| High Process count affecting system performance         | 1. Evaluate the databus thread usage. A star topology is slower but may have fewere threads.<br/>2. Combine some core components<br/>3. Consider more capable HW                  | 1. Only the databus library is affected but performance changes must be benchmarked<br/>2. This will be complex and introduces the dependencies between components.<br/>3. The system may just be too slow for the desired load.<br/> |
| An expected host feature does not exist on a host       | 1. Change hosts<br/>2. The interface component can be more complex and implement the required feature.                                                                            | 1. Even an OS change can add significant complexity to a deployment.<br/> 2. A more complicated interface component to maintain.                                                                                                      |

## Additional Areas to Discuss

- Configuration store - each component has configuration associated with it,
  that (GGv2-classic) is timestamp merged with multiple authority and available
  via IPC configuration functions. Related- if someone brings a new protocol
  translation, how do they apply the security guards on that too.
- “config.yaml”/TLOG - debate on if it is really necessary/desirable to switch
  between GG-classic and GG-lite. If you can relax that requirement - in which
  case the argument may be asked if it’s really the same “v2” product - it
  relaxes need to capture and log config transactions. Related question is how
  the top-level system configuration is applied.
- IPC security - there is rule-based security around IPC, and also finer detail
  in pub/sub actions. This doesn’t map well to file permissions. Deeper dive in
  this with AppSec involvement. In particular, there is an identity attestation
  mechanism possible with GG being orchestrator that allows a component to
  provide it is acting on behalf of a component identity. GG starts component
  “FooBar”, presenting a token representing “FooBar”, that the process
  re-presents to GG to prove it is “FooBar” or a representative of “FooBar” - a
  non-breaking alternative to obtain this identity is required as FooBar is now
  started by systemd and not by the GG orchestrator.
- With GG-classic and previous GG-lite design, there was a path on how
  components/binary signatures can be verified to allow end-to-end attestation -
  how might this be accomplished here
- The architecture is thread heavy to promote simplicity. Do an estimation of an
  application that has sufficient complexity of pub/subs, and number of threads
  that are likely to be consumed (but idle) - what is the resource cost of that
  application - this will help confirm that the approach isn’t going to be a
  problem.
- Any other potential issues when running in docker? systemd is one

## Glossary

| Term    | Definition                                                                                                                               |
| ------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| GG IPC  | The Interprocess communication system used by Greengrass V2 for communications between generic components and nucleus (GGv2 application) |
| databus | The IPC system suggested in this document for communications between core components and future "native" components.                     |
| host    | The collection of CPU, OS, and OS details that create the running environment for a specific Greengrass system.                          |
