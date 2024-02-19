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

    def index_of_partial(self, partial):
        gdb.set_convenience_variable('arg', partial)
        return self.buffer_eval("indexOf($arg)")

    def partial_of_index(self, index):
        return self.buffer_eval("symbolOf(%d)" % index)

    def id_of_partial(self, partial):
        return partial['_asInt']

    @staticmethod
    def partial_of_id(id):
        # Create typed partial handle
        id = int(id)
        partial_type = gdb.lookup_type('data::Symbol::Partial')
        id_bytes = (id).to_bytes(4, sys.byteorder)
        return gdb.Value(id_bytes, partial_type)

    def symbol_of_partial(self, partial):
        gdb.set_convenience_variable('arg', partial)
        return self.eval('applyUnchecked($arg)')

    def pretty_from_partial(self, partial):
        id = self.id_of_partial(partial)
        if id == 0:
            return 'Symbol{0}'
        if not self.is_valid(partial):
            return '!error!'
        str = self.string_view(partial)
        return ('Symbol{%d=%s}' % (id, str))


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

    def handles_eval(self, expr):
        gdb.set_convenience_variable('obj', self.handles())
        return gdb.parse_and_eval("$obj->" + str(expr))

    @staticmethod
    def partial_of_id(id):
        # Create typed partial handle
        id = int(id)
        partial_type = gdb.lookup_type('data::ObjHandle::Partial')
        id_bytes = (id).to_bytes(4, sys.byteorder)
        return gdb.Value(id_bytes, partial_type)

    def to_object(self, partial):
        gdb.set_convenience_variable('arg', partial)
        obj = self.eval('tryGet($arg).getBase().get()').cast(
            gdb.lookup_type('data::TrackedObject*'))
        return obj.cast(obj.dynamic_type)

    def to_root(self, partial):
        gdb.set_convenience_variable('arg', partial)
        return self.eval('tryGet($arg).getRoot().get()').cast(
            gdb.lookup_type('data::TrackingRoot*'))

    def is_valid(self, partial):
        gdb.set_convenience_variable('arg', partial)
        return bool(self.eval('isObjHandleValid($arg)'))


class SymbolPrinter(object):

    def __init__(self, table, partial):
        self._table = table
        self._partial = partial

    def to_string(self):
        return self._table.pretty_from_partial(self._partial)

    def to_simple_string(self):
        return str(self._table.string_view(self._partial))

    def display_hint(self):
        return None


class DataSymbolPrinter(SymbolPrinter):

    def __init__(self, val):
        table = SymbolTable(val['_table']['_p'])
        partial = val['_partial']
        super(DataSymbolPrinter, self).__init__(table, partial)


class PartialDataSymbolPrinter(SymbolPrinter):

    def __init__(self, val):
        super(PartialDataSymbolPrinter, self).__init__(SymbolTable(None), val)


class PartialGgApiSymbolPrinter(SymbolPrinter):

    def __init__(self, val):
        id = val['_asInt']
        partial = SymbolTable.partial_of_id(id)
        super(PartialGgApiSymbolPrinter,
              self).__init__(SymbolTable(None), partial)


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
            partial = self._table.partial_of_index(index)
            elt = self._table.symbol_of_partial(partial)
            return ('[%d]' % index, elt)

    def __init__(self, val):
        self._val = val

    def size(self):
        stringTable = SymbolTable(self._val)
        return stringTable.size()

    def to_string(self):
        return ('data::SymbolTable of size %d' % self.size())

    def children(self):
        return self.Iterator(SymbolTable(self._val))

    def display_hint(self):
        return 'array'


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


class AnchorIterator(object):
    """Iterator to expand anchor"""

    def __init__(self, table, partial):
        self._table = table
        self._partial = partial
        self._index = 0

    def __iter__(self):
        return self

    def __next__(self):
        (index, self._index) = (self._index, self._index + 1)
        match index:
            case 0:
                return 'handle', 'ObjHandle{%d}' % int(
                    self._partial['_asInt'])  # helps CLion
            case 1:
                return 'object', self._table.to_object(
                    self._partial).dereference()
            case 2:
                return 'root', self._table.to_root(self._partial)
            case _:
                raise StopIteration


class ObjHandlePrinter(object):

    def __init__(self, table, partial, type):
        self._table = table
        self._partial = partial
        self._id = partial['_asInt']
        self._type = type

    def to_string(self):
        return '%s{%d}' % (str(self._type), self._id)

    def children(self):
        if not self._table.is_valid(self._partial):
            return None
        return AnchorIterator(self._table, self._partial)


class DataObjHandlePrinter(ObjHandlePrinter):

    def __init__(self, val):
        table = HandleTable(val['_table']['_p'])
        partial = val['_partial']
        super(DataObjHandlePrinter, self).__init__(table, partial, val.type)


class PartialDataHandlePrinter(ObjHandlePrinter):

    def __init__(self, val):
        super(PartialDataHandlePrinter, self).__init__(HandleTable(None), val,
                                                       val.type)


class PartialGgApiHandlePrinter(ObjHandlePrinter):

    def __init__(self, val):
        id = val['_handle']
        partial = HandleTable.partial_of_id(id)
        super(PartialGgApiHandlePrinter,
              self).__init__(HandleTable(None), partial, val.type)


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
            match val_type:
                case 0:  # NONE
                    val = '[null]'
                case 1:  # BOOL
                    val = self._struct.eval('get($arg).getBool()')
                case 2:  # INT
                    val = self._struct.eval('get($arg).getInt()')
                case 3:  # DOUBLE
                    val = self._struct.eval('get($arg).getDouble()')
                case 4:  # STRING, SYMBOL
                    val = self._struct.eval('get($arg).rawGetString()')
                case 5:  # STRING, SYMBOL
                    val = self._struct.eval('get($arg).rawGetSymbol()')
                # case 4 | 5:  # STRING, SYMBOL
                #     val = self._struct.eval('get($arg).getString()')
                case 6:  # OBJECT
                    obj = self._struct.eval(
                        'get($arg).getObject().get()').cast(
                        gdb.lookup_type('data::TrackedObject*'))
                    val = obj.cast(obj.dynamic_type).dereference()
                case _:
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
        args = arg.split()
        if len(args) == 0:
            table = DataSymbolTablePrinter(
                gdb.parse_and_eval('&scope::context().get()->symbols()'))
            for k in table.children():
                print(k[0], k[1])
            return None
        else:
            table = SymbolTable(
                gdb.parse_and_eval('&scope::context().get()->symbols()'))
            id = int(args[0])
            print(str(table.pretty_from_partial(table.partial_of_id(id))))
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

    # Tables
    pp.add_printer('data::SymbolTable', '^data::SymbolTable$',
                   DataSymbolTablePrinter)

    return pp


gdb.printing.register_pretty_printer(None,
                                     build_greengrass_printers(),
                                     replace=True)
Symbols()
