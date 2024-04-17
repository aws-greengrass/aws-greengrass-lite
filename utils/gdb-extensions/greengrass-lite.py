# Extensions to help debug greengrass-lite
#
# Create a ".gdbinit" file in $HOME, enabling auto-loading:
#       set auto-load local-gdbinit on
#       add-auto-load-safe-path <path>
#
# Or type 'source <path to this file>' in GDB to load it
#
# Commands:
# p <symbol> - resolve and print the symbol
# gg-symbol - dump the entire symbol table
# gg-symbol <id> - dump the symbol with the given id
#
# See https://sourceware.org/gdb/current/onlinedocs/gdb.html/Python-API.html
#
""" Greengrass-lite extensions """
import sys

import gdb
import itertools
import os
import re

#
# Logic assumes it needs to run in GDB prior to version 14 - CLion uses GDB 13.1
#


def obfuscate(index):
    return gdb.parse_and_eval('data::IdObfuscator::obfuscate(%d)' % int(index))


def deobfuscate(id):
    return gdb.parse_and_eval('data::IdObfuscator::deobfuscate(%d)' % int(id))


def is_index_invalid(index):
    return int(index) == 0xffffffff


#
# Access to the contextualized symbols table - if symbols table is changed, this class
# needs to also change.
#
class SymbolTable(object):

    def __init__(self, table):
        if table is None:
            table = gdb.parse_and_eval('&scope::context().get()->symbols()')
        if table.type.code != gdb.TYPE_CODE_PTR:
            table = table.address
        self._table = table

    def eval(self, expr):
        gdb.set_convenience_variable('obj', self._table)
        return gdb.parse_and_eval("$obj->" + str(expr))

    def buffer(self):
        return self._table['_buffer'].address

    def buffer_eval(self, expr):
        gdb.set_convenience_variable('obj', self.buffer())
        return gdb.parse_and_eval("$obj->" + str(expr))

    def size(self):
        return self.buffer_eval("size()")

    def span(self, partial):
        gdb.set_convenience_variable('arg', partial)
        return self.buffer_eval("getSpan($arg)")

    def is_valid(self, partial):
        gdb.set_convenience_variable('arg', partial)
        return bool(self.buffer_eval("isValid($arg)"))

    def string_view(self, partial):
        gdb.set_convenience_variable('arg', partial)
        return self.buffer_eval("at($arg)")

    @staticmethod
    def id_of_partial(partial):
        return partial['_asInt']

    @staticmethod
    def partial_of_id(handle_id_value):
        # Create typed partial handle
        # See data/safe_handle.hpp data:PartialHandle
        # Note, treat return partial as temporary
        handle_id = int(handle_id_value)
        partial_type = gdb.lookup_type('data::Symbol::Partial')
        id_bytes = handle_id.to_bytes(4, sys.byteorder)
        return gdb.Value(id_bytes, partial_type)

    def symbol_of_partial(self, partial):
        # return value is temporary
        gdb.set_convenience_variable('arg', partial)
        return self.eval('applyUnchecked($arg)')


#
# Abstracts a single symbol, only calling various underlying code as needed
#
class LazySymbol(object):

    def __init__(self, table, handle, index=None):
        if table is None:
            table = SymbolTable(None)
        self._table = table
        if handle is None:
            handle = int(obfuscate(index))
        if isinstance(handle, int):
            self._handle_id = handle
        else:
            self._handle_id = SymbolTable.id_of_partial(handle)
        self._index = index
        self._string = None
        self._is_valid = None

    def table(self):
        return self._table

    def handle_id(self):
        return self._handle_id

    def index(self):
        if self._index is None:
            self._index = int(deobfuscate(self._handle_id))
        return self._index

    def partial(self):
        # returns temporary value - do not store
        return SymbolTable.partial_of_id(self._handle_id)

    def symbol(self):
        # returns temporary value - do not store
        return self._table.symbol_of_partial(self.partial())

    def is_null(self):
        return self._handle_id == 0

    def is_valid(self):
        if self._is_valid is None:
            self._is_valid = self._handle_id != 0 and self._table.is_valid(
                self.partial())
        return self._is_valid

    def string(self):
        if self._string is not None:
            return self._string
        if self.is_null():
            self._string = ''
        elif self.is_valid():
            self._string = self._table.string_view(self.partial())
        else:
            self._string = '!error!'
        return self._string

    def pretty(self):
        if self.is_null():
            return 'Symbol{0}'
        if not self.is_valid():
            return '!error!'
        return self.string()


