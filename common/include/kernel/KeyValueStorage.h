#pragma once

#include "types.h"
#include "umap.h"
#include "Mutex.h"

class KeyValueStorage
{
public:
        KeyValueStorage();
        bool valueExists(size_t key);
        int setValue(size_t key, size_t value);
        int getValue(size_t key, size_t* value);
private:
        Mutex lock_;
        ustl::map<size_t, size_t> kv_;
};

extern KeyValueStorage global_kv_storage;
