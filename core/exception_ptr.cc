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

#include "exception_ptr.hh"
#include <memory>

namespace seastar {

exception_ptr::record exception_ptr::record::bad_alloc{std::make_exception_ptr(std::bad_alloc()), nullptr, true};

exception_ptr::record exception_ptr::record::bad_exception{std::make_exception_ptr(std::bad_exception()), nullptr, true};

exception_ptr::exception_ptr(std::exception_ptr ep) noexcept {
    if (ep) {
        try {
            _record = new record{std::move(ep), nullptr};
        } catch (std::bad_alloc&) {
            _record = &record::bad_alloc;
        }
    }
}

exception_ptr::exception_ptr(const exception_ptr& ep) noexcept {
    if (ep) {
        try {
            _record = new record{ep._record->ep};
        } catch (std::bad_alloc&) {
            _record = &record::bad_alloc;
        }
    }
}

exception_ptr&
exception_ptr::operator=(const exception_ptr& ep) noexcept {
    return *this = exception_ptr(ep);
}

exception_ptr::operator std::exception_ptr() const noexcept {
    if (_record) {
        return _record->ep;
    } else {
        return std::exception_ptr();
    }
}

void
exception_ptr::delete_local_pending() {
    auto* pd = std::exchange(pending_deletion(), nullptr);
    while (pd) {
        delete std::exchange(pd, pd->next);
    }
}

void
exception_ptr::process_lazy_reports(std::function<void (std::exception_ptr)> func) {
    auto* pr = std::exchange(pending_report(), nullptr);
    while (pr) {
        auto record = std::exchange(pr, pr->next);
        func(std::move(record->ep));
        delete record;
    }
}


}
