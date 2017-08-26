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
 * Copyright 2017 ScyllaDB
 */

#include "../core/app-template.hh"
#include "../core/reactor.hh"
#include <chrono>

// Throwing an exception and the stall detector both use the unwinder,
// one of them from a signal context; however the unwinder is not signal
// safe.  This test reproduces the problem (if the workaround doesn't work)

using namespace seastar;
using namespace std::chrono_literals;

unsigned volatile something = 0;

int throw_deep_exception(unsigned depth = 100) {
    if (depth == 0) {
        throw 0;
    }
    ++something; // avoid clever optimizations
    return depth + something + throw_deep_exception(depth - 1) + throw_deep_exception(depth - 2); // recurse to make the unwinder spend a lot of time unwinding
}

future<> lots_of_exceptions() {
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < t0 + 5s) {
        for (auto i = 0; i < 100; ++i) {
            try {
                throw_deep_exception();
            } catch (...) {
                // ignore
            }
        }
    }
    return make_ready_future<>();
}

int main(int ac, char** av) {
    app_template::config cfg;
    cfg.default_block_notifier_delay = 1ms;
    app_template app(cfg);
    return app.run(ac, av, [] {
        return smp::invoke_on_all([] {
            return lots_of_exceptions();
        }).then([] {
            return 0;
        });
    });
}
