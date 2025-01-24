/**
 * Copyright 2018 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <v8-profiler.h>
#include "contexts.hh"

namespace dd {

v8::Local<v8::Value> TranslateTimeProfile(
    const v8::CpuProfile* profile,
    bool includeLineInfo,
    std::shared_ptr<ContextsByNode> contextsByNode = nullptr,
    bool hasCpuTime = false,
    int64_t nonJSThreadsCpuTime = 0);

}  // namespace dd
