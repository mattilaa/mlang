#pragma once
#include "base/source/fobject.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <array>

namespace mlacker {
using namespace Steinberg;
using namespace Steinberg::Vst;
// Shared bounded pool: at most 512 parameter points per processing block.
struct ParameterPool {
    struct Point { int32 offset = 0, next = -1; ParamValue value = 0; };
    std::array<Point, 512> points{};
    int32 used = 0;
};
class ParameterQueue final : public FObject, public IParamValueQueue {
public:
    ParameterPool *pool = nullptr;
    ParamID id = kNoParamId;
    int32 first = -1, last = -1, count = 0;
    DEFINE_INTERFACES
        DEF_INTERFACE(IParamValueQueue)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return count; }
    tresult PLUGIN_API getPoint(int32 index, int32 &offset, ParamValue &value) override {
        if(index < 0 || index >= count) return kInvalidArgument;
        int32 point = first;
        for(int32 i = 0; i < index; ++i) point = pool->points[point].next;
        offset = pool->points[point].offset; value = pool->points[point].value;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 offset, ParamValue value, int32 &index) override {
        if(last >= 0 && pool->points[last].offset == offset) {
            pool->points[last].value = value; index = count - 1; return kResultOk;
        }
        if(pool->used >= 512) return kResultFalse;
        int32 next = pool->used++;
        pool->points[next] = {offset, -1, value};
        if(last >= 0) pool->points[last].next = next; else first = next;
        last = next; index = count++; return kResultOk;
    }
};
class ParameterChanges final : public FObject, public IParameterChanges {
    ParameterPool pool;
    std::array<ParameterQueue, 512> queues;
    int32 count = 0;
public:
    DEFINE_INTERFACES
        DEF_INTERFACE(IParameterChanges)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
    void clear() { count = 0; pool.used = 0; }
    int32 PLUGIN_API getParameterCount() override { return count; }
    IParamValueQueue *PLUGIN_API getParameterData(int32 index) override {
        return index >= 0 && index < count ? &queues[index] : nullptr;
    }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &id, int32 &index) override {
        for(int32 i = 0; i < count; ++i) if(queues[i].id == id) { index = i; return &queues[i]; }
        if(count >= 512) return nullptr;
        index = count++;
        auto &q = queues[index]; q.pool = &pool; q.id = id;
        q.first = q.last = -1; q.count = 0; return &q;
    }
};
}
