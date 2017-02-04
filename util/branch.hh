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
 * Copyright (C) 2017 ScyllaDB
 */

#pragma once

/// \file

namespace seastar {

/// Annotate a boolean expression as likely to be true
///
/// This function informs the compiler that a boolean
/// expression is overwhelmingly almost always true.
///
/// \param b  A boolean expression about which we make the prediction
/// \return \code b
inline
bool
likely(bool b) {
    return __builtin_expect(b, true);
}

/// Annotate a boolean expression as unlikely to be true
///
/// This function informs the compiler that a boolean
/// expression is overwhelmingly almost always false.
///
/// \param b  A boolean expression about which we make the prediction
/// \return \code b
inline
bool
unlikely(bool b) {
    return __builtin_expect(b, false);
}

}
