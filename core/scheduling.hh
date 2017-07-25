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

#include "sstring.hh"

/// \file

namespace seastar {


template <typename... T>
class future;

class reactor;

class scheduling_group;


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
private:
    explicit scheduling_group(unsigned id) : _id(id) {}
public:
    /// Creates a `scheduling_group` object denoting the default group
    scheduling_group() noexcept : _id(0) {}
    bool active() const;
    const sstring& name() const;
    bool operator==(scheduling_group x) const { return _id == x._id; }
    bool operator!=(scheduling_group x) const { return _id != x._id; }
    friend future<scheduling_group> create_scheduling_group(sstring name, unsigned shares);
    friend class reactor;
};


}
