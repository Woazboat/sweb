#include "KeyValueStorage.h"
#include "debug.h"
#include "assert.h"
#include "Scheduler.h"

KeyValueStorage global_kv_storage;

KeyValueStorage::KeyValueStorage() :
        lock_("KeyValueStorage lock"),
        kv_()
{
        debug(KV_STORAGE, "KeyValueStorage ctor\n");
}

bool KeyValueStorage::valueExists(size_t key)
{
        assert(lock_.isHeldBy(currentThread));
        return kv_.find(key) != kv_.end();
}

int KeyValueStorage::setValue(size_t key, size_t value)
{
        lock_.acquire();
        debug(KV_STORAGE, "KeyValueStorage setValue, key: %zx, value: %zx\n", key, value);

        if(valueExists(key))
        {
                lock_.release();
                return -1;
        }

        kv_[key] = value;

        lock_.release();
        return 0;
}

int KeyValueStorage::getValue(size_t key, size_t* value)
{
        MutexLock l(lock_);
        debug(KV_STORAGE, "KeyValueStorage getValue, key: %zx, value ptr: %p\n", key, value);
        // map[] creates a new element if it does not yet exist, map.at() causes an excaption (=kernel panic, because exceptions are not supported in the kernel), so we have to use map.find()
        auto iterator = kv_.find(key);
        if(iterator == kv_.end())
        {
                return -1;
        }

        *value = iterator->second;
        return 0;
}
