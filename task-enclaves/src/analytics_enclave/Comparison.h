/*
* Copyright 2021 European Union
*
* Licensed under the EUPL, Version 1.2 or – as soon they will be approved by 
* the European Commission - subsequent versions of the EUPL (the "Licence");
* You may not use this work except in compliance with the Licence.
* You may obtain a copy of the Licence at:
*
* https://joinup.ec.europa.eu/software/page/eupl
*
* Unless required by applicable law or agreed to in writing, software 
* distributed under the Licence is distributed on an "AS IS" basis,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the Licence for the specific language governing permissions and 
* limitations under the Licence.
*/ 

#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <sharemind-hi/common/ZeroingArray.h>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include "../pseudonymisation_key_enclave/Entities.h"

namespace eurostat {
namespace enclave {

// Assumes that there is `left` and `right`, and make_tuple-expression (`...`) uses `e`.
#define CMP(CMP_OP, ...)                                                       \
    auto make_tuple = [&](decltype(left) e) {                                  \
        /* Use std::make_tuple here instead of std::tie, as std::tie uses      \
         * references and this will not work with the packed structs. As in,   \
         * really, it does not work, I tested it. */                           \
        return std::make_tuple(__VA_ARGS__);                                   \
    };                                                                         \
    return make_tuple(left) CMP_OP make_tuple(right)

#define CMP_FUN(CMP_OP, TYPE, ...)                                              \
    inline bool operator CMP_OP(TYPE const & left, TYPE const & right) noexcept \
    {                                                                           \
        CMP(CMP_OP, __VA_ARGS__);                                               \
    }

#define CMP_LAMBDA(CMP_OP, TYPE, ...)                                          \
    [](TYPE const & left, TYPE const & right) noexcept {                       \
        CMP(CMP_OP, __VA_ARGS__);                                              \
    }

} // namespace enclave
} // namespace eurostat
