#include "shared_buffer.hpp"

namespace data {
    void SharedBuffer::put(int32_t idx, ConstMemoryView bytes) {
        putOrInsert(idx, bytes, false);
    }

    void SharedBuffer::insert(int32_t idx, ConstMemoryView bytes) {
        putOrInsert(idx, bytes, true);
    }

    void SharedBuffer::putOrInsert(int32_t idx, ConstMemoryView bytes, bool insert) {
        std::unique_lock guard{_mutex};
        size_t realIdx;
        if(idx < 0) {
            if(insert) {
                idx++; // for insert, -1 is end, so re-adjust
            }
            realIdx = _buffer.size() + idx;
        } else {
            realIdx = idx;
        }
        if(realIdx > _buffer.size()) {
            throw std::out_of_range("Put index out of range");
        }
        if(bytes.size() == 0) {
            // No copy required, but bounds check above still required
            return;
        }
        size_t dataEndIdx = realIdx + bytes.size();
        size_t priorBufferSize = _buffer.size();
        size_t newBufferSize = priorBufferSize;
        if(insert) {
            newBufferSize += bytes.size(); // always increase buffer
        } else if(newBufferSize < dataEndIdx) {
            newBufferSize = dataEndIdx; // increase on demand
        }
        if(newBufferSize > MAX_BUFFER_SIZE) {
            throw std::out_of_range("Buffer size too large");
        }
        if(priorBufferSize != newBufferSize) {
            // after size check
            _buffer.resize(newBufferSize, 0);
        }
        if(insert) {
            // need to move data to make room for insert
            auto destI = _buffer.rbegin();
            auto srcI = destI;
            auto dEnd = destI;
            std::advance(srcI, newBufferSize - priorBufferSize);
            std::advance(dEnd, priorBufferSize - realIdx);
            for(; destI != dEnd; ++srcI, ++destI) {
                *destI = *srcI;
            }
        }
        auto start = _buffer.begin();
        std::advance(start, realIdx);
        auto end = _buffer.begin();
        std::advance(end, dataEndIdx);
        bytes.copyTo(start, end);
    }

    uint32_t SharedBuffer::size() const {
        return _buffer.size();
    }

    void SharedBuffer::resize(uint32_t newSize) {
        std::unique_lock guard{_mutex};
        if(newSize > MAX_BUFFER_SIZE) {
            throw std::out_of_range("Buffer size too large");
        }
        _buffer.resize(newSize, 0);
    }

    uint32_t SharedBuffer::get(int idx, MemoryView bytes) const {
        std::shared_lock guard{_mutex};
        size_t realIdx;
        if(idx < 0) {
            realIdx = _buffer.size() + idx;
        } else {
            realIdx = idx;
        }
        if(realIdx > _buffer.size()) {
            throw std::out_of_range("Get index out of range");
        }
        size_t dataEndIdx = realIdx + bytes.size();
        if(dataEndIdx > bytes.size()) {
            dataEndIdx = bytes.size();
        }
        auto start = _buffer.begin();
        std::advance(start, realIdx);
        auto end = _buffer.begin();
        std::advance(end, dataEndIdx);
        bytes.copyFrom(start, end);
        return dataEndIdx - realIdx;
    }

} // namespace data
