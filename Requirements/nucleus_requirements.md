# Nucleus Requirements

>TODO remove most block quotes 

## API requirements
These requirements are derived from the API tests.

## Buffer requirements
>These tests are derived from buffer_tests.cpp

### _BUF-01_ A newly constructed buffer will be empty
The size of a new buffer is 0.

### _BUF-02_ A buffer has an input stream (std::istream) interface
A buffer contains an input stream interface allowing data to be added to the buffer with the stream operator (<< )

### _BUF-03_ A buffer has an output stream (std::ostream) interface
A buffer with data can provide the data via an output stream interface

### _BUF_04_ The buffer input stream interface retains is position in the data between calls
Writing to the buffer with the stream interface with two separate calls will concatenate the data.

### _BUF_05_ The buffer output stream interface retains is position in the data between calls
Reading from the buffer with the stream interface with two separate calls will return consecutive parts of the data.

### _BUF-06_ Getting data from the buffer does not remove the data from the buffer
Reading data via any interface will return the data accessed at that location but will not remove the data.
Re-reading the data from the same location will return teh same data. 

### _BUF-07_ The buffer interface includes a `get` method that can return the buffer contents as a std::string
### _BUF-08_ The buffer interface includes a `get` method that can return a portion of the buffer contents as a std::string
### _BUF-09_ The buffer interface includes a `get` method that can return the buffer as a std::vector<byte>
### _BUF-10_ The buffer interface includes a `get` method that can return a portion of the buffer contents as a std::vector<byte>

### _BUF-11_ Writes to the buffer at the same location as existing data will OVERWRITE the existing data with the new data.
Overwrite behavior will be obeyed for ANY method that can write to the buffer and at any location inside the buffer.

### _BUF-12_ Reading values from the buffer stream into integers will result in an integer conversion from string if possible.
### _BUF-13_ If type conversion cannot be performed when reading data from the buffer via the stream interface, the *_TBD_* error will be produced ??Thrown??
