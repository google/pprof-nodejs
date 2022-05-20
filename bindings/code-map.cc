#include <unordered_map>

#include <node.h>

#include "profilers/cpu.hh"
#include "code-map.hh"

namespace dd {

static std::unordered_map<v8::Isolate*, std::shared_ptr<CodeMap>> code_maps_;

CodeMap::CodeMap(v8::Isolate* isolate, CodeEntries entries)
  : CodeEventHandler(isolate),
    code_entries_(entries),
    isolate_(isolate) {}

CodeMap::~CodeMap() {
  Disable();
}

std::shared_ptr<CodeMap> CodeMap::For(v8::Isolate* isolate) {
  auto maybe = code_maps_.find(isolate);
  if (maybe != code_maps_.end()) {
    return maybe->second;
  }

  code_maps_[isolate] = std::make_shared<CodeMap>(isolate);
  return code_maps_[isolate];
}

CodeEntries CodeMap::Entries() {
  return code_entries_;
}

void CodeMap::Enable() {
  if (++refs == 1) {
    CodeEventHandler::Enable();
    isolate_->SetJitCodeEventHandler(v8::kJitCodeEventDefault,
                                    StaticHandleJitEvent);
  }
}

void CodeMap::Disable() {
  if (--refs == 0) {
    CodeEventHandler::Disable();
    isolate_->SetJitCodeEventHandler(v8::kJitCodeEventDefault, nullptr);
    code_entries_.clear();
  }
}

// TODO: unsure of ordering but might need bi-directional merging for script id
void CodeMap::HandleJitEvent(const v8::JitCodeEvent* event) {
  if (event->type == v8::JitCodeEvent::CODE_REMOVED) {
    Remove(reinterpret_cast<uintptr_t>(event->code_start));
    return;
  }

  CodeEntries::iterator it = code_entries_.find(
      reinterpret_cast<uintptr_t>(event->code_start));

  if (it != code_entries_.end() && !event->script.IsEmpty()) {
    it->second->SetScriptId(event->script->GetId());
  }
}

void CodeMap::StaticHandleJitEvent(const v8::JitCodeEvent* event) {
  auto code_map = CodeMap::For(event->isolate);
  code_map->HandleJitEvent(event);
}

// TODO: Figure out if additional checks are needed to cleanup expired regions.
// If size of previous is greater than offset of new position, the old record
// must be invalid, clean it up.
void CodeMap::Handle(v8::CodeEvent* code_event) {
#if NODE_MODULE_VERSION > 79
  if (code_event->GetCodeType() == v8::CodeEventType::kRelocationType) {
    CodeEntries::iterator it = code_entries_.find(
      code_event->GetPreviousCodeStartAddress());
    if (it != code_entries_.end()) {
      code_entries_.erase(it);
    }
  }
#endif

  Add(code_event->GetCodeStartAddress(), 
      std::make_shared<CodeEventRecord>(isolate_, code_event));
}

void CodeMap::Add(uintptr_t address, std::shared_ptr<CodeEventRecord> record) {
  code_entries_.insert(std::make_pair(address, std::move(record)));
}

void CodeMap::Remove(uintptr_t address) {
  code_entries_.erase(address);
}

void CodeMap::Clear() {
  code_entries_.clear();
}

std::shared_ptr<CodeEventRecord> CodeMap::Lookup(uintptr_t address) {
  CodeEntries::iterator it = code_entries_.upper_bound(address);
  if (it == code_entries_.begin()) return nullptr;
  --it;
  uintptr_t start_address = it->first;
  std::shared_ptr<CodeEventRecord> entry = it->second;
  uintptr_t code_end = start_address + entry->size;
  if (address >= code_end) return nullptr;
  return entry;
}

}; // namespace dd
