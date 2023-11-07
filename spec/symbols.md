# String interning system

For common string constants, the interning system allows you to provide a string
and returns a "symbol" (small data that represents the string data). This allows
using them where there are common copies to save memory, and efficiently compare
them. As an example, the data format for LPC for keys use them.

## Terminology

The term "equal", when applied to strings, means they have the same byte
content, not that they have the same memory location.

The term "equal", when applied to integer values, means the values of each
matching bit are the same.

## 1.

The interning system utilizes values referred to as "symbol". The use of the
term symbol is distinct from the specific data type encoding them, see 6.

## 2.

All symbols have an associated string value.

## 3.

The Symbol data type which represents symbols.

### 3.1.

Unique symbols are represented by unique non-zero values of the Symbol data
type.

### 3.2.

The Symbol data type must be interchangeable with fixed width, 32-bit unsigned
integers.

### 3.3.

The only valid operation on a symbol integer value is to check for equality.

### 3.4.

If a caller constructs a symbol value that is not equal to a value obtained from
this system, that value is not a valid symbol.

### 3.5.

The Symbol data type may encode values that are not valid symbols.

### 3.6.

The Symbol data type with value 0 represents the lack of a symbol (null).

#### 3.6.1

The Symbol data type with value 0 is not a valid symbol.

### 3.7.

Two Symbol data types compare equal if and only if they have the same integer
value, regardless of whether they encode valid symbols.

## 4.

There shall be a function to obtain a symbol from a string.

### 4.1.

If this function is used to obtain a symbol for a string that is equal to a
string previously supplied for some call of the function, the function must
return a symbol value that is equal to the symbol returned from that call.

### 4.2.

If this function is used to obtain a symbol for a string that is not equal to
any string previously supplied to the function, the function must return a 
symbol value that is not equal to any previously returned symbol.

### 4.3.

Two symbols can be equal if and only if the strings used to obtain them are
equal.

### 4.4.

If this function cannot return a symbol, it will throw an exception.

### 4.5.

Calling this function at overlapping times from different threads returns
results that must be equivalent to the results of some non-overlapping
sequential ordering of identical calls.

## 5.

There shall be a function to obtain a string from a symbol.

### 5.1.

The string returned must be equal to the string used to obtain the symbol.

### 5.2.

This function will not throw if passed a valid symbol.

### 5.3.

This function will throw if passed an invalid symbol.

### 5.4.

The strings returned by this function will remain valid as long as the system
instance is valid.

## 6.

There will be a function to check whether a symbol representing a string already
exists.

### 6.1

If the string passed is equal to a string previously passed to the function
described by section 4, then the function returns a symbol equal to the symbol
previously returned.

### 6.2

If the string passed is not equal to any string previously passed to the
function described by section 4, then the Symbol data type returned has value 0.

## 7.

The system will obfuscate the integer representation of symbols so that ordering
is not accidentally depended on.

## 8.

The system will allocate only one string per symbol.