#
# Base class for all variations of symbol printers - just delegates to the lazy symbol object
#
class SymbolPrinter(object):

    def __init__(self, lazy_symbol):
        self._lazy = lazy_symbol

    def to_string(self):
        return self._lazy.pretty()

    def to_simple_string(self):
        return self._lazy.string()

    def display_hint(self):
        return None


#
# Nucleus data::Symbol printer
#
class DataSymbolPrinter(SymbolPrinter):

    def __init__(self, val):
        table = SymbolTable(val['_table']['_p'])
        partial = val['_partial']
        super(DataSymbolPrinter, self).__init__(LazySymbol(table, partial))


#
# Nucleus data::Symbol::Partial printer
#
class PartialDataSymbolPrinter(SymbolPrinter):

    def __init__(self, val):
        super(PartialDataSymbolPrinter, self).__init__(LazySymbol(None, val))


#
# Nucleus ggapi::Symbol printer
#
class PartialGgApiSymbolPrinter(SymbolPrinter):

    def __init__(self, val):
        handle_id = val['_asInt']
        super(PartialGgApiSymbolPrinter,
              self).__init__(LazySymbol(None, handle_id))


#
# Printer for entire symbol table
#
class DataSymbolTablePrinter(object):
    """Print a Greengrass symbol table"""

    class Iterator(object):

        def __init__(self, table):
            self._index = 0
            self._table = table
            self._size = table.size()

        def __iter__(self):
            return self

        def __next__(self):
            (index, self._index) = (self._index, self._index + 1)
            if index == self._size:
                raise StopIteration
            lazy_symbol = LazySymbol(self._table, None, index)
            return '[%d]' % index, lazy_symbol.symbol()

    def __init__(self, val):
        self._val = val

    def size(self):
        string_table = SymbolTable(self._val)
        return string_table.size()

    def to_string(self):
        return ('data::SymbolTable of size %d' % self.size())

    def children(self):
        return self.Iterator(SymbolTable(self._val))

    def display_hint(self):
        return 'array'


#
# Access to the contextualized handle table - if the handle table is changed, this
# needs to also change.
#
class HandleTable(object):

    def __init__(self, table):
        if table is None:
            table = gdb.parse_and_eval('&scope::context().get()->handles()')
        if table.type.code != gdb.TYPE_CODE_PTR:
            table = table.address
        self._table = table

    def eval(self, expr):
        gdb.set_convenience_variable('obj', self._table)
        return gdb.parse_and_eval("$obj->" + str(expr))

    def handles(self):
        return self._table['_handles'].address

    def first_root_index(self):
        return int(self._table['_activeRoots']['next'])

    def first_root(self):
        return self.lazy_root_at(self.first_root_index())

    def handle_expr(self, expr):
        gdb.set_convenience_variable('obj', self.handles())
        return gdb.parse_and_eval("$obj->" + str(expr))

    def handle_lookup(self, index):
        return self.handle_expr('lookup(%d)' % int(index))

    def handle_at(self, index):
        return self.handle_expr('at(%d)' % int(index))

    def lazy_handle_at(self, index):
        if is_index_invalid(index):
            return None
        entry = self.handle_at(index)
        return LazyObjHandle(self, None, None, entry)

    def roots(self):
        return self._table['_roots'].address

    def root_expr(self, expr):
        gdb.set_convenience_variable('obj', self.roots())
        return gdb.parse_and_eval("$obj->" + str(expr))

    def root_lookup(self, index):
        return self.root_expr('lookup(%d)' % int(index))

    def root_at(self, index):
        return self.root_expr('at(%d)' % int(index))

    def lazy_root_at(self, index):
        if is_index_invalid(index):
            return None
        entry = self.root_at(index)
        return LazyRootHandle(self, None, None, entry)

    @staticmethod
    def id_of_partial(partial):
        return partial['_asInt']

    @staticmethod
    def partial_of_id(handle_id_value):
        # Create typed partial handle
        handle_id = int(handle_id_value)
        partial_type = gdb.lookup_type('data::ObjHandle::Partial')
        id_bytes = handle_id.to_bytes(4, sys.byteorder)
        return gdb.Value(id_bytes, partial_type)

    @staticmethod
    def root_partial_of_id(handle_id_value):
        # Create typed partial handle
        handle_id = int(handle_id_value)
        partial_type = gdb.lookup_type('data::RootHandle::Partial')
        id_bytes = handle_id.to_bytes(4, sys.byteorder)
        return gdb.Value(id_bytes, partial_type)

    def root_handle_of_partial(self, partial):
        # return value is temporary
        gdb.set_convenience_variable('arg', partial)
        return self.eval('applyUncheckedRoot($arg)')


