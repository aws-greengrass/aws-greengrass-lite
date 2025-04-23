#!/usr/bin/env python3

import gdb
import gdb.printing


class GglBufferPrinter:

    def __init__(self, val):
        self.__val = val
        self.__len = val["len"]

    def to_string(self):
        return "GglBuffer"

    def num_children(self):
        return 2

    def child(self, n):
        return self.children()[n]

    def children(self):
        return [("data", self.__val["data"].string(length=self.__len)),
                ("len", self.__len)]


class GglListPrinter:

    def __init__(self, val):
        self.__val = val

    def display_hint(self):
        return "array"

    def to_string(self):
        return "GglList"

    def num_children(self):
        return self.__val["len"]

    def child(self, n):
        return (str(n), self.__val["items"][n])

    def children(self):
        return (self.child(i) for i in range(self.num_children()))


class GglMapPrinter:

    def __init__(self, val):
        self.__val = val

    def display_hint(self):
        return "array"

    def to_string(self):
        return "GglMap"

    def num_children(self):
        return self.__val["len"]

    def child(self, n):
        return (str(n), self.__val["pairs"][n])

    def children(self):
        return [self.child(i) for i in range(self.num_children())]


class GglObjectPrinter:

    def __init__(self, val):
        self.__val = val
        self.__key = None
        match str(val["type"]):
            case "GGL_TYPE_BOOLEAN":
                self.__key = "boolean"
            case "GGL_TYPE_I64":
                self.__key = "i64"
            case "GGL_TYPE_F64":
                self.__key = "f64"
            case "GGL_TYPE_BUF":
                self.__key = "buf"
            case "GGL_TYPE_LIST":
                self.__key = "list"
            case "GGL_TYPE_MAP":
                self.__key = "map"

    def to_string(self):
        return "GglObject"

    def num_children(self):
        return 2

    def child(self, n):
        return self.children()[n]

    def children(self):
        if self.__key is None:
            return [("type", self.__val["type"])]
        return [("type", self.__val["type"]),
                (self.__key, self.__val[self.__key])]


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("ggl-sdk")
    pp.add_printer('GglBuffer', '^GglBuffer$', GglBufferPrinter)
    pp.add_printer('GglList', '^GglList$', GglListPrinter)
    pp.add_printer('GglMap', '^GglMap$', GglMapPrinter)
    pp.add_printer('GglObject', '^GglObject$', GglObjectPrinter)
    return pp


gdb.printing.register_pretty_printer(gdb.current_objfile(),
                                     build_pretty_printer())
