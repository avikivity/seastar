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
 * Copyright 2016 ScyllaDB
 */

#pragma once

#include "util/branch.hh"
#include <exception>
#include <utility>
#include <functional>

namespace seastar {

class exception_ptr {
    struct record {
        std::exception_ptr ep;
        record* next = nullptr;
        bool is_static = false;
        static record bad_alloc;
        static record bad_exception;
    };
private:
    record* _record = nullptr;
private:
    static record*& pending_deletion() {
        static thread_local record* pd = nullptr;
        return pd;
    }
    static record*& pending_report() {
        static thread_local record* pr = nullptr;
        return pr;
    }
public:
    exception_ptr() = default;
    exception_ptr(std::exception_ptr ep) noexcept;
    exception_ptr(const exception_ptr&) noexcept;
    exception_ptr(exception_ptr&& ep) noexcept : _record(std::exchange(ep._record, nullptr)) {}
    ~exception_ptr() {
        if (unlikely(_record) && !_record->is_static) {
            auto& pd = pending_deletion();
            _record->next = pd;
            pd = _record;
        }
    }
    exception_ptr& operator=(const exception_ptr&) noexcept;
    exception_ptr& operator=(exception_ptr&& ep) noexcept {
        std::swap(_record, ep._record);
        return *this;
    }
    explicit operator bool() const noexcept {
        return _record;
    }
    operator std::exception_ptr() const noexcept;
    void report_lazy() && {
        auto& pr = pending_report();
        _record->next = pr;
        pr = _record;
        _record = nullptr;
    }
public:
    static void delete_local_pending();
    static void process_lazy_reports(std::function<void (std::exception_ptr)> f);
};

}
