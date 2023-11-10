# LIST requirements
>These tests are derived from list_tests.cpp

### 1.
A newly constructed list will be empty

### 2.
The list interface shall include a method `append` that will add elements to the end of the list.

### 3.
The list interface shall include a method `get` that will return the elements indicated by an integer parameter.  
`template<class T> T list::get<T>(int index)`

### 4.
List elements are a heterogenous mix of types.
One list can contain elements of any type.

### 5.
The list shall retain the elements in the order they are added.

### 6.
The list interface shall include a method `insert` That will allow inserting new elements at an index.
