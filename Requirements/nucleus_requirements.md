# Nucleus Requirements

## API requirements
These requirements are derived from the API tests.

## _BUF-01_ A newly constructed buffer will be empty
The size of a new buffer is 0.

## _BUF-02_ A buffer has an input stream (std::istream) interface
A buffer contains an input stream interface allowing data to be added to the buffer with the stream operator (<< )

## _BUF-03_ A buffer has an output stream (std::ostream) interface
A buffer with data can provide the data via an output stream interface

## __BUF-04_ D