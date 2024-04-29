import argparse
from sortedcontainers import SortedDict
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import (SymbolTableSection, SymbolTableIndexSection)
from elftools.elf.dynamic import (DynamicSection)
from elftools.elf.gnuversions import (GNUVerSymSection, GNUVerDefSection,
                                      GNUVerNeedSection)
from elftools.elf.constants import SHN_INDICES

try:
    import cxxfilt
except ImportError:
    cxxfilt = None

    def cxx_demangle(name):
        return name
else:

    def cxx_demangle(name):
        try:
            return cxxfilt.demangle(name, external_only=False)
        except cxxfilt.InvalidName:
            return name


TYPEINFO = "[typeinfo]"
VTABLE = "[vtable]"


class Symbol(object):

    def __init__(self, symbol, section, segment):
        self.symbol = symbol
        self.name = name = cxx_demangle(symbol.name)
        self.info = info = symbol['st_info']
        self.value = symbol['st_value']
        self.size = symbol['st_size']
        self.type = info['type']
        self.bind = info['bind']
        self.other = symbol['st_other']
        self.section = section
        self.segment = segment
        if name.startswith("typeinfo "):
            self.type = TYPEINFO
        elif name.startswith("vtable "):
            self.type = VTABLE
        else:
            self.type = section.name


