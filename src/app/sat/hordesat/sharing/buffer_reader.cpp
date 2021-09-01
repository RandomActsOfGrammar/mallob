
#include "buffer_reader.hpp"
#include "util/hashing.hpp"
#include "util/logger.hpp"

BufferReader::BufferReader(int* buffer, int size, int maxLbdPartitionedSize, bool useChecksum) : 
        _buffer(buffer), _size(size), 
        _max_lbd_partitioned_size(maxLbdPartitionedSize), _use_checksum(useChecksum) {
    
    int numInts = sizeof(size_t)/sizeof(int);
    if (_use_checksum && _size > 0) {
        // Extract checksum
        assert(size >= numInts);
        memcpy(&_true_hash, _buffer, sizeof(size_t));
    }

    _remaining_cls_of_bucket = _size <= numInts ? 0 : _buffer[numInts];
    _current_pos = numInts+1;
    _hash = 1;
}

Mallob::Clause BufferReader::getNextIncomingClause() {
    Mallob::Clause cls {nullptr, 0, 0};

    // Find appropriate clause size
    while (_remaining_cls_of_bucket == 0) {
        
        // Nothing left to read?
        if (_current_pos >= _size) {
            // Verify checksum
            if (_use_checksum && _hash != _true_hash) {
                log(V0_CRIT, "[ERROR] Checksum fail\n");
                abort();
            }
            return cls;
        }

        // Go to next bucket
        _bucket.next(_max_lbd_partitioned_size);
        _remaining_cls_of_bucket = _buffer[_current_pos++];
    }

    if (_current_pos >= _size) return cls;
    bool partitionedByLbd = _bucket.size <= _max_lbd_partitioned_size;
    assert(_buffer[_current_pos] != 0);

    // Get start and stop index of next clause
    int start = _current_pos;
    int stop = start + _bucket.size;
    if (stop+(partitionedByLbd ? 0 : 1) > _size) return cls;

    if (_use_checksum) {
        hash_combine(_hash, Mallob::ClauseHasher::hash(
            _buffer+start,
            _bucket.size+(partitionedByLbd ? 0 : 1), 3
        ));
    }

    if (partitionedByLbd) {
        // set LBD value inferred from buffer structure
        cls.lbd = _bucket.lbd;
    } else {
        // read explicit LBD value
        cls.lbd = _buffer[start];
        start++;
        stop++;
    }
    _current_pos = stop;
    _remaining_cls_of_bucket--;

    // Return clause
    cls.begin = _buffer+start;
    cls.size = _bucket.size;
    return cls;
}