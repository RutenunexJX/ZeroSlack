#ifndef SEMANTICSHARDDIRECTORY_H
#define SEMANTICSHARDDIRECTORY_H

#include <QHash>
#include <array>
#include <memory>

// Copying a publication copies only the fixed directory. An update detaches
// the touched bucket, never every entry in a workspace-wide lookup table.
template<class Key, class Value, size_t BucketCount = 256>
class SemanticShardDirectory {
    using Bucket = QHash<Key, Value>;
    std::array<std::shared_ptr<Bucket>, BucketCount> buckets{};
    static size_t slot(const Key& key) { return size_t(qHash(key)) % BucketCount; }
    Bucket& writable(const Key& key) {
        auto& bucket = buckets[slot(key)];
        if (!bucket)
            bucket = std::make_shared<Bucket>();
        else if (!bucket.unique())
            bucket = std::make_shared<Bucket>(*bucket);
        return *bucket;
    }
public:
    Value value(const Key& key, const Value& fallback = Value{}) const {
        const auto& bucket = buckets[slot(key)];
        return bucket ? bucket->value(key, fallback) : fallback;
    }
    bool contains(const Key& key) const {
        const auto& bucket = buckets[slot(key)];
        return bucket && bucket->contains(key);
    }
    void insert(const Key& key, const Value& value) { writable(key).insert(key, value); }
    // Detach the bucket and its value only on the first modification of a
    // shared publication. Repeated membership changes must not copy a growing
    // set out of the directory and then detach it on every insertion.
    template<class Mutation> void mutate(const Key& key, Mutation&& mutation) {
        auto& bucket = writable(key);
        auto it = bucket.find(key);
        if (it == bucket.end())
            it = bucket.insert(key, Value{});
        if (!mutation(it.value()))
            bucket.erase(it);
    }
    void remove(const Key& key) {
        if (contains(key))
            writable(key).remove(key);
    }
    template<class Visitor> void forEach(Visitor&& visitor) const {
        for (const auto& bucket : buckets)
            if (bucket)
                for (auto it = bucket->cbegin(); it != bucket->cend(); ++it)
                    visitor(it.key(), it.value());
    }
};

#endif
