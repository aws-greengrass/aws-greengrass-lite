#pragma once
#include "span.hpp"

#include <cstdint>
#include <ios>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

namespace util {

    class MemoryBase : public std::streambuf {

    public:
    protected:
        std::streamsize showmanyc() override {
            return -1; // this is called only if in_avail() cannot compute
        }

        int overflow(int_type c) override {
            // called when buffer full
            return traits_type::eof();
        }

        int sync() override {
            return 0;
        }

    public:
        MemoryBase(const MemoryBase &) = default;
        MemoryBase(MemoryBase &&) noexcept = default;
        MemoryBase &operator=(const MemoryBase &) = default;
        MemoryBase &operator=(MemoryBase &&) noexcept = default;
        ~MemoryBase() override = default;
        MemoryBase() = default;

        uint64_t written() {
            if(pptr() == nullptr) {
                return 0;
            } else {
                return pptr() - pbase();
            }
        }
    };

    /**
     * Simple wrapper around a buffer for writing, reading is allowed with caution.
     */
    class MemoryWriter : public MemoryBase {

    public:
        MemoryWriter(const MemoryWriter &) = default;
        MemoryWriter(MemoryWriter &&) noexcept = default;
        MemoryWriter &operator=(const MemoryWriter &) = default;
        MemoryWriter &operator=(MemoryWriter &&) noexcept = default;
        ~MemoryWriter() override = default;

        explicit MemoryWriter(char *data, uint32_t len) {
            auto span = util::Span{data, len};
            setg(span.begin(), span.begin(), span.end());
            setp(span.begin(), span.end());
        }
    };

    /**
     * Simple wrapper around a buffer for reading, writing is prohibited.
     */
    class MemoryReader : public MemoryBase {
    public:
        MemoryReader(const MemoryReader &) = default;
        MemoryReader(MemoryReader &&) noexcept = default;
        MemoryReader &operator=(const MemoryReader &) = default;
        MemoryReader &operator=(MemoryReader &&) noexcept = default;
        ~MemoryReader() override = default;

        explicit MemoryReader(const char *data, uint32_t len) {
            // NOLINTNEXTLINE(*-pro-type-const-cast)
            auto span = util::Span{const_cast<char *>(data), len};
            setg(span.begin(), span.begin(), span.end());
            setp(nullptr, nullptr);
        }
    };

    /**
     * Given a buffer that implements a set of predefined operations, provides buffered
     * streaming operations around it. Implementation is lightweight and predominantly
     * to support << and >> operations and/or APIs that take streams.
     */
    template<typename Buffer>
    class BufferStreamBase : public std::streambuf {

        Buffer _buffer;
        pos_type _inPos{0}; // unbuffered position
        pos_type _outPos{0};
        std::vector<char> _inBuf;
        std::vector<char> _outBuf;
        static constexpr uint32_t BUFFER_SIZE{256};

        int32_t inAsInt(uint32_t limit = std::numeric_limits<int32_t>::max()) {
            if(_inPos > limit) {
                throw std::invalid_argument("Seek position beyond limit");
            }
            return static_cast<int32_t>(_inPos);
        }

        int32_t outAsInt(uint32_t limit = std::numeric_limits<int32_t>::max()) {
            if(_outPos > limit) {
                throw std::invalid_argument("Seek position beyond limit");
            }
            return static_cast<int32_t>(_outPos);
        }

        bool readMore() {

            flushRead();
            uint32_t pos = inAsInt();
            uint32_t end = _buffer.size();
            if(pos >= end) {
                return false;
            }
            std::vector<char> temp(std::min(end - pos, BUFFER_SIZE));
            auto didRead = _buffer.get(pos, temp);
            if(didRead > 0) {
                _inBuf = std::move(temp);
                auto span = util::Span{_inBuf};
                setg(span.begin(), span.begin(), span.end());
                return true;
            } else {
                return false;
            }
        }

        void flushWrite() {

            if(!_outBuf.empty()) {
                if(unflushed() > 0) {
                    int32_t pos = outAsInt();
                    _buffer.put(pos, util::Span{pbase(), unflushed()});
                    _outPos += unflushed();
                }
                _outBuf.clear();
                setp(nullptr, nullptr);
            }
        }

        void flushRead() {
            if(!_inBuf.empty()) {
                _inPos += consumed();
                _inBuf.clear();
                setg(nullptr, nullptr, nullptr);
            }
        }

        std::ptrdiff_t unflushed() {
            return pptr() - pbase();
        }

