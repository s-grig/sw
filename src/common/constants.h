/*
 * Copyright (C) 2016-2017, Egor Pugin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

constexpr auto operator "" _B(unsigned long long int i)
{
    return i;
}

constexpr auto operator "" _KB(unsigned long long int i)
{
    return i * 1024_B;
}

constexpr auto operator "" _MB(unsigned long long int i)
{
    return i * 1024_KB;
}

constexpr auto operator "" _GB(unsigned long long int i)
{
    return i * 1024_MB;
}

constexpr auto operator "" _PB(unsigned long long int i)
{
    return i * 1024_GB;
}

