## Compatibility Requirements
### 1.

GG-Lite Components (not plugins) will run in the GG-Java environment.
>Test by running a representative "complete" GG-Lite components in a GG-Java environment

### 2.

GG-Lite Components (not plugins) can communicate over the IPC bus hosted by GG-Java
>Test by ensuring the complete GG-Lite component communicates via IPC

### 4.

GG-Lite Components (not plugins) have the same lifecycle management
> Test by ensuring the test GG-Lite component receives each of the GG-Java lifecycle events.

### 5.
All IPC communications shall be compatible with GG-Java (GGv2) based IPC.