#
# Represents a single object handle, with lazy calls of underlying code
#
class LazyObjHandle(object):

    def __init__(self, table, handle, index=None, entry=None):
        if table is None:
            table = HandleTable(None)
        self._table = table
        self._is_valid = None
        self._entry = None
        if entry is not None:
            self._is_valid = True
            self._entry = entry
            index = entry['check']
        if handle is None:
            handle = int(obfuscate(index))
        if isinstance(handle, int):
            self._handle_id = handle
        else:
            self._handle_id = HandleTable.id_of_partial(handle)
        self._index = index
        self._object = None
        self._root_index = None

    def table(self):
        return self._table

    def handle_id(self):
        return self._handle_id

    def index(self):
        if self._index is None:
            self._index = int(deobfuscate(self._handle_id))
        return self._index

    def partial(self):
        return HandleTable.partial_of_id(self._handle_id)

    def handle(self):
        return self._table.handle_of_partial(self.partial())

    def is_null(self):
        return self._handle_id == 0

    def entry(self):
        if self._entry is None and self._is_valid is None:
            self._entry = self._table.handle_lookup(self.index())
            if int(self._entry) == 0:
                self._is_valid = False
                return None
            self._is_valid = True
        return self._entry

    def next(self):
        entry = self.entry()
        if not entry:
            return None
        index = entry['next']
        return self._table.lazy_handle_at(index)

    def is_valid(self):
        self.entry()  # side-effect is setting valid flag
        return self._is_valid

    def object_ptr(self):
        if self._object is not None:
            return self._object
        entry = self.entry()
        if not self._is_valid:
            return None
        gdb.set_convenience_variable('obj', entry['obj'].address)
        obj = gdb.parse_and_eval('$obj->get()')
        obj = obj.cast(gdb.lookup_type('data::TrackedObject*'))
        self._object = obj.cast(obj.dynamic_type)
        return self._object

    def root(self):
        entry = self.entry()
        if not entry:
            return None
        index = entry['rootIndex']
        handle_id = int(obfuscate(index))
        if handle_id == 0:
            return None
        else:
            return LazyRootHandle(self._table, handle_id, index)

    def pretty(self):
        return str(self.object_ptr())

    def __str__(self):
        return 'ObjHandle{%d}' % self.handle_id()


#
# Represents an object handle root
#
class LazyRootHandle(object):

    def __init__(self, table, handle, index=None, entry=None):
        if table is None:
            table = HandleTable(None)
        self._table = table
        self._is_valid = None
        self._entry = None
        if entry is not None:
            self._is_valid = True
            self._entry = entry
            index = entry['check']
        if handle is None:
            handle = int(obfuscate(index))
        if isinstance(handle, int):
            self._handle_id = handle
        else:
            self._handle_id = HandleTable.id_of_partial(handle)
        self._index = index
        self._object = None
        self._root_index = None

    def table(self):
        return self._table

    def handle_id(self):
        return self._handle_id

    def index(self):
        if self._index is None:
            self._index = int(deobfuscate(self._handle_id))
        return self._index

    def partial(self):
        return HandleTable.root_partial_of_id(self._handle_id)

    def handle(self):
        return self._table.root_handle_of_partial(self.partial())

    def is_null(self):
        return self._handle_id == 0

    def entry(self):
        if self._entry is None and self._is_valid is None:
            self._entry = self._table.root_lookup(self.index())
            if int(self._entry) == 0:
                self._is_valid = False
                return None
            self._is_valid = True
        return self._entry

    def first_object(self):
        entry = self.entry()
        if not entry:
            return None
        index = entry['handles']['next']
        return self._table.lazy_handle_at(index)

    def next(self):
        entry = self.entry()
        if not entry:
            return None
        index = entry['next']
        return self._table.lazy_root_at(index)

    def is_valid(self):
        self.entry()  # side-effect is setting valid flag
        return self._is_valid

    def __str__(self):
        return 'RootHandle{%d}' % self.handle_id()


class DynamicStruct(object):
    """Provides access to struct object"""

    def __init__(self, val):
        if val.type.code != gdb.TYPE_CODE_PTR:
            val = val.address
        self._val = val

    def eval(self, expr):
        gdb.set_convenience_variable('obj', self._val)
        return gdb.parse_and_eval("$obj->" + str(expr))

    def keys(self):
        # Hack to use 'children' of std::vector to collect values
        std_vec = self.eval('getKeys()')
        vis = gdb.default_visualizer(std_vec)
        if vis is None:
            return []
        i = vis.children()
        if i is None:
            return []
        arr = []
        for (k, v) in i:
            arr.append(v)
        return arr

    def size(self):
        return self.eval('size()')