class ParseElf(object):

    def __init__(self, file):
        self.elffile = ELFFile(file)
        self.symbols = SortedDict()
        self.rev_symbols = SortedDict()
        self.section_to_segment = {}
        self.name_to_section = {}
        self.shndx_sections = []
        self.symbol_tables = []
        self.ver_sym = None
        self.ver_def = None
        self.ver_need = None
        self.is_gnu_ver = False

    def _get_shndx(self, symbol, symbol_index, symtab_index):
        shndx = symbol['st_shndx']
        if shndx != SHN_INDICES.SHN_XINDEX:
            return shndx
        return self.shndx_sections[symtab_index].get_section_index(
            symbol_index)

    def _find_segment(self, section):
        if section.name in self.section_to_segment:
            return self.section_to_segment[section.name]
        for segment in self.elffile.iter_segments():
            if segment.section_in_segment(section):
                self.section_to_segment[section.name] = segment
                return segment
        return None

    def build_sections(self):
        for section in self.elffile.iter_sections():
            self.name_to_section[section.name] = section
            if isinstance(section, GNUVerSymSection):
                ver_sym = section
            elif isinstance(section, GNUVerDefSection):
                ver_def = section
            elif isinstance(section, GNUVerNeedSection):
                ver_need = section
            elif isinstance(section, DynamicSection):
                for tag in section.iter_tags():
                    if tag['d_tag'] == 'DT_VERSYM':
                        is_gnu_ver = True
            elif isinstance(section, SymbolTableIndexSection):
                self.shndx_sections.append(section)
            elif isinstance(section, SymbolTableSection):
                self.symbol_tables.append(section)

    def build_symbols(self):
        for nsec, section in enumerate(self.symbol_tables):
            entsize = section['sh_entsize']
            if entsize == 0:
                continue

            for nsym, symbol in enumerate(section.iter_symbols()):
                shndx = self._get_shndx(symbol, nsym, nsec)
                if isinstance(shndx, int):
                    symsec = self.elffile.get_section(shndx)
                    symseg = self._find_segment(symsec)
                    sym = Symbol(symbol, symsec, symseg)
                    self.symbols[sym.value] = sym
                    self.rev_symbols[sym.name] = sym

    def dump_sections(self):
        if self.elffile.num_sections() == 0:
            print("There are no sections!")
            return

        print("Sections:")
        for nsec, section in enumerate(self.elffile.iter_sections()):
            section_type = section['sh_type']
            addr = section['sh_addr']
            offset = section['sh_offset']
            size = section['sh_size']
            entsize = section['sh_entsize']
            flags = section['sh_flags']
            link = section['sh_link']
            info = section['sh_info']
            print("[%2u] %-17s %-15s 0x%08x 0x%08x 0x%04x 0x%02x %s %s %s" %
                  (nsec, section.name, section_type, addr, offset, size,
                   entsize, flags, link, info))

    def dump_segments(self):
        if self.elffile.num_segments() == 0:
            print("There are no segments!")
            return

        print("Segments:")
        for nseg, segment in enumerate(self.elffile.iter_segments()):
            segment_type = segment['p_type']
            offset = segment['p_offset']
            vaddr = segment['p_vaddr']
            paddr = segment['p_paddr']
            filesz = segment['p_filesz']
            memsz = segment['p_memsz']
            flags = segment['p_flags']
            align = segment['p_align']
            print("[%2u] %-15s 0x%04x 0x%8x 0x%8x 0x%4x 0x%4x 0x%04x 0x%04x" %
                  (nseg, segment_type, offset, vaddr, paddr, filesz, memsz,
                   flags, align))

    def dump_symbols_by_address(self):

        print("Symbols by address:")

        for val, symbol in self.symbols.items():
            print("0x%08x %5d %-7s %-6s %s %s" %
                  (symbol.value, symbol.size, symbol.type, symbol.bind,
                   symbol.section.name, symbol.name))

    def dump_symbols_by_name(self):

        print("Symbols by name:")

        for val, symbol in self.symbols.items():
            print("%s %5d 0x%08x %-7s %-6s %s" %
                  (symbol.name, symbol.size, symbol.value, symbol.type,
                   symbol.bind, symbol.section.name))

    def section_size(self, name):
        if name in self.name_to_section:
            return self.name_to_section[name]['sh_size']
        else:
            return 0

    def segment_size(self, name):
        if name in self.section_to_segment:
            return self.section_to_segment[name]['p_memsz']
        else:
            return 0

    @staticmethod
    def to_kilo(count):
        return float(count) / 1024.0

    @staticmethod
    def to_meg(count):
        return ParseElf.to_kilo(count) / 1024.0

    @staticmethod
    def to_sizes(count):
        return "%8.4fK %8.4fM" % (ParseElf.to_kilo(count),
                                  ParseElf.to_meg(count))

    def vtable_est(self):
        total = 0
        for syms in self.symbols.values():
            if syms.type == VTABLE:
                total += syms.size
        return total

    def typeinfo_est(self):
        total = 0
        for syms in self.symbols.values():
            if syms.type == TYPEINFO:
                total += syms.size
        return total

    def dump_summaries(self):
        code_size = self.section_size('.text')
        plt_size = self.section_size('.plt') + self.section_size('.plt.got')
        ro_size = self.section_size('.rodata') + self.section_size(
            '.data.rel.ro')
        bss_size = self.section_size('.bss')
        except_size = (self.section_size('.eh_frame_hdr') +
                       self.section_size('.eh_frame') +
                       self.section_size('.gcc_except_table'))
        vtable_est_size = self.vtable_est()
        typeinfo_est_size = self.typeinfo_est()

        code_seg = self.segment_size('.text')
        ro_data_seg = self.segment_size('.rodata')
        ro_reloc_data_seg = self.segment_size('.data.rel.ro')
        bss_seg = self.segment_size('.bss')

        print("Code size:         %s" % self.to_sizes(code_size))
        print("Lazy trampoline:   %s" % self.to_sizes(plt_size))
        print("Read only data:    %s" % self.to_sizes(ro_size))
        print("Uninit data:       %s" % self.to_sizes(bss_size))
        print("Exception data:    %s" % self.to_sizes(except_size))
        print("")
        print("TypeInfo est size: %s" % self.to_sizes(typeinfo_est_size))
        print("vTable est size:   %s" % self.to_sizes(vtable_est_size))
        print("")
        print("Code segment:    %s" % self.to_sizes(code_seg))
        print("RO segment:      %s" % self.to_sizes(ro_data_seg))
        print("RO Relo segment: %s" % self.to_sizes(ro_reloc_data_seg))
        print("BSS segment:     %s" % self.to_sizes(bss_seg))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Analyze image sizes")
    parser.add_argument("--file",
                        action="store",
                        help="file to analyze",
                        required=True)
    args = parser.parse_args()
    with open(args.file, 'rb') as f:
        p = ParseElf(f)
        p.build_sections()
        p.build_symbols()
        p.dump_summaries()
        p.dump_sections()
        p.dump_segments()
        p.dump_symbols_by_address()
        p.dump_symbols_by_name()
