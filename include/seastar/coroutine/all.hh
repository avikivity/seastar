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
 * Copyright (C) 2021-present ScyllaDB
 */

#pragma once

#include <concepts>
#include <type_traits>
#include <seastar/core/coroutine.hh>

namespace seastar::coroutine {

template <typename Future>
constexpr inline bool is_future_v = is_future<Future>::value;

template <typename Future>
concept future_type = is_future_v<Future>;

namespace internal {


}

template <typename... Futures>
class all {
    using tuple = std::tuple<Futures...>;
    tuple _futures;
    unsigned _nr_waiting;
private:
    template <unsigned idx>
    struct intermediate_task : continuation_base<typename std::tuple_element_t<idx, tuple>::value_type> {
        awaiter& container;
        virtual void run_and_dispose() noexcept {
            std::get<idx>(container.state._futures) = std::move(_state);
            this->~intermediate_task();
            container.process<idx+1>();
        }
    };

    struct awaiter {
        all& state;
        SEASTAR_INTERNAL_COROUTINE_NAMESPACE::coroutine_handle<> when_ready;
        awaiter(all& state) : state(state) {}
        bool await_ready() const { return !_nr_waiting; }
        tuple await_resum
        template <unsigned idx>
        void process() {
            if constexpr (idx == std::tuple_size(tuple)) {
                when_ready.resume();
            }
        }
    };
public:
    template <typename... Func>
    requires (... && std::invocable<Func>) && (... && future_type<std::invoke_result_t<Func>>)
    explicit all(Func&&... funcs)
            : _futures(futurize_invoke(funcs)...)
            , _nr_waiting(std::apply([] (future_type auto&... fs) { return (... + unsigned(!fs.available())); }, _futures)) {
    }
    awaiter operator co_await() { return awaiter{*this}; }
};

template <typename... Func>
explicit all(Func&&... funcs) -> all<std::invoke_result_t<Func>...>;

}