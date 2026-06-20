#include "syminfo.h"

#include <QMutex>
#include <memory>

std::unique_ptr<sym_list> sym_list::instance = nullptr;

sym_list::sym_list()
{
    commentRegions.reserve(100);
}

sym_list::~sym_list() = default;

sym_list* sym_list::getInstance()
{
    static QMutex instanceMutex;
    QMutexLocker lock(&instanceMutex);
    if (!instance) {
        instance = std::unique_ptr<sym_list>(new sym_list());
    }
    return instance.get();
}
