/*
 * Copyright (2022) Bytedance Ltd. and/or its affiliates
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Functions/FunctionFactory.h>
#include <Functions/DateTimeTransforms.h>
#include <Functions/FunctionDateOrDateTimeToDateOrDate32.h>

namespace DB
{

using FunctionLastDay = FunctionDateOrDateTimeToDateOrDate32<ToLastDayOfMonthImpl>;

REGISTER_FUNCTION(LastDay)
{
    factory.registerFunction<FunctionLastDay>();
    factory.registerAlias("last_day", ToLastDayOfMonthImpl::name, FunctionFactory::CaseInsensitive);
    factory.registerAlias("lastDay", ToLastDayOfMonthImpl::name);
}

}
