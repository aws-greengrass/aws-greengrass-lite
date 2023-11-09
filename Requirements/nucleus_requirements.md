# Nucleus Requirements

## Abbreviations and Jargon
1. GG-Java : The existing Greengrass v2 in Java.
2. GG-Lite : The new implementatyion of Greengrass v2 in C++.
3. Nucleus : The executable core of Greengrass.
4. 

## Background
GG-Lite is an application called Nucleus that manages plugins & libraries.  To facilitate communications between plugins
the Nucleus provides the Local Process Communications (LPC) bus.  To facilitate communications between components the
Nucleus provides the Interprocess Communications (IPC) bus.  Plugins are libraries (details are OS specific) that
Nucleus loads at runtime according to a recipe.  Plugins are authenticated as they are installed and as they load.
Once loaded they are trusted entities and are safe to execute inside the Nucleus process.

>TODO: Fix the abbreviations in this diagram.
> 
![](./images/top_level_nucleus_components.png "top level block diagram")


## Nucleus functionality
Nucleus has the following functions:
1. Lifecycle management of plugins
   1. Locate Plugins
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
5. Distribute messages on the LPC bus
6. Provide an API for plugins to access the LPC bus
7. String Internment
8. 


## IPC Message Distribution Requirements

## IPC Interface API Requirements

### 1.0

T

## IPC Message Format Requirements

### 1.0 

IPC message formatting conforms to the following specification: https://quip-amazon.com/aECHAUcJkIk8/IPC-as-is-2022#temp:C:QcL33a7ae991c1c44709189e52af
> This specification needs to be updated to a public facing document.


## LPC Message Distribution Requirements

## LPC Authentication Requirements

## LPC Message Format Requirements

## String Internment Requirements

## Buffer requirements
>These tests are derived from buffer_tests.cpp

### _BUF-01_ A newly constructed buffer shall be empty
The size of a new buffer is 0.

### _BUF-02_ A buffer shall have an input stream (std::istream) interface
A buffer contains an input stream interface allowing data to be added to the buffer with the stream operator (<< )

### _BUF-03_ A buffer shall have an output stream (std::ostream) interface
A buffer with data can provide the data via an output stream interface

### _BUF_04_ The buffer input stream interface shall retain its position in the data between calls
Writing to the buffer with the stream interface with two separate calls will concatenate the data.

### _BUF_05_ The buffer output stream interface shall retain its position in the data between calls
Reading from the buffer with the stream interface with two separate calls will return consecutive parts of the data.

### _BUF-06_ Getting data from the buffer shall not remove the data from the buffer
Reading data via any interface will return the data accessed at that location but will not remove the data.
Re-reading the data from the same location will return teh same data. 

### _BUF-07_ The buffer interface shall include a `get` method that can return the buffer contents as a std::string
### _BUF-08_ The buffer interface shall include a `get` method that can return a portion of the buffer contents as a std::string
### _BUF-09_ The buffer interface shall include a `get` method that can return the buffer as a std::vector<byte>
### _BUF-10_ The buffer interface shall include a `get` method that can return a portion of the buffer contents as a std::vector<byte>

### _BUF-11_ Writes to the buffer at the same location as existing data will OVERWRITE the existing data with the new data.
Overwrite behavior will be obeyed for ANY method that can write to the buffer and at any location inside the buffer.

### _BUF-12_ Reading values from the buffer stream into integers will result in an integer conversion from string if possible.
### _BUF-13_ If type conversion cannot be performed when reading data from the buffer via the stream interface, the *_TBD_* error will be produced ??Thrown??

## LIST requirements
>These tests are derived from list_tests.cpp

### _LIST-01_ A newly constructed list will be empty

### _LIST-02_ The list interface shall include a method `append` that will add elements to the end of the list.

### _LIST-03_ The list interface shall include a method `get` that will return the elements indicated by an integer parameter.
`template<class T> T list::get<T>(int index)`

### _LIST-04_ List elements are a heterogenous mix of types.
One list can contain elements of any type.

### _LIST-05_ The list shall retain the elements in the order they are added.

### _LIST_06_ The list interface shall include a method `insert` That will allow inserting new elements at an index.

## IPC requirements

### _PUBSUB-01_ Topics shall be created when listeners subscribe

### _PUBSUB-02_ Named Topics shall be identified by strings

>### _PUBSUB_03_ Topic String Naming Rules are Required

### _PUBSUB_04_ Listeners shall be tracked by listener objects