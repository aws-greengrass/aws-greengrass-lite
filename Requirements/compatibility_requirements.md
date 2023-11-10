## Compatibility Requirements
### 1.

Generic components written for GG-Lite are also able to run in the GG-Java environment.
Generic components rely upon environment variables and the IPC mechanisms for GG operations.
These variables and mechanisms shall be compatible between GG-Lite and GG-Java.
>Test by running a representative "complete" GG-Lite components in a GG-Java environment

### 2.

The IPC bus will be identical under GG-Lite as GG-Java

### 2.1

Generic components written for GG-Lite can communicate over the IPC bus hosted by GG-Java 
>Test by ensuring the complete GG-Lite component communicates via IPC

### 2.2

Existing components written for GG-Java can communicate over the IPC bus hosted by GG-Lite
> Test by ensuring an existing component can communicate over IPC hosted by GG-Lite

### 4.

GG-Lite Components (not plugins) have the same lifecycle management
> Test by ensuring the test GG-Lite component receives each of the GG-Java lifecycle events.
