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


#include "../core/app-template.hh"
#include "../core/future.hh"
#include "../core/scheduling.hh"
#include "../core/thread.hh"
#include "../core/future-util.hh"
#include "../core/reactor.hh"
#include <chrono>
#include <cmath>

using namespace std::chrono_literals;

template <typename Func, typename Duration>
future<>
compute_intensive_task(Duration duration, unsigned& counter, Func func) {
    auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        func();
    }
    ++counter;
    return make_ready_future<>();
}

future<>
heavy_task(unsigned& counter) {
    return compute_intensive_task(1ms, counter, [] {
        static thread_local double x = 1;
        x = std::exp(x) / 3;
    });
}

future<>
light_task(unsigned& counter) {
    return compute_intensive_task(100us, counter, [] {
        static thread_local double x = 0.1;
        x = std::log(x + 1);
    });
}

future<>
medium_task(unsigned& counter) {
    return compute_intensive_task(400us, counter, [] {
        static thread_local double x = 0.1;
        x = std::cos(x);
    });
}

future<>
run_compute_intensive_tasks(seastar::scheduling_group sg, bool& done, unsigned concurrency, unsigned& counter, std::function<future<> (unsigned& counter)> task) {
    return seastar::async([task, sg, concurrency, &done, &counter] {
        while (!done) {
            parallel_for_each(boost::irange(0u, concurrency), [task, sg, &counter] (unsigned i) {
                return seastar::with_scheduling_group(sg, [task, &counter] {
                    return task(counter);
                })();
            }).get();
        }
    });
}

future<>
run_compute_intensive_tasks_in_threads(seastar::scheduling_group sg, bool& done, unsigned concurrency, unsigned& counter, std::function<future<> (unsigned& counter)> task) {
    auto attr = seastar::thread_attributes();
    attr.sched_group = sg;
    return parallel_for_each(boost::irange(0u, concurrency), [attr, &done, &counter, task] (unsigned i) {
        return seastar::async(attr, [&done, &counter, task] {
            while (!done) {
                task(counter).get();
            }
        });
    });
}

#include <fenv.h>

int main(int ac, char** av) {
    app_template app;
    return app.run(ac, av, [] {
        return seastar::async([] {
            auto sg100 = seastar::create_scheduling_group("sg100", 100).get0();
            auto sg20 = seastar::create_scheduling_group("sg20", 20).get0();
            auto sg50 = seastar::create_scheduling_group("sg50", 50).get0();

            bool done = false;
            auto end = timer<>([&done] {
                done = true;
            });
            end.arm(10s);
            unsigned ctr100 = 0, ctr20 = 0, ctr50 = 0;
            when_all(
                    run_compute_intensive_tasks(sg100, done, 5, ctr100, heavy_task),
                    run_compute_intensive_tasks(sg20, done, 3, ctr20, light_task),
                    run_compute_intensive_tasks_in_threads(sg50, done, 2, ctr50, medium_task)
                    ).get();
            print("%10s %15s %10s %12s\n", "shares", "task_time (us)", "executed", "runtime (ms)");
            print("%10d %15d %10d %12d\n", 100, 1000, ctr100, ctr100 * 1000 / 1000);
            print("%10d %15d %10d %12d\n", 20, 100, ctr20, ctr20 * 100 / 1000);
            print("%10d %15d %10d %12d\n", 50, 400, ctr50, ctr50 * 400 / 1000);
            return 0;
        });
    });
}
