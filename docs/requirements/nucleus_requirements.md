# Nucleus Requirements

## Abbreviations and Jargon

1. GG-Java : The existing Greengrass v2 in Java.
2. GG-Lite : The new implementation of Greengrass v2 in C++.
3. Nucleus : The executable core of Greengrass.
4.

## Background

Nucleus is the runtime for GG-Lite. The purpose of the nucleus is managing
plugins & components. To facilitate communications between plugins the Nucleus
provides the Local Process Communications (LPC) bus. To facilitate
communications between components the Nucleus provides the Interprocess
Communications (IPC) bus. Plugins are libraries (details are OS specific) that
Nucleus loads at runtime according to a recipe.

Plugins are authenticated as they are installed and as they load. Once loaded
they are trusted entities and are safe to execute inside the Nucleus process.
Plugins communicate via the LPC bus.

Components are program that Nucleus starts and monitors according to a recipe.
Components communicate with Nucleus (and other components/plugins) via the IPC
bus. Components are authenticated through a token that is provided when the
component is started.

> TODO: Fix the abbreviations in this diagram.
>
> ![](./images/top_level_nucleus_components.png "top level block diagram")

## Nucleus functionality

Nucleus has the following functions:

1. Lifecycle management of plugins
   1. Locate Plugins (_Temporary until plugin deployment is ready_)
   2. Install Plugins
   3. Load Plugins
   4. Run Plugins
   5. Unload Plugins
   6. Update Plugins
   7. Delete Plugins
2. Provide a Lifecycle API interface to the Plugin
3. Distribute messages on the IPC bus
4. Provide an API for the plugins to access the IPC bus
5. Specify the IPC bus message format
6. Distribute messages on the LPC bus
7. Provide an API for plugins to access the LPC bus
8. String Internment

## IPC Messaging

IPC message formatting conforms to the following specification:
https://quip-amazon.com/aECHAUcJkIk8/IPC-as-is-2022

> This specification needs to be updated to a public facing document.