        std::ptrdiff_t unread() {
            return egptr() - gptr();
        }

        std::ptrdiff_t consumed() {
            return gptr() - eback();
        }

        pos_type eInPos() {
            return _inPos + static_cast<pos_type>(consumed());
        }

        void prepareWrite() {
            flushWrite();
            _outBuf.resize(BUFFER_SIZE);
            auto span = util::Span{_outBuf};
            setp(span.begin(), span.end());
        }

        pos_type seek(pos_type cur, off_type pos, std::ios_base::seekdir seekdir) {
            uint32_t end = _buffer.size();
            off_type newPos;

            switch(seekdir) {
                case std::ios_base::beg:
                    newPos = pos;
                    break;
                case std::ios_base::end:
                    newPos = end + pos;
                    break;
                case std::ios_base::cur:
                    newPos = cur + pos;
                    break;
                default:
                    throw std::invalid_argument("Seekdir is invalid");
            }
            if(newPos < 0) {
                newPos = 0;
            }
            if(newPos > end) {
                newPos = end;
            }
            return newPos;
        }

    protected:
        pos_type seekoff(
            off_type pos,
            std::ios_base::seekdir seekdir,
            std::ios_base::openmode openmode) override {
            bool _seekIn = (openmode & std::ios_base::in) != 0;
            bool _seekOut = (openmode & std::ios_base::out) != 0;
            if(_seekIn && _seekOut) {
                flushRead();
                flushWrite();
                _outPos = _inPos = seek(_outPos, pos, seekdir);
                return _outPos;
            }
            if(_seekIn) {
                flushRead();
                _inPos = seek(_inPos, pos, seekdir);
                return _inPos;
            }
            if(_seekOut) {
                flushWrite();
                _outPos = seek(_outPos, pos, seekdir);
                return _outPos;
            }
            return std::streambuf::seekoff(pos, seekdir, openmode);
        }

        pos_type seekpos(pos_type pos, std::ios_base::openmode openmode) override {
            return seekoff(pos, std::ios_base::beg, openmode);
        }

        std::streamsize showmanyc() override {
            pos_type end = _buffer.size();
            pos_type cur = eInPos();

            if(cur >= end) {
                return -1;
            } else {
                return end - cur;
            }
        }

        int underflow() override {
            // called when get buffer underflows
            readMore();
            if(unread() == 0) {
                return traits_type::eof();
            } else {
                return traits_type::to_int_type(*gptr());
            }
        }

        int pbackfail(int_type c) override {
            // called when put-back underflows
            flushRead();
            flushWrite();
            if(eInPos() == 0) {
                return traits_type::eof();
            }
            _inPos -= 1;
            if(traits_type::not_eof(c)) {
                auto cc = static_cast<char_type>(c);
                _buffer.put(inAsInt(), std::basic_string_view(&cc, 1));
                return c;
            } else {
                return 0;
            }
        }

        int overflow(int_type c) override {
            // called when buffer full
            prepareWrite(); // make room for data
            std::streambuf::overflow(c);
            if(traits_type::not_eof(c)) {
                // expected to write one character
                *pptr() = static_cast<char_type>(c);
                pbump(1);
            }
            return 0;
        }

        int sync() override {
            flushRead();
            flushWrite();
            return 0;
        }

    public:
        BufferStreamBase(const BufferStreamBase &) = default;
        BufferStreamBase(BufferStreamBase &&) noexcept = default;
        BufferStreamBase &operator=(const BufferStreamBase &) = default;
        BufferStreamBase &operator=(BufferStreamBase &&) noexcept = default;

        ~BufferStreamBase() override {
            try {
                flushWrite(); // attempt final flush if omitted
            } catch(...) {
                // destructor not allowed to throw exceptions
            }
        };

        explicit BufferStreamBase(const Buffer buffer) : _buffer(buffer) {
        }
    };

    template<typename BufferStream>
    class BufferInStreamBase : public std::istream {
        BufferStream _stream;

    public:
        explicit BufferInStreamBase(BufferStream buffer)
            : std::istream(&_stream), _stream(std::move(buffer)) {
        }
    };

    template<typename BufferStream>
    class BufferOutStreamBase : public std::ostream {
        BufferStream _stream;

    public:
        explicit BufferOutStreamBase(BufferStream buffer)
            : std::ostream(&_stream), _stream(std::move(buffer)) {
            _stream.pubseekoff(0, std::ios_base::end, std::ios_base::out);
        }
    };

} // namespace util