#
# Break an object into related parts
#
class ObjHandleIterator(object):
    """Iterator to expand object"""

    def __init__(self, lazy_obj):
        self._lazy_obj = lazy_obj
        self._index = 0

    def __iter__(self):
        return self

    def __next__(self):
        (index, self._index) = (self._index, self._index + 1)
        if index == 0:
            return 'handle', 'ObjHandle{%d}' % int(
                self._lazy_obj.handle_id())  # helps CLion
        if index == 1:
            return 'object', self._lazy_obj.object_ptr().dereference()
        if index == 2:
            return 'root', 'RootHandle{%d}' % int(
                self._lazy_obj.root().handle_id())
        raise StopIteration


#
# Base printer for object handles
#
class ObjHandlePrinter(object):

    def __init__(self, lazy_obj, type='data::ObjHandle'):
        self._lazy_obj = lazy_obj
        self._type = type

    def to_string(self):
        return '%s{%d}' % (str(self._type), self._lazy_obj.handle_id())

    def children(self):
        if not self._lazy_obj.is_valid():
            return None
        return ObjHandleIterator(self._lazy_obj)


class DataObjHandlePrinter(ObjHandlePrinter):

    def __init__(self, val):
        table = HandleTable(val['_table']['_p'])
        partial = val['_partial']
        super(DataObjHandlePrinter,
              self).__init__(LazyObjHandle(table, partial), val.type)


class PartialDataHandlePrinter(ObjHandlePrinter):

    def __init__(self, val):
        super(PartialDataHandlePrinter,
              self).__init__(LazyObjHandle(None, val), val.type)


class PartialGgApiHandlePrinter(ObjHandlePrinter):

    def __init__(self, val):
        gdb.set_convenience_variable('obj', val['_handle'].address)
        handle_id = gdb.parse_and_eval('$obj->get()->_handle')
        partial = HandleTable.partial_of_id(handle_id)
        super(PartialGgApiHandlePrinter,
              self).__init__(LazyObjHandle(None, partial), val.type)


#
# Base printer for object roots
#
class RootHandlePrinter(object):

    def __init__(self, lazy_obj, type='data::RootHandle'):
        self._lazy_obj = lazy_obj
        self._type = type

    def to_string(self):
        return '%s{%d}' % (str(self._type), self._lazy_obj.handle_id())


class DataRootHandlePrinter(RootHandlePrinter):

    def __init__(self, val):
        table = HandleTable(val['_table']['_p'])
        partial = val['_partial']
        super(DataRootHandlePrinter,
              self).__init__(LazyRootHandle(table, partial), val.type)


class PartialRootHandlePrinter(RootHandlePrinter):

    def __init__(self, val):
        super(PartialRootHandlePrinter,
              self).__init__(LazyRootHandle(None, val), val.type)


class DynamicStructPrinter:

    class Iterator(object):

        def __init__(self, struct):
            self._struct = struct
            self._index = 0
            self._keys = struct.keys()
            self._size = len(self._keys)

        def __iter__(self):
            return self

        def __next__(self):
            if self._index == self._size:
                raise StopIteration

            (index, self._index) = (self._index, self._index + 1)
            name = self._keys[index]
            str_name = str(DataSymbolPrinter(name).to_simple_string())
            gdb.set_convenience_variable('arg', name)
            val_type = int(self._struct.eval('get($arg).getType()'))
            if val_type == 0:
                val = '[null]'
            elif val_type == 1:
                val = self._struct.eval('get($arg).getBool()')
            elif val_type == 2:
                val = self._struct.eval('get($arg).getInt()')
            elif val_type == 3:
                val = self._struct.eval('get($arg).getDouble()')
            elif val_type == 4:
                val = self._struct.eval('get($arg).rawGetString()')
            elif val_type == 5:
                val = self._struct.eval('get($arg).rawGetSymbol()')
            elif val_type == 6:
                obj = self._struct.eval('get($arg).getObject().get()').cast(
                    gdb.lookup_type('data::TrackedObject*'))
                val = obj.cast(obj.dynamic_type).dereference()
            else:
                val = '[Unknown]'

            return str(str_name), val

    def __init__(self, val):
        self._val = val
        self._struct = DynamicStruct(val)
        self._type = val.type

    def to_string(self):
        return '%s{size=%d}' % (str(self._type), self._struct.size())

    def children(self):
        return self.Iterator(self._struct)

    def display_hint(self):
        # While it seems 'map' would make sense here, it is intended for std::map type expansion (any->any)
        # 'array' drops the keys... so use default.
        return None


