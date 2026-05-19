/*
 * Copyright 2024 Datadog, Inc
 *
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

#pragma once

#include <v8-profiler.h>
#include <v8.h>
#include <memory>
#include <string>
#include <vector>

namespace dd {

struct Node {
  using Allocation = v8::AllocationProfile::Allocation;
  std::string name;
  std::string script_name;
  int line_number;
  int column_number;
  int script_id;
  std::vector<std::shared_ptr<Node>> children;
  std::vector<Allocation> allocations;
};

std::shared_ptr<Node> TranslateAllocationProfileToCpp(
    v8::AllocationProfile::Node* node);

v8::Local<v8::Value> TranslateAllocationProfile(Node* node);
v8::Local<v8::Value> TranslateAllocationProfile(
    v8::AllocationProfile::Node* node);

}  // namespace dd
