/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright (C) 2016 ScyllaDB.
 */

#pragma once
#include <atomic>

namespace seastar {

extern __thread bool g_need_preempt;

inline bool need_preempt() {
#ifndef SEASTAR_DEBUG
    // prevent compiler from eliminating loads in a loop
    std::atomic_signal_fence(std::memory_order_seq_cst);
    return __builtin_expect(g_need_preempt, false);
#else
    return true;
#endif
}

// A variant of \ref need_preempt() that's only true in debug mode. This
// is used to glue two short continuations: in release mode we assume they
// are short and that preemption checks elsewhere will detect the need to
// preempt, and in debug mode we always preempt in order to detect object
// lifetime problems caused by breaking continuations into tasks.
inline bool debug_need_preempt() {
#ifndef SEASTAR_DEBUG
    return false;
#else
    return true;
#endif
}

}
