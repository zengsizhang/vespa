// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "compacter.h"
#include "logdatastore.h"
#include <vespa/log/log.h>

LOG_SETUP(".searchlib.docstore.compacter");

namespace search {
namespace docstore {

void
Compacter::write(LockGuard guard, uint32_t chunkId, uint32_t lid, const void *buffer, size_t sz) {
    (void) chunkId;
    FileChunk::FileId fileId= _ds.getActiveFileId(guard);
    _ds.write(guard, fileId, lid, buffer, sz);
}

BucketCompacter::BucketCompacter(LogDataStore & ds, const IBucketizer & bucketizer, FileId source, FileId destination) :
    _sourceFileId(source),
    _destinationFileId(destination),
    _ds(ds),
    _bucketizer(bucketizer),
    _writeCount(0),
    _tmpStore(256),
    _lidGuard(ds.getLidReadGuard()),
    _bucketizerGuard(bucketizer.getGuard()),
    _stat()
{ }

FileChunk::FileId
BucketCompacter::getDestinationId(const LockGuard & guard) const {
    return (_destinationFileId.isActive()) ? _ds.getActiveFileId(guard) : _destinationFileId;
}

void
BucketCompacter::write(LockGuard guard, uint32_t chunkId, uint32_t lid, const void *buffer, size_t sz)
{
    _writeCount++;
    guard.unlock();
    BucketId bucketId = (sz > 0) ? _bucketizer.getBucketOf(_bucketizerGuard, lid) : BucketId();
    uint64_t sortableBucketId = bucketId.toKey();
    _tmpStore[(sortableBucketId >> 56) % _tmpStore.size()].add(bucketId, chunkId, lid, buffer, sz);
    if ((_writeCount % 1000) == 0) {
        _bucketizerGuard = _bucketizer.getGuard();
    }
}

void
BucketCompacter::close()
{
    _bucketizerGuard = GenerationHandler::Guard();
    size_t lidCount1(0);
    size_t bucketCount(0);
    size_t chunkCount(0);
    for (const StoreByBucket & store : _tmpStore) {
        lidCount1 += store.getLidCount();
        bucketCount += store.getBucketCount();
        chunkCount += store.getChunkCount();
    }
    LOG(info, "Have read %ld lids and placed them in %ld buckets. Temporary compressed in %ld chunks.",
              lidCount1, bucketCount, chunkCount);

    for (StoreByBucket & store : _tmpStore) {
        store.drain(*this);
    }

    size_t lidCount(0);
    for (const auto & it : _stat) {
        lidCount += it.second;
    }
    LOG(info, "Compacted %ld lids into %ld buckets", lidCount, _stat.size());
}

void
BucketCompacter::write(BucketId bucketId, uint32_t chunkId, uint32_t lid, const void *buffer, size_t sz)
{
    _stat[bucketId.getId()]++;
    LockGuard guard(_ds.getLidGuard(lid));
    LidInfo lidInfo(_sourceFileId.getId(), chunkId, sz);
    if (_ds.getLid(_lidGuard, lid) == lidInfo) {
        FileId fileId = getDestinationId(guard);
        _ds.write(guard, fileId, lid, buffer, sz);
    }
}

}
}
