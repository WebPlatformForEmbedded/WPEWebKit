#pragma once

#include "Logging.h"

#include <atomic>
#include <deque>

namespace WTF {
    class ConservativeScanStackGuards {
        public:
            struct ConservativeScanStackGuard {
                ConservativeScanStackGuard() {
                    ConservativeScanStackGuards::setStackGuard(this);
                }
                ~ConservativeScanStackGuard() {
                    ConservativeScanStackGuards::resetStackGuard(this);
                }

                private:
                    // Prevent heap allocation
                    void *operator new   (size_t);
                    void *operator new[] (size_t);
                    void  operator delete   (void *);
                    void  operator delete[] (void*);
            };

            friend struct ConservativeScanStackGuard;

            static void* updatedStackOrigin(void *stackTop, void *stackOrigin) {
                if (!active.load() || guard_ptr_stack.empty()) {
                    return stackOrigin;
                }

                void *ret = stackOrigin;

                void *guard_ptr = guard_ptr_stack.back();

                if (guard_ptr > stackTop && guard_ptr < stackOrigin) {
                    WTFLogAlways("ConservativeScanStackGuards: guard IN RANGE: stackTop: %p guard: %p stackOrigin: %p; correcting stackOrigin\n", stackTop, guard_ptr, stackOrigin);
                    ret = guard_ptr;
                }
                return ret;
            }

            static void setActive(bool act) {
                active.store(act);
            }

        private:

            static thread_local std::deque<void*> guard_ptr_stack;
            static std::atomic_bool active;

            static void setStackGuard(void *ptr) {
                guard_ptr_stack.push_back(ptr);
            }

            static void resetStackGuard(void *ptr) {
                if (ptr != guard_ptr_stack.back()) {
                    WTFLogAlways("ConservativeScanStackGuards::resetStackGuard expected %p, had: %p", guard_ptr_stack.back(), ptr);
                }
                guard_ptr_stack.pop_back();
            }
    };
}