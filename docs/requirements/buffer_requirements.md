# Buffer requirements

> These tests are derived from buffer_tests.cpp

### 1.

A newly constructed buffer shall be empty. The size of a new buffer is 0.

### 2.

A buffer shall have an input stream (std::istream) interface. A buffer contains
an input stream interface allowing data to be added to the buffer with the
stream operator (<< )

### 3.

A buffer shall have an output stream (std::ostream) interface. A buffer with
data can provide the data via an output stream interface

### 4.

The buffer input stream interface shall retain its position in the data between
calls. Writing to the buffer with the stream interface with two separate calls
will concatenate the data.

### 5.

The buffer output stream interface shall retain its position in the data between
calls. Reading from the buffer with the stream interface with two separate calls
will return consecutive parts of the data.

### 6.

Getting data from the buffer shall not remove the data from the buffer. Reading
data via any interface will return the data accessed at that location but will
not remove the data. Re-reading the data from the same location will return teh
same data.

### 7.

The buffer interface shall include a `get` method that can return the buffer
contents as a std::string.

### 8.

The buffer interface shall include a `get` method that can return a portion of
the buffer contents as a std::string.

### 9.

The buffer interface shall include a `get` method that can return the buffer as
a std::vector<byte>.

### 10.

The buffer interface shall include a `get` method that can return a portion of
the buffer contents as a std::vector<byte>.

### 11.

Writes to the buffer at the same location as existing data will OVERWRITE the
existing data with the new data. Overwrite behavior will be obeyed for ANY
method that can write to the buffer and at any location inside the buffer.

### 12.

Reading values from the buffer stream into integers will result in an integer
conversion from string if possible.

### 13.

If type conversion cannot be performed when reading data from the buffer via the
stream interface, the _*TBD*_ error will be produced ??Thrown??