class Symbols(gdb.Command):
    """Display symbol table"""

    def __init__(self):
        super(Symbols, self).__init__("gg-symbol", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        if len(args) == 0:
            table = DataSymbolTablePrinter(
                gdb.parse_and_eval('&scope::context().get()->symbols()'))
            for k in table.children():
                print(k[0], k[1])
            return None
        else:
            table = SymbolTable(
                gdb.parse_and_eval('&scope::context().get()->symbols()'))
            lazy_symbol = LazySymbol(table, int(args[0]))
            print(lazy_symbol.symbol())
            return None


class Handles(gdb.Command):
    """Display handle table"""

    def __init__(self):
        super(Handles, self).__init__("gg-handle", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        table = HandleTable(
            gdb.parse_and_eval('&scope::context().get()->handles()'))
        if len(args) == 0:
            print('Handle ID required')
            return None
        else:
            lazy_obj = LazyObjHandle(table, int(args[0]))
            if lazy_obj.is_null():
                print('ObjHandle{0} - Null handle')
            elif not lazy_obj.is_valid():
                print('ObjHandle{%d} is not valid' % lazy_obj.handle_id())
            else:
                print(str(lazy_obj.root()))
                obj = lazy_obj.object_ptr()
                if not obj:
                    print('ObjHandle{%d} cannot be resolved')
                else:
                    obj = obj.dereference()
                    print('ObjHandle{%d} = %s' % (lazy_obj.handle_id(), obj))
            return None


class HandleRoots(gdb.Command):
    """Display handle roots"""

    def __init__(self):
        super(HandleRoots, self).__init__("gg-root", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        table = HandleTable(
            gdb.parse_and_eval('&scope::context().get()->handles()'))
        args = gdb.string_to_argv(arg)
        if len(args) == 0:
            lazy_root = table.first_root()
            n = 0
            while lazy_root is not None:
                n = n + 1
                print('%d: %s' % (n, lazy_root))
                lazy_root = lazy_root.next()
            print('%d roots(s)' % n)
            return None
        else:
            lazy_root = LazyRootHandle(table, int(args[0]))
            print(lazy_root)
            obj = lazy_root.first_object()
            n = 0
            while obj is not None:
                n = n + 1
                print('%d: %s = %s' % (n, obj, obj.object_ptr()))
                obj = obj.next()
            print('%d handles(s) for %s' % (n, lazy_root))
            return None


def build_greengrass_printers():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("greengrass-lite")

    # Symbols
    pp.add_printer('data::Symbol', '^data::Symbol$', DataSymbolPrinter)
    pp.add_printer('data::Symbol::Partial', '^data::Symbol::Partial$',
                   PartialDataSymbolPrinter)
    pp.add_printer('ggapi::Symbol', '^(ggapi::Symbol|ggapi::StringOrd)$',
                   PartialGgApiSymbolPrinter)

    # Objects
    pp.add_printer('data::ObjHandle', '^data::ObjHandle$',
                   DataObjHandlePrinter)
    pp.add_printer('data::ObjHandle::Partial', '^data::ObjHandle::Partial$',
                   PartialDataHandlePrinter)
    pp.add_printer(
        'ggapi::ObjHandle',
        '^(ggapi::ObjHandle|ggapi::Container|ggapi::Struct|ggapi::List|ggapi::Buffer|ggapi::Task)$',
        PartialGgApiHandlePrinter)
    pp.add_printer(
        'data::StructModelBase',
        '^(data::StructModelBase|data::SharedStruct|config::Topic)$',
        DynamicStructPrinter)

    # Object Roots
    pp.add_printer('data::RootHandle', '^data::RootHandle$',
                   DataRootHandlePrinter)
    pp.add_printer('data::RootHandle::Partial', '^data::RootHandle::Partial$',
                   PartialRootHandlePrinter)

    # Tables
    pp.add_printer('data::SymbolTable', '^data::SymbolTable$',
                   DataSymbolTablePrinter)

    return pp


gdb.printing.register_pretty_printer(None,
                                     build_greengrass_printers(),
                                     replace=True)
Symbols()
Handles()
HandleRoots()
