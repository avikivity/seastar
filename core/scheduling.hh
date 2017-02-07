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
 * Copyright (C) 2016 Scylla DB Ltd
 */

#pragma once

#include <utility>
#include <type_traits>
#include "sstring.hh"

/// \file

template <typename... T>
class future;

class reactor;

namespace seastar {


/// \cond internal

class scheduling_group;

template <typename Func>
class scheduled_function;

namespace impl {

template <typename Func, typename... Args>
std::result_of_t<Func (Args...)> execute_in_scheduling_group(scheduling_group sg, Func&& func, Args&&... args);

}

/// \endcond

/// Creates a scheduling group with a specified number of shares.
///
/// \return a scheduling group that can be used on any shard
future<scheduling_group> create_scheduling_group(sstring name, unsigned shares);

/// \brief Identifies function calls that are accounted as a group
///
/// A `scheduling_group` is a tag that can be used to mark a function call.
/// Executions of such tagged calls are accounted as a group.
class scheduling_group {
    unsigned _id;

    template <typename Func>
    friend class scheduled_function;
private:
    explicit scheduling_group(unsigned id) : _id(id) {}
public:
    /// Creates a `scheduling_group` object denoting the default group
    scheduling_group() noexcept : _id(0) {}
    bool active() const;
    const sstring& name() const;
    friend future<scheduling_group> create_scheduling_group(sstring name, unsigned shares);
    friend class ::reactor;
};

/// \brief A function that is tagged with a scheduling group.
///
/// A `scheduled_function` is a wrapper around a callable that associates
/// it with a \ref scheduling_group.  The system may delay execution of the
/// function in accordance with its scheduling policy.
///
/// The wrapped callable must return a \ref future.
template <typename Func>
class scheduled_function {
    scheduling_group _scheduling_group;
    Func _func;
public:
    using wrapped_type = Func;
public:
    scheduled_function(scheduling_group sg, Func&& func)
            : _scheduling_group(sg), _func(std::move(func)) {
    }
    template <typename... Args>
    std::result_of_t<Func (Args...)> operator()(Args&&... args) && {
        return impl::execute_in_scheduling_group(_scheduling_group, std::move(_func), std::forward<Args>(args)...);
    }
    template <typename... Args>
    std::result_of_t<Func (Args...)> operator()(Args&&... args) & {
        return impl::execute_in_scheduling_group(_scheduling_group, _func, std::forward<Args>(args)...);
    }
    template <typename... Args>
    std::result_of_t<Func (Args...)> operator()(Args&&... args) const & {
        return impl::execute_in_scheduling_group(_scheduling_group, _func, std::forward<Args>(args)...);
    }
    scheduling_group get_scheduling_group() const { return _scheduling_group; }
    Func&& unwrap() && { return std::move(_func); }
    Func& unwrap() & { return _func; }
};

/// \brief Wraps a callable in a \ref scheduled_function.
///
/// Marks a callable as a CPU-intensive function that should be accounted
/// and scheduled to prevent it (or others in the same \ref scheduling_group)
/// from dominating the processor.
///
/// \param sg the scheduling group under which this function is accounted
/// \param func callable to execute under `sg`; must return a function
/// \return a \ref scheduled_function with the same signature as `func`,
///         but which will be accounted for and executed under `sg`
template <typename Func>
inline
scheduled_function<std::remove_reference_t<Func>>
with_scheduling_group(scheduling_group sg, Func&& func) {
    return scheduled_function<std::remove_reference_t<Func>>(sg, std::move(func));
}

/// \cond internal

namespace impl {


// Given a callable inner, which may be a scheduled_function, and a callable outer:
//
//   If inner is not a scheduled_function, returns a callable(args) that is equivalent to
//
//       outer(inner, args)
//
//   If inner is a scheduled_function, returns a scheduled_function(args) that is equivalent to
//
//       outer(inner.unwrap(), args)
//
//   and has the same scheduling group as inner
//
//  In other words, it peels the scheduled_function wrapper from inner, and applies it to outer.
//
//  If you don't have a headache now, consult a doctor.

template <typename OuterFunc, typename InnerFunc>
inline
auto
rebind_scheduled_function(InnerFunc&& inner, OuterFunc&& outer) {
    return [outer = std::forward<OuterFunc>(outer), inner = std::forward<InnerFunc>(inner)] (auto&&... args) mutable {
        return outer(std::forward<InnerFunc>(inner), std::forward<decltype(args)>(args)...);
    };
}

template <typename OuterFunc, typename InnerFunc>
inline
auto
rebind_scheduled_function(scheduled_function<InnerFunc>&& inner, OuterFunc&& outer) {
    return with_scheduling_group(inner.get_scheduling_group(), [outer = std::forward<OuterFunc>(outer), inner = std::forward<InnerFunc>(std::move(inner).unwrap())] (auto&&... args) mutable {
        return outer(std::move(inner), std::forward<decltype(args)>(args)...);
    });
}

template <typename T>
struct safe_capture_helper {
    using captured_type = T&&;
    static captured_type capture(T& v) { return std::move(v); }
};

template <typename T>
struct safe_capture_helper<T&> {
    using captured_type = std::reference_wrapper<T>;
    static captured_type capture(T& v) { return std::ref(v); }
};

template <typename Func>
typename safe_capture_helper<Func>::captured_type
safe_capture(std::remove_reference_t<Func>& func) {
    return safe_capture_helper<Func>::capture(func);
}

template <typename Func, typename... Args>
void execute_in_scheduling_group_deferred_void(scheduling_group sg, Func&& func, Args&&... args) {
    //print("really deferring: %s\n", __PRETTY_FUNCTION__);
    schedule(sg, make_task([func = safe_capture<Func>(func), args = std::make_tuple(std::move(args)...)] () mutable {
        //print("finally running: %s %s %s\n", __PRETTY_FUNCTION__, typeid(func).name(), typeid(args).name());
        apply(func, std::move(args));
    }));
}

template <typename Func, typename... Args>
auto execute_in_scheduling_group_deferred_future(scheduling_group sg, Func&& func, Args&&... args) {
    using result_type = std::result_of_t<Func (Args...)>;
    using promise_type = typename result_type::promise_type;
    promise_type pr;
    auto ret = pr.get_future();
    schedule(sg, make_task([pr = std::move(pr), func = safe_capture<Func>(func), qargs = std::make_tuple(std::move(args)...)] () mutable {
        //print("finally running: %s %s %s\n", __PRETTY_FUNCTION__, typeid(func).name(), typeid(qargs).name());
        //apply([] (auto&&... qqargs) { maybe_report_size(std::forward<decltype(qqargs)>(qqargs)...); }, qargs);
        apply(func, std::move(qargs)).forward_to(std::move(pr));
    }));
    return ret;
}

template <typename Result>
struct execute_in_scheduling_group_deferred_dispatcher;

template <>
struct execute_in_scheduling_group_deferred_dispatcher<void> {
    template <typename Func, typename... Args>
    static void go(scheduling_group sg, Func&& func, Args&&... args) {
        return execute_in_scheduling_group_deferred_void(sg, std::forward<Func>(func), std::forward<Args>(args)...);
    }
};

template <typename... T>
struct execute_in_scheduling_group_deferred_dispatcher<future<T...>> {
    template <typename Func, typename... Args>
    static future<T...> go(scheduling_group sg, Func&& func, Args&&... args) {
        return execute_in_scheduling_group_deferred_future(sg, std::forward<Func>(func), std::forward<Args>(args)...);
    }
};

template <typename Func, typename... Args>
std::result_of_t<Func (Args...)> execute_in_scheduling_group(scheduling_group sg, Func&& func, Args&&... args) {
    if (sg.active() && !need_preempt()) {
        return func(std::forward<Args>(args)...);
    } else {
        using result_type = std::result_of_t<Func (Args...)>;
        return execute_in_scheduling_group_deferred_dispatcher<result_type>::go(sg, std::forward<Func>(func), std::forward<Args>(args)...);
    }
}

}

/// \endcond

}
