#include "ConservativeScanStackGuards.h"

namespace WTF {
    std::atomic_bool ConservativeScanStackGuards::active {true};
    thread_local std::deque<void*>  ConservativeScanStackGuards::guard_ptr_stack;;
}