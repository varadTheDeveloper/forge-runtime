#include <stdio.h>
#include <string.h>
#include "js/CallAndConstruct.h"
#include "js/String.h"
#include <string>
#include <utility>
#include "js/CompileOptions.h"
#include "js/CompilationAndEvaluation.h"
#include "js/Context.h"
#include "js/GCAPI.h"
#include "js/GlobalObject.h"
#include "js/Initialization.h"
#include "js/RealmOptions.h"
#include "js/RootingAPI.h"
#include "js/SourceText.h"
#include "js/Promise.h"
#include "js/Object.h"
#include "js/TracingAPI.h"
#include "js/Value.h"

#include "js/CallArgs.h"
#include "js/CharacterEncoding.h"
#include "js/PropertyAndElement.h"
#include "js/Conversions.h"
#include "js/Exception.h"
#include "js/ErrorReport.h"
#include "js/ArrayBuffer.h"
#include "js/experimental/TypedData.h"
#include "js/PropertyDescriptor.h"  // JSPROP_ENUMERATE (Phase 7.3, fs.statSync)

// JS_NewPlainObject (Phase 7.3, fs's namespace object and statSync's
// returned {size} object) is not exposed under js/public -- confirmed by
// reading js/public/Object.h directly, which has no object-construction
// free function. It lives in jsapi.h instead (js/src/jsapi.h, confirmed by
// reading it directly), the classic top-level SpiderMonkey embedding
// header every other js/public/*.h include in this file was deliberately
// kept narrower than -- included here just for this one function.
#include "jsapi.h"

#include "forge-core/platform/IoLoop.h"
#include "forge-core/File.h"
#include "forge-core/HashMap.h"
#include "forge-core/Path.h"
#include "forge-core/Queue.h"
#include "forge-core/Result.h"
#include "forge-core/String.h"
#include "forge-core/StringView.h"
#include "forge-core/memory/DefaultAllocator.h"
#include "forge-core/memory/MakeUnique.h"
#include "forge-core/memory/UniquePtr.h"
#include "forge-core/memory/Vector.h"

// cx points to SpiderMonkey's context object.
static const JSClass globalClass = {
    "global",
    JSCLASS_GLOBAL_FLAGS | JSCLASS_HAS_RESERVED_SLOTS(1),
    &JS::DefaultGlobalClassOps,
};

enum GlobalSlots { RuntimeSlot = JSCLASS_GLOBAL_SLOT_COUNT };
static bool EnqueueMicrotask(JSContext* cx, JS::HandleObject callback);

// Forward declarations: ForgeTimerFired (defined further down, right after
// JsTimerRegistry) needs to reach the current Runtime's timer registry,
// but Runtime itself is only fully defined later in this file (it embeds
// JsTimerRegistry by value, so it can't be forward-declared-only at that
// point). Same pattern already used for EnqueueMicrotask above.
class JsTimerRegistry;
static JsTimerRegistry& GetRuntimeTimers(JSContext* cx);

// Prints (and consumes) whatever exception is currently pending on `cx`,
// tagged with `context` for the "could not even retrieve it" fallback
// case. Shared by every callback boundary that calls into JS and cannot
// propagate a C++ exception on failure (there are no C++ exceptions in
// this codebase — see forge-core's Result<T>/AGENTS.md philosophy; this is
// the JS-engine-boundary equivalent: report, don't throw, don't silently
// drop it).
static void ReportPendingException(JSContext* cx, const char* context) {
  if (!JS_IsExceptionPending(cx)) {
    return;
  }

  JS::ExceptionStack exn(cx);

  if (JS::StealPendingExceptionStack(cx, &exn)) {
    JS::ErrorReportBuilder report(cx);

    if (report.init(cx, exn, JS::ErrorReportBuilder::NoSideEffects)) {
      JS::PrintError(stderr, report.report(), false);
      return;
    }
  }

  fprintf(stderr, "Forge: %s failed (exception pending, but could not be retrieved)\n",
          context);
}

// No `arguments` field here (unlike JsTimer below): ForgeJobQueue::runJobs()
// always invokes a microtask's callback with JS::HandleValueArray::empty(),
// never task->arguments, so a PersistentRootedVector<JS::Value> on every
// queued microtask was dead weight — allocated (well, self-registered with
// `cx`) and destroyed on every single one for a field nothing ever read.
// Only JsTimer actually needs argument forwarding, for setTimeout/
// setInterval's extra arguments passed through to the callback.
struct Microtask {
  JS::Heap<JSObject*> callback;
};

//==============================================================================
// Runtime Integration (Phase 6)
//
// The microtask queue and timer registry below are backed by forge-core's
// own containers (Queue<T>/HashMap<K,V>) rather than std::vector, and
// EnqueueMicrotask/SetTimeout/SetInterval go through forge-core's
// MakeUnique<T> (Result<UniquePtr<T>>) rather than std::make_unique, so
// that an allocation failure is a reported JS OOM error instead of an
// abrupt std::terminate() (this codebase's -fno-exceptions build has no
// other way to recover from operator new failing). See HISTORY.md's
// Phase 6 entry for the two real bugs this rewrite fixes:
//   (1) GC-root-tracing gap: a JobQueue must trace every still-pending
//       microtask's callback, not just the front one, or the collector
//       could reclaim a callback a later Pop() still needs. Fixed via
//       Queue<T>::operator[] (front-relative indexed access, added this
//       phase) walked in TraceForgeRoots below.
//   (2) Use-after-free-on-OOM-rollback: JsTimerRegistry::Add() schedules
//       the native timer before it knows whether the JS-side wrapper can
//       actually be stored. If storing it fails, the *already-armed*
//       native timer must be cancelled before returning, or it fires
//       against memory that's about to be freed. See the comment in
//       Add() below.
//==============================================================================

forge::core::Queue<forge::core::memory::UniquePtr<Microtask>> microtasks;

class ForgeJobQueue : public JS::JobQueue {
 public:
  bool empty() const override { return microtasks.Empty(); }

  bool isDrainingStopped() const override { return false; }

  bool getHostDefinedData(JSContext* cx,
                          JS::MutableHandle<JSObject*> data) const override {
    data.set(nullptr);
    return true;
  }

  void runJobs(JSContext* cx) override {
    while (!microtasks.Empty()) {
      Microtask* task = microtasks.Front().Get();

      JS::RootedObject callback(cx, task->callback);
      JS::RootedValue thisValue(cx, JS::UndefinedValue());
      JS::RootedValue rval(cx);

      bool ok = JS::Call(cx, thisValue, callback, JS::HandleValueArray::empty(),
                         JS::MutableHandleValue(&rval));

      if (!ok) {
        ReportPendingException(cx, "microtask");
        return;
      }

      microtasks.Pop();
    }
  }

  bool enqueuePromiseJob(JSContext* cx, JS::HandleObject promise,
                         JS::HandleObject job, JS::HandleObject allocationSite,
                         JS::HandleObject hostDefinedData) override {
    return EnqueueMicrotask(cx, job);
  }

 protected:
  class SavedQueue;

  js::UniquePtr<SavedJobQueue> saveJobQueue(JSContext* cx) override;
};
js::UniquePtr<JS::JobQueue::SavedJobQueue> ForgeJobQueue::saveJobQueue(
    JSContext* cx) {
  return nullptr;
}

//==============================================================================
// Timers
//
// Bridges the JS-facing setTimeout/setInterval/clearTimeout API onto
// forge::core::platform::IoLoop's TimerId-based scheduling (see
// ROADMAP.md Phase 2 / HISTORY.md — this replaces the old TimerQueue's
// manual "scan every timer, compare to now()" polling, driven by
// EventLoop's busy-wait, with the IOCP-backed loop actually sleeping until
// the next timer or I/O event).
//
// Two id spaces are kept deliberately separate: `jsId` is the small
// integer setTimeout()/setInterval() return to script (unchanged
// behaviour); `nativeId` is IoLoop's own forge::core::platform::TimerId,
// used only internally to cancel the right native timer.
//==============================================================================

static void ForgeTimerFired(void* userData) noexcept;

struct JsTimer {
  JSContext* cx;
  JS::Heap<JSObject*> callback;
  JS::PersistentRootedVector<JS::Value> arguments;

  int jsId{0};
  forge::core::platform::TimerId nativeId{0};
  bool repeat{false};

  explicit JsTimer(JSContext* cx) : cx(cx), arguments(cx) {}
};

class JsTimerRegistry {
 public:
  explicit JsTimerRegistry(forge::core::platform::IoLoop& loop) : loop_(loop) {}

  // Takes ownership of `timer` (already populated with callback/arguments/
  // repeat/delayMs by the caller) and schedules it. Returns the JS-visible
  // id, or -1 if native scheduling (or JS-side registration) failed —
  // `timer` is destroyed in that case, nothing is left registered.
  int Add(forge::core::memory::UniquePtr<JsTimer> timer, uint64_t delayMs) {
    timer->jsId = nextJsId_++;

    JsTimer* raw = timer.Get();

    forge::core::Result<forge::core::platform::TimerId> scheduled =
        loop_.ScheduleTimer(delayMs, raw->repeat, &ForgeTimerFired, raw);

    if (scheduled.HasError()) {
      return -1;
    }

    raw->nativeId = scheduled.Value();

    int jsId = raw->jsId;

    forge::core::Result<bool> inserted = timers_.Insert(jsId, std::move(timer));

    if (inserted.HasError()) {
      // HashMap::Insert's failure path (its internal GrowIfNeeded() running
      // out of memory) returns before ever moving from its V&& argument —
      // so `timer` (this function's local parameter) still owns *raw at
      // this point. That means the native timer scheduled above is now
      // armed against memory that's about to be freed the moment this
      // function returns (timer's destructor runs, since nothing else took
      // ownership) — a real use-after-free the next time it fired. Cancel
      // it first so there's nothing left pointing at *raw once it's gone.
      loop_.CancelTimer(raw->nativeId);
      return -1;
    }

    return jsId;
  }

  void CancelByJsId(int jsId) {
    forge::core::memory::UniquePtr<JsTimer>* found = timers_.Find(jsId);

    if (found == nullptr) {
      return;
    }

    loop_.CancelTimer((*found)->nativeId);
    timers_.Erase(jsId);
  }

  // Called once a one-shot timer's callback has finished running, to
  // release its JsTimer. The native side has already removed the
  // corresponding entry itself (TimerScheduler::PopDue erases one-shots on
  // firing) — this only releases the JS-side wrapper.
  void RemoveFired(int jsId) {
    timers_.Erase(jsId);
  }

  // Traces every live timer's JS callback so the GC does not collect it
  // out from under a still-pending timer. JS::Heap<T> (unlike
  // JS::PersistentRootedVector, which self-registers and needs no manual
  // tracing — that's why `arguments` above needed no attention here) is
  // *not* traced automatically; something owning it must do so explicitly.
  void TraceRoots(JSTracer* trc) {
    for (auto entry : timers_) {
      JS::TraceEdge(trc, &entry.Value()->callback, "forge-timer-callback");
    }
  }

  // Cancels and releases every still-pending timer. Must be called (via
  // Runtime::Shutdown()) while `cx` is still alive and before
  // JS_DestroyContext(cx) — each JsTimer holds a
  // JS::PersistentRootedVector<JS::Value>, which needs to unregister
  // itself from the still-live context when destroyed. Normally run()
  // only stops once every timer is already gone, but it can now also stop
  // early on a genuine I/O error (see Runtime::run()), which is exactly
  // the case this exists for.
  void Clear() {
    for (auto entry : timers_) {
      loop_.CancelTimer(entry.Value()->nativeId);
    }
    timers_.Clear();
  }

 private:
  forge::core::platform::IoLoop& loop_;
  forge::core::HashMap<int, forge::core::memory::UniquePtr<JsTimer>> timers_;
  int nextJsId_{1};
};

static void ForgeTimerFired(void* userData) noexcept {
  auto* timer = static_cast<JsTimer*>(userData);
  JSContext* cx = timer->cx;
  bool repeat = timer->repeat;
  int jsId = timer->jsId;

  JS::RootedObject callback(cx, timer->callback);
  JS::RootedValue thisValue(cx, JS::UndefinedValue());
  JS::RootedValue rval(cx);

  bool ok = JS::Call(cx, thisValue, callback, timer->arguments,
                     JS::MutableHandleValue(&rval));

  if (!ok) {
    ReportPendingException(cx, "timer callback");
  }

  // Must be last: on a one-shot timer this destroys *timer (via the
  // registry's owning HashMap), so nothing above may touch `timer` again
  // after this — hence copying jsId/repeat out at the top instead of
  // reading timer->... below.
  if (!repeat) {
    GetRuntimeTimers(cx).RemoveFired(jsId);
  }
}

//==============================================================================
// Runtime / GC root tracing
//==============================================================================

static void TraceForgeRoots(JSTracer* trc, void* data) {
  auto* timers = static_cast<JsTimerRegistry*>(data);

  for (forge::core::Size i = 0; i < microtasks.Size(); ++i) {
    JS::TraceEdge(trc, &microtasks[i]->callback, "forge-microtask-callback");
  }

  timers->TraceRoots(trc);
}

class Runtime {
 public:
  explicit Runtime(JSContext* cx)
      : cx_(cx), loop_(), timers_(loop_), jobQueue_() {
    JS::SetJobQueue(cx, &jobQueue_);
  }

  // Must be called exactly once, after construction and before any timer
  // is scheduled or run() is called.
  [[nodiscard]] forge::core::Result<void> Initialize() {
    if (forge::core::Result<void> result = loop_.Initialize(); result.HasError()) {
      return result;
    }

    JS_AddExtraGCRootsTracer(cx_, TraceForgeRoots, &timers_);
    return {};
  }

  // Drains microtasks, then blocks on the event loop for the next timer
  // or I/O completion, repeating until there is genuinely nothing left
  // pending — replaces the old EventLoop's busy-poll `while (...) { ...;
  // sleep_for(1ms); }` with the loop actually sleeping until something is
  // due (see ROADMAP.md Phase 2).
  void run() {
    for (;;) {
      jobQueue_.runJobs(cx_);

      if (jobQueue_.empty() && loop_.Empty()) {
        break;
      }

      forge::core::Result<void> result = loop_.RunOnce();

      if (result.HasError()) {
        fprintf(stderr, "Forge: event loop I/O error (native code %d)\n",
                result.Error().NativeCode());
        break;
      }
    }
  }

  JsTimerRegistry& timers() { return timers_; }

  // Releases every still-pending timer. Call this once, after run() has
  // returned (however it returned), and before JS_DestroyContext(cx) — see
  // JsTimerRegistry::Clear() for exactly why this ordering matters.
  void Shutdown() { timers_.Clear(); }

 private:
  JSContext* cx_;
  forge::core::platform::IoLoop loop_;
  JsTimerRegistry timers_;
  ForgeJobQueue jobQueue_;
};

Runtime* GetRuntime(JSContext* cx)
{
    JS::RootedObject global(
        cx,
        JS::CurrentGlobalOrNull(cx)
    );

    if (!global) {
        return nullptr;
    }

    JS::Value value =
        JS::GetReservedSlot(global, RuntimeSlot);

    return static_cast<Runtime*>(value.toPrivate());
}

// Thin helper so ForgeTimerFired (a free function, declared above Runtime)
// can reach the registry without needing Runtime's full definition visible
// at its point of use.
static JsTimerRegistry& GetRuntimeTimers(JSContext* cx) {
  return GetRuntime(cx)->timers();
}

static bool Print(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  for (unsigned i = 0; i < args.length(); i++) {
    JS::RootedString str(cx, JS::ToString(cx, args[i]));

    if (!str) {
      return false;
    }

    JS::UniqueChars bytes = JS_EncodeStringToUTF8(cx, str);

    if (!bytes) {
      return false;
    }

    printf("%s", bytes.get());

    if (i + 1 < args.length()) {
      printf(" ");
    }
  }

  printf("\n");

  args.rval().setUndefined();
  return true;
}

static bool SetTimeout(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (argc < 2) {
    return false;
  }
  if (!args[0].isObject()) {
    return false;
  }
  double delay = 0;

  if (!JS::ToNumber(cx, args[1], &delay)) {
    return false;
  }
  if (delay < 0 || !(delay == delay)) {  // NaN-safe: NaN < 0 is false, so clamp NaN explicitly too.
    delay = 0;
  }

  forge::core::Result<forge::core::memory::UniquePtr<JsTimer>> made =
      forge::core::memory::MakeUnique<JsTimer>(cx);

  if (made.HasError()) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  forge::core::memory::UniquePtr<JsTimer> timer = std::move(made.Value());

  JS::RootedObject callback(cx, &args[0].toObject());
  timer->callback = callback;
  timer->repeat = false;

  for (unsigned i = 2; i < args.length(); i++) {
    if (!timer->arguments.append(args[i])) {
      return false;
    }
  }

  int id = GetRuntime(cx)->timers().Add(std::move(timer), (uint64_t)delay);

  if (id < 0) {
    JS_ReportErrorASCII(cx, "setTimeout: failed to schedule timer");
    return false;
  }

  args.rval().setInt32(id);

  return true;
}
static bool ClearTimeout(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (argc < 1) {
    return false;
  }

  int32_t id;

  if (!JS::ToInt32(cx, args[0], &id)) {
    return false;
  }

  GetRuntime(cx)->timers().CancelByJsId(id);

  args.rval().setUndefined();

  return true;
}
static bool SetInterval(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (argc < 2) {
    return false;
  }
  if (!args[0].isObject()) {
    return false;
  }
  double delay = 0;

  if (!JS::ToNumber(cx, args[1], &delay)) {
    return false;
  }
  if (delay < 0 || !(delay == delay)) {
    delay = 0;
  }

  forge::core::Result<forge::core::memory::UniquePtr<JsTimer>> made =
      forge::core::memory::MakeUnique<JsTimer>(cx);

  if (made.HasError()) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  forge::core::memory::UniquePtr<JsTimer> timer = std::move(made.Value());

  JS::RootedObject callback(cx, &args[0].toObject());
  timer->callback = callback;
  timer->repeat = true;

  for (unsigned i = 2; i < args.length(); i++) {
    if (!timer->arguments.append(args[i])) {
      return false;
    }
  }

  int id = GetRuntime(cx)->timers().Add(std::move(timer), (uint64_t)delay);

  if (id < 0) {
    JS_ReportErrorASCII(cx, "setInterval: failed to schedule timer");
    return false;
  }

  args.rval().setInt32(id);

  return true;
}
static bool EnqueueMicrotask(JSContext* cx, JS::HandleObject callback) {
  forge::core::Result<forge::core::memory::UniquePtr<Microtask>> made =
      forge::core::memory::MakeUnique<Microtask>();

  if (made.HasError()) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  forge::core::memory::UniquePtr<Microtask> task = std::move(made.Value());
  task->callback = callback;

  if (microtasks.Push(std::move(task)).HasError()) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  return true;
}
static bool QueueMicrotask(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  if (argc < 1) {
    return false;
  }

  if (!args[0].isObject()) {
    return false;
  }

  JS::RootedObject callback(cx, &args[0].toObject());

  if (!EnqueueMicrotask(cx, callback)) {
    return false;
  }

  args.rval().setUndefined();

  return true;
}

//==============================================================================
// JS/Native Marshalling Primitives (Phase 7.2) -- real-build-confirmed
// 2026-07-30 (`python mach build` succeeded against this exact file).
//
// Implements JsBindings.md's frozen "Public API" section verbatim (same
// names, parameter types, return types) -- the conventions every future
// JS-visible binding (fs now; net/threads later) is meant to share rather
// than each reinventing its own error/marshalling glue. See JsBindings.md
// for the full design rationale; only implementation-specific notes are
// repeated here.
//
// All six are `static` (internal linkage), matching every other helper
// in this file (Print/SetTimeout/ReportPendingException/etc.) -- none of
// them need to be visible outside this translation unit. None of the six
// are called from any JS-visible entry point yet -- Phase 7.3 (the
// concrete `fs.*Sync` bindings from Fs.md) is what wires them up to a
// JS_DefineFunction. Until then, the self-test suite below (run via
// `forge --self-test`) is what exercises them for real: it gives every
// one of these six a genuine call site with a live JSContext/Realm, so
// none needs an [[maybe_unused]] marker despite nothing script-visible
// calling them yet.
//
// Verification history (per AGENTS.md's "Be Honest"): every SpiderMonkey
// function name/signature used below (JS_NewStringCopyUTF8N,
// JS_IsUint8Array, JS_NewUint8Array, JS_GetUint8ArrayData,
// JS_GetTypedArrayByteLength, JS::IsArrayBufferObject,
// JS::IsDetachedArrayBufferObject, JS::GetArrayBufferData,
// JS::GetArrayBufferByteLength, JS::AutoCheckCannotGC, and everything
// already proven by Print()/SetTimeout()/etc. above) was confirmed by
// reading the real headers under this tree's own
// js/public/{ArrayBuffer.h,experimental/TypedData.h,String.h,
// Exception.h,ErrorReport.h,Conversions.h,PropertyAndElement.h,
// Value.h,GCAPI.h,RootingAPI.h} directly before writing this code -- not
// guessed or inferred from memory. Before the real `mach build`, the
// forge-core-facing logic was separately compiled and run (real
// forge-core headers + a signature-faithful fake JSAPI shim) under
// g++/clang++ with full warnings, ASan+UBSan, and valgrind (16/16
// scenarios, 0 leaks) -- see HISTORY.md's Phase 7.2 entry for the full
// account. The real `mach build` succeeding confirms the SpiderMonkey
// call shapes themselves were right; the self-test suite below adds the
// one thing neither of those passes covered: actually calling these
// functions against a live JSContext and checking their results.
//==============================================================================

// ErrorCode -> error.code string table from JsBindings.md's "Error
// Handling Policy". Exhaustive over every ErrorCode enumerator (verified
// against forge-core/Error.h directly) so adding a new enumerator there
// without updating this switch is a compiler error (no `default:`), not
// a silent "Unknown" fallback for a code that should have had a real
// mapping -- the trailing return below only covers the (impossible in
// practice) case of `code` holding a value outside the enum's defined
// range.
static const char* ErrorCodeToString(forge::core::ErrorCode code) {
  using forge::core::ErrorCode;

  switch (code) {
    case ErrorCode::None:             return "None";
    case ErrorCode::Unknown:          return "Unknown";
    case ErrorCode::InvalidArgument:  return "InvalidArgument";
    case ErrorCode::InvalidOperation: return "InvalidOperation";
    case ErrorCode::NotSupported:     return "NotSupported";
    case ErrorCode::NotImplemented:   return "NotImplemented";
    case ErrorCode::NotFound:         return "NotFound";
    case ErrorCode::AlreadyExists:    return "AlreadyExists";
    case ErrorCode::PermissionDenied: return "PermissionDenied";
    case ErrorCode::Busy:             return "Busy";
    case ErrorCode::Timeout:          return "Timeout";
    case ErrorCode::Cancelled:        return "Cancelled";
    case ErrorCode::EndOfFile:        return "EndOfFile";
    case ErrorCode::IOError:          return "IOError";
    case ErrorCode::OutOfMemory:      return "OutOfMemory";
    case ErrorCode::BufferTooSmall:   return "BufferTooSmall";
    case ErrorCode::Overflow:         return "Overflow";
    case ErrorCode::Underflow:        return "Underflow";
    case ErrorCode::InvalidData:      return "InvalidData";
    case ErrorCode::ParseError:       return "ParseError";
    case ErrorCode::PlatformError:    return "PlatformError";
  }

  return "Unknown";
}

// Throws a JS Error annotated with `.code` (and, when available, `.path`/
// `.syscall`/`.nativeCode`) for a failed Result<T>/Result<void>. Per
// JsBindings.md, OutOfMemory is never routed through here -- callers use
// JS_ReportOutOfMemory directly, matching EnqueueMicrotask/SetTimeout/
// SetInterval above.
static void ThrowJsError(JSContext* cx,
                                          const forge::core::Error& error,
                                          const char* context,
                                          const forge::core::Path* path = nullptr) {
  const char* codeStr = ErrorCodeToString(error.Code());

  // forge::core::Error carries no free-text message (see Error.h -- just
  // Code()/NativeCode()), so the reported message is synthesized from
  // `context` and the mapped code string rather than inventing an
  // Error::Message() this codebase doesn't have.
  JS_ReportErrorUTF8(cx, "%s failed: %s", context, codeStr);

  if (!JS_IsExceptionPending(cx)) {
    // JS_ReportErrorUTF8 itself hit trouble (e.g. OOM formatting the
    // message) and left nothing pending to annotate further.
    return;
  }

  JS::RootedValue excVal(cx);
  if (!JS_GetPendingException(cx, &excVal) || !excVal.isObject()) {
    // Not an Error object we can annotate (shouldn't normally happen for
    // an exception JS_ReportErrorUTF8 itself just created) -- the base
    // message is still thrown, just without the extra properties below.
    return;
  }

  JS::RootedObject excObj(cx, &excVal.toObject());

  // Every JS_SetProperty call below is best-effort: if one fails (e.g.
  // OOM allocating the property string), the base thrown Error --
  // already pending -- is left intact rather than compounding the
  // original failure with a second, unrelated OOM report.
  if (JSString* codeJsStr = JS_NewStringCopyZ(cx, codeStr)) {
    JS::RootedValue codeVal(cx, JS::StringValue(codeJsStr));
    JS_SetProperty(cx, excObj, "code", codeVal);
  }

  if (JSString* syscallJsStr = JS_NewStringCopyZ(cx, context)) {
    JS::RootedValue syscallVal(cx, JS::StringValue(syscallJsStr));
    JS_SetProperty(cx, excObj, "syscall", syscallVal);
  }

  if (path != nullptr) {
    const forge::core::StringView pathView = path->View();
    if (JSString* pathJsStr = JS_NewStringCopyUTF8N(
            cx, JS::UTF8Chars(pathView.Data(), pathView.Size()))) {
      JS::RootedValue pathVal(cx, JS::StringValue(pathJsStr));
      JS_SetProperty(cx, excObj, "path", pathVal);
    }
  }

  if (error.Code() == forge::core::ErrorCode::PlatformError) {
    JS::RootedValue nativeCodeVal(cx, JS::NumberValue(error.NativeCode()));
    JS_SetProperty(cx, excObj, "nativeCode", nativeCodeVal);
  }

  // `excObj` is the very heap object already set as cx's pending
  // exception (JS_GetPendingException returned it, not a copy) -- setting
  // properties on it in place is visible to whatever eventually catches
  // it without needing a second JS_SetPendingException call.
}

// Converts an arbitrary JS value to a forge::core::String via ToString
// semantics (matches JS::ToString(cx, value) + JS_EncodeStringToUTF8,
// the exact pattern Print() above already uses and this sandbox cannot
// re-verify beyond that existing, working precedent).
//
// Design note (documented here per the instruction to record any design
// clarification before implementing it -- this is a behavioral
// clarification of JsBindings.md's contract, not a change to any frozen
// signature): a Result<T> failure returned by this function, ToForgePath,
// or any future sibling never leaves its own exception pending on `cx` --
// JS_ClearPendingException is called internally on every failure path
// where an underlying JS:: call (JS::ToString, JS_EncodeStringToUTF8)
// may already have reported one. This lets every caller uniformly do
// `if (result.HasError()) { ThrowJsError(cx, result.Error(), ...); return
// false; }` on any Result<T> from this module without ever risking two
// exceptions pending at once. JsBindings.md's "Error Handling Policy"
// section is updated alongside this change to state the same thing.
static forge::core::Result<forge::core::String> ToForgeString(JSContext* cx,
                                                        JS::HandleValue value) {
  JS::RootedString jsStr(cx, JS::ToString(cx, value));

  if (!jsStr) {
    JS_ClearPendingException(cx);
    return forge::core::Result<forge::core::String>(
        forge::core::Failure{forge::core::Error(forge::core::ErrorCode::InvalidArgument)});
  }

  JS::UniqueChars utf8 = JS_EncodeStringToUTF8(cx, jsStr);

  if (!utf8) {
    JS_ClearPendingException(cx);
    return forge::core::Result<forge::core::String>(
        forge::core::Failure{forge::core::Error(forge::core::ErrorCode::OutOfMemory)});
  }

  // Treated as a null-terminated C string, same assumption Print() above
  // already relies on (printf("%s", bytes.get())) -- a JS string
  // containing an embedded U+0000 would be silently truncated here. Not
  // a new limitation introduced by this function, just inherited from
  // the same JS_EncodeStringToUTF8-based idiom already in production use
  // in this file.
  return forge::core::String::Create(forge::core::StringView(utf8.get()));
}

// Converts a JS value to a forge::core::Path by first converting it to a
// String (above) and then through Path::Create -- Path never touches the
// OS itself (see Path.md), so this is pure value conversion, no new
// failure modes beyond what ToForgeString/Path::Create already have.
static forge::core::Result<forge::core::Path> ToForgePath(
    JSContext* cx, JS::HandleValue value) {
  forge::core::Result<forge::core::String> str = ToForgeString(cx, value);

  if (str.HasError()) {
    return forge::core::Result<forge::core::Path>(
        forge::core::Failure{str.Error()});
  }

  return forge::core::Path::Create(str.Value().View());
}

// Converts a forge::core::StringView to a new JS string (copies; does not
// take ownership of `text`'s storage, matching every JS_New*StringCopy*
// function's own documented convention in js/String.h, as opposed to
// JS_NewUCString's ownership-transferring convention).
//
// Returns nullptr on OOM. JS_NewStringCopyUTF8N is expected to report its
// own failure (standard JS_New*StringCopy* convention across this
// header), but since that isn't spelled out explicitly in js/String.h's
// comments and this sandbox has no way to compile-and-observe it
// directly, this reports defensively if the call somehow left nothing
// pending -- so a caller can always trust "null return means an
// exception is pending" without needing to know which of the two
// conventions is actually in effect.
static JSString* FromForgeString(JSContext* cx,
                                          forge::core::StringView text) {
  JSString* result = JS_NewStringCopyUTF8N(
      cx, JS::UTF8Chars(text.Data(), text.Size()));

  if (!result && !JS_IsExceptionPending(cx)) {
    JS_ReportOutOfMemory(cx);
  }

  return result;
}

// Creates a new Uint8Array and copies `bytes`'s contents into it. Consumes
// `bytes` (taken by value; released when this function returns, per
// JsBindings.md's documented ownership: the JS engine owns the copy from
// here on, forge-core's Vector<u8> is not retained).
static JSObject* Uint8ArrayFromBytes(JSContext* cx,
                                               forge::core::Vector<forge::core::u8> bytes) {
  const size_t length = static_cast<size_t>(bytes.Size());

  JS::RootedObject array(cx, JS_NewUint8Array(cx, length));

  if (!array) {
    // JS_NewUint8Array reports its own failure (OOM, or a RangeError if
    // `length` exceeds the engine's maximum typed array size).
    return nullptr;
  }

  if (length > 0) {
    JS::AutoCheckCannotGC nogc(cx);
    bool isSharedMemory = false;
    uint8_t* data = JS_GetUint8ArrayData(array, &isSharedMemory, nogc);

    // `array` was just created by JS_NewUint8Array immediately above, so
    // it is guaranteed to be a private, non-shared, non-detached buffer:
    // `data` is non-null and `isSharedMemory` is false here by
    // construction, not by runtime luck.
    memcpy(data, bytes.Data(), length);
  }

  return array;
}

// Reads a JS Uint8Array or ArrayBuffer's bytes without copying into a
// forge-core container -- the returned Span aliases the JS buffer's own
// storage and is valid only for the duration of the call site (per
// JsBindings.md), since a GC can move or (for a resizable/detachable
// buffer) invalidate the underlying storage afterward.
//
// Deliberately narrower than "any ArrayBufferView": JsBindings.md's
// Public API doc comment names exactly "a JS Uint8Array/ArrayBuffer",
// not every typed array element-type variant (Int16Array,
// Float64Array, etc.) -- so a caller passing e.g. a Float64Array gets
// InvalidArgument here rather than this function silently reinterpreting
// its bytes as raw uint8_t, which nothing in the frozen spec asked for.
static forge::core::Result<forge::core::Span<const forge::core::u8>> AsByteSpan(
    JSContext* cx, JS::HandleValue value) {
  using ByteSpan = forge::core::Span<const forge::core::u8>;

  if (!value.isObject()) {
    return forge::core::Result<ByteSpan>(
        forge::core::Failure{forge::core::Error(forge::core::ErrorCode::InvalidArgument)});
  }

  JS::RootedObject obj(cx, &value.toObject());

  if (JS_IsUint8Array(obj)) {
    JS::AutoCheckCannotGC nogc(cx);
    bool isSharedMemory = false;
    uint8_t* data = JS_GetUint8ArrayData(obj, &isSharedMemory, nogc);
    const size_t length = JS_GetTypedArrayByteLength(obj);

    if (isSharedMemory || (length > 0 && data == nullptr)) {
      // A SharedArrayBuffer-backed view could be mutated by another
      // thread concurrently with whatever synchronous I/O the caller is
      // about to do with this span -- not safe to alias directly. A
      // null data pointer with nonzero length means a detached backing
      // buffer -- nothing valid to read.
      return forge::core::Result<ByteSpan>(
          forge::core::Failure{forge::core::Error(forge::core::ErrorCode::InvalidOperation)});
    }

    return forge::core::Result<ByteSpan>(
        ByteSpan(reinterpret_cast<const forge::core::u8*>(data), length));
  }

  if (JS::IsArrayBufferObject(obj)) {
    if (JS::IsDetachedArrayBufferObject(obj)) {
      return forge::core::Result<ByteSpan>(
          forge::core::Failure{forge::core::Error(forge::core::ErrorCode::InvalidOperation)});
    }

    JS::AutoCheckCannotGC nogc(cx);
    bool isSharedMemory = false;
    uint8_t* data = JS::GetArrayBufferData(obj, &isSharedMemory, nogc);
    const size_t length = JS::GetArrayBufferByteLength(obj);

    // isSharedMemory is always false for a plain (non-Shared)
    // ArrayBuffer per js/ArrayBuffer.h's own doc comment on
    // JS::GetArrayBufferData -- checked anyway rather than assumed.
    if (isSharedMemory) {
      return forge::core::Result<ByteSpan>(
          forge::core::Failure{forge::core::Error(forge::core::ErrorCode::InvalidOperation)});
    }

    return forge::core::Result<ByteSpan>(
        ByteSpan(reinterpret_cast<const forge::core::u8*>(data), length));
  }

  return forge::core::Result<ByteSpan>(
      forge::core::Failure{forge::core::Error(forge::core::ErrorCode::InvalidArgument)});
}

//==============================================================================
// Phase 7.2 smoke tests -- run via `forge --self-test` (see main()'s CLI
// dispatch below). Exercises each of the six marshalling helpers above
// against a real, live JSContext/Realm -- the one verification step the
// sandbox this was implemented in genuinely could not do (no real
// js/public build graph available there; see the block comment above and
// HISTORY.md's Phase 7.2 entry for what *was* done there). Not
// JS-visible, not part of Fs.md's surface -- a self-contained internal
// diagnostic for this phase, expected to be complemented (not replaced)
// by real fs.*Sync-driven coverage once Phase 7.3 wires these up.
//==============================================================================

// Evaluates a small JS expression and returns its value. Reuses the exact
// JS::SourceText<mozilla::Utf8Unit>/JS::CompileOptions/JS::Evaluate shape
// main() already uses for real script files below, just against an
// in-memory literal instead of a file -- lets the self-tests construct
// arbitrary JS values (a Symbol, a Uint8Array, an ArrayBuffer, ...) for
// the marshalling helpers to operate on. An expression that throws (e.g.
// `Symbol('x')` fed to something expecting ToString to succeed) leaves
// that exception pending for the caller to handle -- not swallowed here.
static bool EvaluateExpression(JSContext* cx, const char* expr,
                               JS::MutableHandleValue rval) {
  JS::SourceText<mozilla::Utf8Unit> src;

  if (!src.init(cx, expr, strlen(expr), JS::SourceOwnership::Borrowed)) {
    return false;
  }

  JS::CompileOptions opts(cx);
  opts.setFileAndLine("<self-test>", 1);

  return JS::Evaluate(cx, opts, src, rval);
}

static bool SelfTest_ToForgeStringFromLiteral(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(cx, "'hello world'", &val)) {
    fprintf(stderr, "  FAIL ToForgeString(literal): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  forge::core::Result<forge::core::String> result = ToForgeString(cx, val);

  if (result.HasError()) {
    fprintf(stderr, "  FAIL ToForgeString(literal): unexpected error\n");
    return false;
  }
  if (result.Value().View() != forge::core::StringView("hello world")) {
    fprintf(stderr, "  FAIL ToForgeString(literal): content mismatch\n");
    return false;
  }

  return true;
}

static bool SelfTest_ToForgeStringFromNumber(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(cx, "42", &val)) {
    fprintf(stderr, "  FAIL ToForgeString(number): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  forge::core::Result<forge::core::String> result = ToForgeString(cx, val);

  if (result.HasError() || result.Value().View() != forge::core::StringView("42")) {
    fprintf(stderr, "  FAIL ToForgeString(number): expected \"42\"\n");
    return false;
  }

  return true;
}

// JS::ToString (the abstract ToString operation) throws a TypeError for a
// Symbol -- this exercises ToForgeString's failure path and the
// JS_ClearPendingException contract documented in JsBindings.md: after a
// failed Result<T>, no exception should be left pending on `cx`.
static bool SelfTest_ToForgeStringSymbolFails(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(cx, "Symbol('x')", &val)) {
    fprintf(stderr, "  FAIL ToForgeString(symbol): evaluate itself failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  forge::core::Result<forge::core::String> result = ToForgeString(cx, val);
  bool ok = true;

  if (!result.HasError()) {
    fprintf(stderr,
            "  FAIL ToForgeString(symbol): expected failure (ToString "
            "throws on a Symbol)\n");
    ok = false;
  } else if (result.Error().Code() != forge::core::ErrorCode::InvalidArgument) {
    fprintf(stderr, "  FAIL ToForgeString(symbol): expected InvalidArgument\n");
    ok = false;
  }

  if (JS_IsExceptionPending(cx)) {
    fprintf(stderr,
            "  FAIL ToForgeString(symbol): exception left pending -- "
            "violates the documented JS_ClearPendingException contract\n");
    JS_ClearPendingException(cx);
    ok = false;
  }

  return ok;
}

static bool SelfTest_ToForgePathFromLiteral(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(cx, "'a/b/c.txt'", &val)) {
    fprintf(stderr, "  FAIL ToForgePath(literal): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  forge::core::Result<forge::core::Path> result = ToForgePath(cx, val);

  if (result.HasError()) {
    fprintf(stderr, "  FAIL ToForgePath(literal): unexpected error\n");
    return false;
  }
  if (result.Value().View() != forge::core::StringView("a/b/c.txt")) {
    fprintf(stderr, "  FAIL ToForgePath(literal): content mismatch\n");
    return false;
  }

  return true;
}

static bool SelfTest_FromForgeStringRoundTrip(JSContext* cx) {
  const forge::core::StringView text("round-trip");
  JSString* str = FromForgeString(cx, text);

  if (!str) {
    fprintf(stderr, "  FAIL FromForgeString: unexpected null\n");
    return false;
  }

  bool match = false;
  if (!JS_StringEqualsAscii(cx, str, "round-trip", &match) || !match) {
    fprintf(stderr, "  FAIL FromForgeString: content mismatch\n");
    return false;
  }

  return true;
}

static bool SelfTest_Uint8ArrayFromBytes(JSContext* cx) {
  forge::core::Vector<forge::core::u8> bytes;

  const bool pushOk = !bytes.PushBack(forge::core::u8{10}).HasError() &&
                      !bytes.PushBack(forge::core::u8{20}).HasError() &&
                      !bytes.PushBack(forge::core::u8{30}).HasError();

  if (!pushOk) {
    fprintf(stderr, "  FAIL Uint8ArrayFromBytes: test setup PushBack failed\n");
    return false;
  }

  JS::RootedObject array(cx, Uint8ArrayFromBytes(cx, std::move(bytes)));

  if (!array) {
    fprintf(stderr, "  FAIL Uint8ArrayFromBytes: returned null\n");
    return false;
  }
  if (!JS_IsUint8Array(array)) {
    fprintf(stderr, "  FAIL Uint8ArrayFromBytes: result is not a Uint8Array\n");
    return false;
  }

  JS::AutoCheckCannotGC nogc(cx);
  bool isSharedMemory = false;
  uint8_t* data = JS_GetUint8ArrayData(array, &isSharedMemory, nogc);
  const size_t length = JS_GetTypedArrayByteLength(array);

  if (length != 3 || data[0] != 10 || data[1] != 20 || data[2] != 30) {
    fprintf(stderr, "  FAIL Uint8ArrayFromBytes: content mismatch\n");
    return false;
  }

  return true;
}

static bool SelfTest_AsByteSpanUint8Array(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(cx, "new Uint8Array([10, 20, 30])", &val)) {
    fprintf(stderr, "  FAIL AsByteSpan(Uint8Array): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  forge::core::Result<forge::core::Span<const forge::core::u8>> result =
      AsByteSpan(cx, val);

  if (result.HasError()) {
    fprintf(stderr, "  FAIL AsByteSpan(Uint8Array): unexpected error\n");
    return false;
  }

  const forge::core::Span<const forge::core::u8> span = result.Value();
  if (span.Size() != 3 || span[0] != 10 || span[1] != 20 || span[2] != 30) {
    fprintf(stderr, "  FAIL AsByteSpan(Uint8Array): content mismatch\n");
    return false;
  }

  return true;
}

static bool SelfTest_AsByteSpanArrayBuffer(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(cx, "new ArrayBuffer(4)", &val)) {
    fprintf(stderr, "  FAIL AsByteSpan(ArrayBuffer): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  forge::core::Result<forge::core::Span<const forge::core::u8>> result =
      AsByteSpan(cx, val);

  if (result.HasError() || result.Value().Size() != 4) {
    fprintf(stderr, "  FAIL AsByteSpan(ArrayBuffer): expected a 4-byte span\n");
    return false;
  }

  return true;
}

// AsByteSpan is deliberately narrower than "any ArrayBufferView" (see its
// own comment above) -- a Float64Array must be rejected, not silently
// reinterpreted as raw bytes.
static bool SelfTest_AsByteSpanRejectsOtherTypedArray(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(cx, "new Float64Array(2)", &val)) {
    fprintf(stderr, "  FAIL AsByteSpan(Float64Array): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  forge::core::Result<forge::core::Span<const forge::core::u8>> result =
      AsByteSpan(cx, val);

  if (!result.HasError() ||
      result.Error().Code() != forge::core::ErrorCode::InvalidArgument) {
    fprintf(stderr,
            "  FAIL AsByteSpan(Float64Array): expected InvalidArgument\n");
    return false;
  }

  return true;
}

static bool SelfTest_AsByteSpanRejectsNonObject(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(cx, "5", &val)) {
    fprintf(stderr, "  FAIL AsByteSpan(number): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  forge::core::Result<forge::core::Span<const forge::core::u8>> result =
      AsByteSpan(cx, val);

  if (!result.HasError() ||
      result.Error().Code() != forge::core::ErrorCode::InvalidArgument) {
    fprintf(stderr, "  FAIL AsByteSpan(number): expected InvalidArgument\n");
    return false;
  }

  return true;
}

static bool SelfTest_ThrowJsErrorBasicFields(JSContext* cx) {
  forge::core::Result<forge::core::Path> pathResult =
      forge::core::Path::Create(forge::core::StringView("some/file.txt"));

  if (pathResult.HasError()) {
    fprintf(stderr,
            "  FAIL ThrowJsError(basic): test setup Path::Create failed\n");
    return false;
  }

  const forge::core::Error err(forge::core::ErrorCode::NotFound);
  ThrowJsError(cx, err, "readFileSync", &pathResult.Value());

  if (!JS_IsExceptionPending(cx)) {
    fprintf(stderr, "  FAIL ThrowJsError(basic): no exception pending\n");
    return false;
  }

  JS::RootedValue excVal(cx);
  if (!JS_GetPendingException(cx, &excVal) || !excVal.isObject()) {
    fprintf(stderr,
            "  FAIL ThrowJsError(basic): pending exception is not an object\n");
    JS_ClearPendingException(cx);
    return false;
  }

  JS::RootedObject excObj(cx, &excVal.toObject());
  JS::RootedValue codeVal(cx);
  JS::RootedValue syscallVal(cx);
  JS::RootedValue pathVal(cx);
  bool ok = true;

  if (!JS_GetProperty(cx, excObj, "code", &codeVal) ||
      !JS_GetProperty(cx, excObj, "syscall", &syscallVal) ||
      !JS_GetProperty(cx, excObj, "path", &pathVal)) {
    fprintf(stderr, "  FAIL ThrowJsError(basic): could not read properties\n");
    ok = false;
  } else {
    bool match = false;
    if (!codeVal.isString() ||
        !JS_StringEqualsAscii(cx, codeVal.toString(), "NotFound", &match) ||
        !match) {
      fprintf(stderr, "  FAIL ThrowJsError(basic): .code != \"NotFound\"\n");
      ok = false;
    }

    match = false;
    if (!syscallVal.isString() ||
        !JS_StringEqualsAscii(cx, syscallVal.toString(), "readFileSync",
                              &match) ||
        !match) {
      fprintf(stderr,
              "  FAIL ThrowJsError(basic): .syscall != \"readFileSync\"\n");
      ok = false;
    }

    match = false;
    if (!pathVal.isString() ||
        !JS_StringEqualsAscii(cx, pathVal.toString(), "some/file.txt", &match) ||
        !match) {
      fprintf(stderr,
              "  FAIL ThrowJsError(basic): .path != \"some/file.txt\"\n");
      ok = false;
    }
  }

  JS_ClearPendingException(cx);
  return ok;
}

static bool SelfTest_ThrowJsErrorPlatformErrorNativeCode(JSContext* cx) {
  const forge::core::Error err(forge::core::ErrorCode::PlatformError, 5);
  ThrowJsError(cx, err, "statSync");

  if (!JS_IsExceptionPending(cx)) {
    fprintf(stderr,
            "  FAIL ThrowJsError(PlatformError): no exception pending\n");
    return false;
  }

  JS::RootedValue excVal(cx);
  if (!JS_GetPendingException(cx, &excVal) || !excVal.isObject()) {
    fprintf(stderr,
            "  FAIL ThrowJsError(PlatformError): pending exception is not "
            "an object\n");
    JS_ClearPendingException(cx);
    return false;
  }

  JS::RootedObject excObj(cx, &excVal.toObject());
  JS::RootedValue codeVal(cx);
  JS::RootedValue nativeCodeVal(cx);
  bool ok = true;

  if (!JS_GetProperty(cx, excObj, "code", &codeVal) ||
      !JS_GetProperty(cx, excObj, "nativeCode", &nativeCodeVal)) {
    fprintf(stderr,
            "  FAIL ThrowJsError(PlatformError): could not read properties\n");
    ok = false;
  } else {
    bool match = false;
    if (!codeVal.isString() ||
        !JS_StringEqualsAscii(cx, codeVal.toString(), "PlatformError", &match) ||
        !match) {
      fprintf(stderr,
              "  FAIL ThrowJsError(PlatformError): .code != \"PlatformError\"\n");
      ok = false;
    }

    double nativeCodeNum = 0;
    if (!JS::ToNumber(cx, nativeCodeVal, &nativeCodeNum) || nativeCodeNum != 5) {
      fprintf(stderr,
              "  FAIL ThrowJsError(PlatformError): .nativeCode != 5\n");
      ok = false;
    }
  }

  JS_ClearPendingException(cx);
  return ok;
}

// Runs every case above in sequence, printing PASS/FAIL per case, and
// returns true only if every one passed. Invoked via `forge --self-test`
// (see main()'s CLI dispatch below), which now also runs
// RunFsSmokeTests(cx) (Phase 7.3) afterward and prints one combined
// "ALL PASSED"/"FAILURES ABOVE" summary across both suites -- this
// function used to print that summary itself, but doing so here would
// print a possibly-false "ALL PASSED" before the fs suite (which runs
// after this one returns) has even had a chance to fail.
static bool RunMarshallingSmokeTests(JSContext* cx) {
  struct Case {
    const char* name;
    bool (*fn)(JSContext*);
  };

  const Case cases[] = {
      {"ToForgeString(literal)", SelfTest_ToForgeStringFromLiteral},
      {"ToForgeString(number)", SelfTest_ToForgeStringFromNumber},
      {"ToForgeString(symbol) fails cleanly", SelfTest_ToForgeStringSymbolFails},
      {"ToForgePath(literal)", SelfTest_ToForgePathFromLiteral},
      {"FromForgeString round-trip", SelfTest_FromForgeStringRoundTrip},
      {"Uint8ArrayFromBytes", SelfTest_Uint8ArrayFromBytes},
      {"AsByteSpan(Uint8Array)", SelfTest_AsByteSpanUint8Array},
      {"AsByteSpan(ArrayBuffer)", SelfTest_AsByteSpanArrayBuffer},
      {"AsByteSpan rejects Float64Array", SelfTest_AsByteSpanRejectsOtherTypedArray},
      {"AsByteSpan rejects non-object", SelfTest_AsByteSpanRejectsNonObject},
      {"ThrowJsError basic fields", SelfTest_ThrowJsErrorBasicFields},
      {"ThrowJsError PlatformError nativeCode",
       SelfTest_ThrowJsErrorPlatformErrorNativeCode},
  };

  bool allOk = true;

  for (const Case& c : cases) {
    const bool ok = c.fn(cx);
    printf("[self-test] %-40s %s\n", c.name, ok ? "PASS" : "FAIL");
    allOk = allOk && ok;
  }

  return allOk;
}

//==============================================================================
// fs.*Sync JS Bindings (Phase 7.3) -- implements Fs.md's frozen "Public API"
// verbatim against globalThis.fs, built entirely on Phase 7.2's marshalling
// helpers above and File.h/Path.h's already real-build-confirmed
// synchronous I/O (Phase 3). See Fs.md for the full design rationale
// (Non-Goals, Ownership, Error Handling Policy) -- only implementation-
// specific notes are repeated here.
//
// Verification history (per AGENTS.md's "Be Honest"): JS_NewPlainObject's
// signature was confirmed by reading js/src/jsapi.h directly (see the
// include comment above -- it is not exposed under js/public);
// JSPROP_ENUMERATE was confirmed by reading js/public/PropertyDescriptor.h
// directly. File::Open/Read/Write/Seek/SizeInBytes/Exists/MakeDirectory/
// CreateDirectories/Remove/ReadAllBytes/ReadAllText signatures, and the
// exact Win32-error-to-ErrorCode mapping each depends on
// (TranslateWin32Error in forge-core/platform/Win32Error.cpp), were
// confirmed by reading the real File.h/File.cpp/Win32Error.cpp under this
// tree directly -- not guessed. In particular: ERROR_FILE_NOT_FOUND/
// ERROR_PATH_NOT_FOUND both map to ErrorCode::NotFound, and
// ERROR_ALREADY_EXISTS/ERROR_FILE_EXISTS both map to
// ErrorCode::AlreadyExists -- both facts the self-test suite below relies
// on when asserting a specific thrown `.code`.
//==============================================================================

// Resolves writeFileSync/appendFileSync's `data` argument (a JS string,
// Uint8Array, or ArrayBuffer per Fs.md) to a byte span. For a JS string,
// `ownedText` receives the UTF-8-encoded storage the returned span
// aliases -- the caller must keep `ownedText` alive for exactly as long as
// the returned span is used (same "valid only for the duration of the
// call" contract JsBindings.md documents for AsByteSpan itself, which this
// function defers to unchanged for the Uint8Array/ArrayBuffer case).
static forge::core::Result<forge::core::Span<const forge::core::u8>>
ResolveWriteData(JSContext* cx, JS::HandleValue value,
                  forge::core::String* ownedText) {
  using ByteSpan = forge::core::Span<const forge::core::u8>;

  if (value.isString()) {
    forge::core::Result<forge::core::String> text = ToForgeString(cx, value);

    if (text.HasError()) {
      return forge::core::Result<ByteSpan>(forge::core::Failure{text.Error()});
    }

    *ownedText = std::move(text.Value());
    const forge::core::StringView view = ownedText->View();

    return forge::core::Result<ByteSpan>(ByteSpan(
        reinterpret_cast<const forge::core::u8*>(view.Data()), view.Size()));
  }

  return AsByteSpan(cx, value);
}

// Writes every byte of `data` to `file`, retrying past a short write --
// Fs.md's own documented policy: "File::Write's own policy is 'surface a
// short write as-is, don't retry' ... so the retry loop belongs in this
// binding, not in File itself." Throws via ThrowJsError and returns false
// on the first genuine error, or if a Write() call makes no forward
// progress without reporting one (File::Write's contract doesn't rule
// this out explicitly, so this binding guards against looping forever on
// it).
static bool WriteAllBytes(JSContext* cx, forge::core::File& file,
                          forge::core::Span<const forge::core::u8> data,
                          const char* context, const forge::core::Path* path) {
  forge::core::Size written = 0;

  while (written < data.Size()) {
    const forge::core::Span<const forge::core::u8> remaining =
        data.Subspan(written, data.Size() - written);

    forge::core::Result<forge::core::File::SizeType> result = file.Write(remaining);

    if (result.HasError()) {
      ThrowJsError(cx, result.Error(), context, path);
      return false;
    }

    if (result.Value() == 0) {
      ThrowJsError(cx, forge::core::Error(forge::core::ErrorCode::IOError), context, path);
      return false;
    }

    written += result.Value();
  }

  return true;
}

// fs.readFileSync(path, encoding?) -- Uint8Array via File::ReadAllBytes,
// or a string via File::ReadAllText when encoding === "utf8". Any other
// encoding value is a JS-argument-shape error (not a Result<T> failure),
// so it's thrown directly with code InvalidArgument per Fs.md.
static bool FsReadFileSync(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "readFileSync", 1)) {
    return false;
  }

  forge::core::Result<forge::core::Path> path = ToForgePath(cx, args[0]);

  if (path.HasError()) {
    ThrowJsError(cx, path.Error(), "readFileSync");
    return false;
  }

  bool wantUtf8 = false;

  if (args.hasDefined(1)) {
    forge::core::Result<forge::core::String> encoding = ToForgeString(cx, args[1]);

    if (encoding.HasError()) {
      ThrowJsError(cx, encoding.Error(), "readFileSync", &path.Value());
      return false;
    }

    if (encoding.Value().View() == forge::core::StringView("utf8")) {
      wantUtf8 = true;
    } else {
      ThrowJsError(cx, forge::core::Error(forge::core::ErrorCode::InvalidArgument),
                   "readFileSync", &path.Value());
      return false;
    }
  }

  if (wantUtf8) {
    forge::core::Result<forge::core::String> text =
        forge::core::File::ReadAllText(path.Value());

    if (text.HasError()) {
      ThrowJsError(cx, text.Error(), "readFileSync", &path.Value());
      return false;
    }

    JSString* result = FromForgeString(cx, text.Value().View());

    if (!result) {
      return false;  // FromForgeString already reported (OOM or pending exception).
    }

    args.rval().setString(result);
    return true;
  }

  forge::core::Result<forge::core::Vector<forge::core::u8>> bytes =
      forge::core::File::ReadAllBytes(path.Value());

  if (bytes.HasError()) {
    ThrowJsError(cx, bytes.Error(), "readFileSync", &path.Value());
    return false;
  }

  JSObject* array = Uint8ArrayFromBytes(cx, std::move(bytes.Value()));

  if (!array) {
    return false;  // Uint8ArrayFromBytes/JS_NewUint8Array already reported.
  }

  args.rval().setObject(*array);
  return true;
}

// Shared implementation for writeFileSync/appendFileSync -- identical
// shape per Fs.md ("Same as writeFileSync but File::Open(path,
// FileMode::Append)"), differing only in FileMode and the reported
// function name.
static bool FsWriteOrAppendImpl(JSContext* cx, unsigned argc, JS::Value* vp,
                                forge::core::FileMode mode,
                                const char* context) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, context, 2)) {
    return false;
  }

  forge::core::Result<forge::core::Path> path = ToForgePath(cx, args[0]);

  if (path.HasError()) {
    ThrowJsError(cx, path.Error(), context);
    return false;
  }

  forge::core::String ownedText;
  forge::core::Result<forge::core::Span<const forge::core::u8>> data =
      ResolveWriteData(cx, args[1], &ownedText);

  if (data.HasError()) {
    ThrowJsError(cx, data.Error(), context, &path.Value());
    return false;
  }

  forge::core::Result<forge::core::File> file =
      forge::core::File::Open(path.Value(), mode);

  if (file.HasError()) {
    ThrowJsError(cx, file.Error(), context, &path.Value());
    return false;
  }

  if (!WriteAllBytes(cx, file.Value(), data.Value(), context, &path.Value())) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool FsWriteFileSync(JSContext* cx, unsigned argc, JS::Value* vp) {
  return FsWriteOrAppendImpl(cx, argc, vp, forge::core::FileMode::Write, "writeFileSync");
}

static bool FsAppendFileSync(JSContext* cx, unsigned argc, JS::Value* vp) {
  return FsWriteOrAppendImpl(cx, argc, vp, forge::core::FileMode::Append, "appendFileSync");
}

// fs.existsSync(path) -- direct File::Exists. Per Fs.md's Error Handling
// Policy (unconditional: "every Result<T>/Result<void> failure throws"),
// a genuine File::Exists failure (e.g. a permission error probing the
// path) still throws here -- a deliberate divergence from Node's own
// fs.existsSync, which swallows every error and returns false. Fs.md's
// own Design Goals explicitly permit divergence from Node's exact
// behavior wherever forge-core's semantics differ, and forge-core draws
// a real distinction between "doesn't exist" (false, not an error) and
// "couldn't tell" (an Error) that this binding preserves rather than
// collapsing the two.
static bool FsExistsSync(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "existsSync", 1)) {
    return false;
  }

  forge::core::Result<forge::core::Path> path = ToForgePath(cx, args[0]);

  if (path.HasError()) {
    ThrowJsError(cx, path.Error(), "existsSync");
    return false;
  }

  forge::core::Result<bool> exists = forge::core::File::Exists(path.Value());

  if (exists.HasError()) {
    ThrowJsError(cx, exists.Error(), "existsSync", &path.Value());
    return false;
  }

  args.rval().setBoolean(exists.Value());
  return true;
}

// fs.mkdirSync(path, options?) -- options.recursive selects
// File::CreateDirectories ("mkdir -p", tolerates an already-existing
// ancestor -- including the target itself, per its own Result<void>
// bubbling logic in File.cpp) vs. the non-recursive File::MakeDirectory
// (fails with AlreadyExists if the target itself already exists, and
// NotFound if any ancestor is missing).
static bool FsMkdirSync(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "mkdirSync", 1)) {
    return false;
  }

  forge::core::Result<forge::core::Path> path = ToForgePath(cx, args[0]);

  if (path.HasError()) {
    ThrowJsError(cx, path.Error(), "mkdirSync");
    return false;
  }

  bool recursive = false;

  if (args.hasDefined(1)) {
    if (!args[1].isObject()) {
      ThrowJsError(cx, forge::core::Error(forge::core::ErrorCode::InvalidArgument),
                   "mkdirSync", &path.Value());
      return false;
    }

    JS::RootedObject options(cx, &args[1].toObject());
    JS::RootedValue recursiveVal(cx);

    if (!JS_GetProperty(cx, options, "recursive", &recursiveVal)) {
      return false;
    }

    if (!recursiveVal.isUndefined()) {
      recursive = JS::ToBoolean(recursiveVal);
    }
  }

  forge::core::Result<void> result =
      recursive ? forge::core::File::CreateDirectories(path.Value())
                : forge::core::File::MakeDirectory(path.Value());

  if (result.HasError()) {
    ThrowJsError(cx, result.Error(), "mkdirSync", &path.Value());
    return false;
  }

  args.rval().setUndefined();
  return true;
}

// fs.rmSync(path) -- direct File::Remove (Win32 DeleteFileW). File-removal
// only, per Fs.md's Non-Goals; calling this on a directory surfaces
// whatever File::Remove itself returns for that case (a RemoveDirectoryW
// retry, per File.cpp -- not re-derived here, this binding is a thin
// passthrough).
static bool FsRmSync(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "rmSync", 1)) {
    return false;
  }

  forge::core::Result<forge::core::Path> path = ToForgePath(cx, args[0]);

  if (path.HasError()) {
    ThrowJsError(cx, path.Error(), "rmSync");
    return false;
  }

  forge::core::Result<void> result = forge::core::File::Remove(path.Value());

  if (result.HasError()) {
    ThrowJsError(cx, result.Error(), "rmSync", &path.Value());
    return false;
  }

  args.rval().setUndefined();
  return true;
}

// fs.statSync(path) -- { size }. Opens the file (FileMode::Read, so a
// missing path throws NotFound via the same path readFileSync's failure
// case uses), reads SizeInBytes(), closes it. Per Fs.md's Non-Goals, size
// is the entire returned shape -- no isFile()/isDirectory()/timestamps
// until forge-core actually supports querying them.
static bool FsStatSync(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "statSync", 1)) {
    return false;
  }

  forge::core::Result<forge::core::Path> path = ToForgePath(cx, args[0]);

  if (path.HasError()) {
    ThrowJsError(cx, path.Error(), "statSync");
    return false;
  }

  forge::core::Result<forge::core::File> file =
      forge::core::File::Open(path.Value(), forge::core::FileMode::Read);

  if (file.HasError()) {
    ThrowJsError(cx, file.Error(), "statSync", &path.Value());
    return false;
  }

  forge::core::Result<forge::core::u64> size = file.Value().SizeInBytes();

  if (size.HasError()) {
    ThrowJsError(cx, size.Error(), "statSync", &path.Value());
    return false;
  }

  file.Value().Close();

  JS::RootedObject stats(cx, JS_NewPlainObject(cx));

  if (!stats) {
    return false;  // JS_NewPlainObject reports its own failure (OOM).
  }

  // A file larger than 2^53 bytes would lose precision as a JS double --
  // not addressed here, same limitation Fs.md's own spec already accepts
  // ("size: number"), and astronomically outside any real workload this
  // runtime targets.
  if (!JS_DefineProperty(cx, stats, "size", static_cast<double>(size.Value()),
                         JSPROP_ENUMERATE)) {
    return false;
  }

  args.rval().setObject(*stats);
  return true;
}

// Creates globalThis.fs and defines every method above on it, per
// JsBindings.md's naming convention (a namespaced global object, not more
// flat globals). Called once from main(), immediately after the existing
// flat-global JS_DefineFunction calls.
static bool DefineFsNamespace(JSContext* cx, JS::HandleObject global) {
  JS::RootedObject fs(cx, JS_NewPlainObject(cx));

  if (!fs) {
    return false;
  }

  if (!JS_DefineFunction(cx, fs, "readFileSync", FsReadFileSync, 1, 0) ||
      !JS_DefineFunction(cx, fs, "writeFileSync", FsWriteFileSync, 2, 0) ||
      !JS_DefineFunction(cx, fs, "appendFileSync", FsAppendFileSync, 2, 0) ||
      !JS_DefineFunction(cx, fs, "existsSync", FsExistsSync, 1, 0) ||
      !JS_DefineFunction(cx, fs, "mkdirSync", FsMkdirSync, 1, 0) ||
      !JS_DefineFunction(cx, fs, "rmSync", FsRmSync, 1, 0) ||
      !JS_DefineFunction(cx, fs, "statSync", FsStatSync, 1, 0)) {
    return false;
  }

  return JS_DefineProperty(cx, global, "fs", fs, 0);
}

//==============================================================================
// Phase 7.3 smoke tests -- run via `forge --self-test` (see main()'s CLI
// dispatch below), immediately after the Phase 7.2 marshalling suite.
// Exercises every fs.*Sync method above through the real JS-visible
// globalThis.fs surface (each case is a JS snippet run via
// EvaluateExpression, exactly like Phase 7.2's suite above) against real
// files under a scratch directory relative to whatever cwd `forge
// --self-test` is run from. Fulfills Fs.md's own Acceptance Criteria: "at
// least one success path and one failure path per method."
//
// Fixtures are deliberately deterministic, fixed-name paths (no
// Date.now()/Math.random()) so repeated runs don't accumulate garbage --
// each case either truncates its own file on write (FileMode::Write) or
// explicitly tolerates the one idempotency edge case that isn't already
// handled by forge-core itself (File::MakeDirectory's non-recursive
// AlreadyExists on a second run -- see SelfTest_FsMkdirSyncNonRecursive
// below). The scratch directory itself is never removed (fs.rmSync is
// file-removal only, per Fs.md's Non-Goals) -- a harmless, documented
// byproduct of running this suite.
//==============================================================================

static const char* kFsScratchDir = "forge-selftest-fs-scratch";

// Ensures the scratch directory exists, via forge-core directly (not
// through fs.mkdirSync -- that's exercised by its own test cases below,
// and shouldn't be entangled with fixture setup every other case relies
// on).
static bool SelfTest_FsEnsureScratchDir() {
  forge::core::Result<forge::core::Path> dir =
      forge::core::Path::Create(forge::core::StringView(kFsScratchDir));

  if (dir.HasError()) {
    fprintf(stderr, "  FAIL fs scratch dir: Path::Create failed\n");
    return false;
  }

  forge::core::Result<void> created = forge::core::File::CreateDirectories(dir.Value());

  if (created.HasError()) {
    fprintf(stderr, "  FAIL fs scratch dir: CreateDirectories failed\n");
    return false;
  }

  return true;
}

// Shared by every failure-path case below: asserts an exception is
// currently pending on `cx` and that its `.code` property (set by
// ThrowJsError -- see JsBindings.md) equals `expectedCode`. Does not clear
// the exception -- every caller does that itself right after, matching
// Phase 7.2's SelfTest_ThrowJsError* cases above.
static bool CheckPendingErrorCode(JSContext* cx, const char* label,
                                  const char* expectedCode) {
  if (!JS_IsExceptionPending(cx)) {
    fprintf(stderr, "  FAIL %s: no exception pending\n", label);
    return false;
  }

  JS::RootedValue excVal(cx);
  if (!JS_GetPendingException(cx, &excVal) || !excVal.isObject()) {
    fprintf(stderr, "  FAIL %s: pending exception is not an object\n", label);
    return false;
  }

  JS::RootedObject excObj(cx, &excVal.toObject());
  JS::RootedValue codeVal(cx);

  if (!JS_GetProperty(cx, excObj, "code", &codeVal)) {
    fprintf(stderr, "  FAIL %s: could not read .code\n", label);
    return false;
  }

  bool match = false;
  if (!codeVal.isString() ||
      !JS_StringEqualsAscii(cx, codeVal.toString(), expectedCode, &match) || !match) {
    fprintf(stderr, "  FAIL %s: .code != \"%s\"\n", label, expectedCode);
    return false;
  }

  return true;
}

// --- Success paths ---

static bool SelfTest_FsWriteFileSyncStringThenReadBytes(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  fs.writeFileSync('forge-selftest-fs-scratch/roundtrip.txt', 'Hello, Forge!');"
      "  return fs.readFileSync('forge-selftest-fs-scratch/roundtrip.txt');"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL writeFileSync(string)+readFileSync(bytes): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  if (!val.isObject()) {
    fprintf(stderr, "  FAIL writeFileSync(string)+readFileSync(bytes): result is not an object\n");
    return false;
  }

  JS::RootedObject obj(cx, &val.toObject());

  if (!JS_IsUint8Array(obj)) {
    fprintf(stderr, "  FAIL writeFileSync(string)+readFileSync(bytes): result is not a Uint8Array\n");
    return false;
  }

  JS::AutoCheckCannotGC nogc(cx);
  bool isSharedMemory = false;
  uint8_t* data = JS_GetUint8ArrayData(obj, &isSharedMemory, nogc);
  const size_t length = JS_GetTypedArrayByteLength(obj);

  const char* expected = "Hello, Forge!";
  const size_t expectedLen = strlen(expected);

  if (length != expectedLen || memcmp(data, expected, expectedLen) != 0) {
    fprintf(stderr, "  FAIL writeFileSync(string)+readFileSync(bytes): content mismatch\n");
    return false;
  }

  return true;
}

static bool SelfTest_FsReadFileSyncUtf8Encoding(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  fs.writeFileSync('forge-selftest-fs-scratch/utf8.txt', 'utf8 round trip');"
      "  return fs.readFileSync('forge-selftest-fs-scratch/utf8.txt', 'utf8');"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL readFileSync(utf8): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  bool match = false;
  if (!val.isString() ||
      !JS_StringEqualsAscii(cx, val.toString(), "utf8 round trip", &match) || !match) {
    fprintf(stderr, "  FAIL readFileSync(utf8): content mismatch\n");
    return false;
  }

  return true;
}

static bool SelfTest_FsWriteFileSyncUint8ArrayData(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  fs.writeFileSync('forge-selftest-fs-scratch/uint8array.txt', new Uint8Array([72, 105, 33]));"
      "  return fs.readFileSync('forge-selftest-fs-scratch/uint8array.txt', 'utf8');"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL writeFileSync(Uint8Array): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  bool match = false;
  if (!val.isString() ||
      !JS_StringEqualsAscii(cx, val.toString(), "Hi!", &match) || !match) {
    fprintf(stderr, "  FAIL writeFileSync(Uint8Array): content mismatch\n");
    return false;
  }

  return true;
}

static bool SelfTest_FsAppendFileSyncConcatenates(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  fs.writeFileSync('forge-selftest-fs-scratch/append.txt', 'abc');"
      "  fs.appendFileSync('forge-selftest-fs-scratch/append.txt', 'def');"
      "  return fs.readFileSync('forge-selftest-fs-scratch/append.txt', 'utf8');"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL appendFileSync: evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  bool match = false;
  if (!val.isString() ||
      !JS_StringEqualsAscii(cx, val.toString(), "abcdef", &match) || !match) {
    fprintf(stderr, "  FAIL appendFileSync: content mismatch (expected \"abcdef\")\n");
    return false;
  }

  return true;
}

static bool SelfTest_FsExistsSyncTrueForCreatedFile(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  fs.writeFileSync('forge-selftest-fs-scratch/exists.txt', 'x');"
      "  return fs.existsSync('forge-selftest-fs-scratch/exists.txt');"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL existsSync(true): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  if (!val.isBoolean() || !val.toBoolean()) {
    fprintf(stderr, "  FAIL existsSync(true): expected true\n");
    return false;
  }

  return true;
}

static bool SelfTest_FsExistsSyncFalseForMissingFile(JSContext* cx) {
  JS::RootedValue val(cx);

  if (!EvaluateExpression(
          cx, "fs.existsSync('forge-selftest-fs-scratch/definitely-does-not-exist.txt')",
          &val)) {
    fprintf(stderr, "  FAIL existsSync(false): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  if (!val.isBoolean() || val.toBoolean()) {
    fprintf(stderr, "  FAIL existsSync(false): expected false\n");
    return false;
  }

  return true;
}

// File::MakeDirectory (non-recursive) is not documented as idempotent --
// unlike CreateDirectories, it doesn't tolerate the target itself already
// existing (confirmed via File.cpp: a second CreateDirectoryW on the same
// path returns ERROR_ALREADY_EXISTS, mapped to ErrorCode::AlreadyExists).
// Running this suite twice in a row against the same on-disk scratch
// directory is expected and normal, so this case tolerates that one
// specific, documented outcome rather than treating it as a failure -- the
// postcondition checked below (the directory exists) holds either way.
static bool SelfTest_FsMkdirSyncNonRecursive(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  try {"
      "    fs.mkdirSync('forge-selftest-fs-scratch/mkdir-plain');"
      "  } catch (e) {"
      "    if (e.code !== 'AlreadyExists') { throw e; }"
      "  }"
      "  return fs.existsSync('forge-selftest-fs-scratch/mkdir-plain');"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL mkdirSync(non-recursive): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  if (!val.isBoolean() || !val.toBoolean()) {
    fprintf(stderr, "  FAIL mkdirSync(non-recursive): directory does not exist afterward\n");
    return false;
  }

  return true;
}

static bool SelfTest_FsMkdirSyncRecursiveNestedDirs(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  fs.mkdirSync('forge-selftest-fs-scratch/mkdir-recursive/a/b/c', { recursive: true });"
      "  return fs.existsSync('forge-selftest-fs-scratch/mkdir-recursive/a/b/c');"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL mkdirSync(recursive): evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  if (!val.isBoolean() || !val.toBoolean()) {
    fprintf(stderr, "  FAIL mkdirSync(recursive): nested directory does not exist afterward\n");
    return false;
  }

  return true;
}

static bool SelfTest_FsRmSyncRemovesFile(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  fs.writeFileSync('forge-selftest-fs-scratch/rm-me.txt', 'temp');"
      "  fs.rmSync('forge-selftest-fs-scratch/rm-me.txt');"
      "  return fs.existsSync('forge-selftest-fs-scratch/rm-me.txt');"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL rmSync: evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  if (!val.isBoolean() || val.toBoolean()) {
    fprintf(stderr, "  FAIL rmSync: file still exists after removal\n");
    return false;
  }

  return true;
}

static bool SelfTest_FsStatSyncReportsSize(JSContext* cx) {
  JS::RootedValue val(cx);

  const char* expr =
      "(() => {"
      "  fs.writeFileSync('forge-selftest-fs-scratch/stat-me.txt', 'twelve bytes');"
      "  return fs.statSync('forge-selftest-fs-scratch/stat-me.txt').size;"
      "})()";

  if (!EvaluateExpression(cx, expr, &val)) {
    fprintf(stderr, "  FAIL statSync: evaluate failed\n");
    JS_ClearPendingException(cx);
    return false;
  }

  double size = 0;
  if (!JS::ToNumber(cx, val, &size) || size != 12) {
    fprintf(stderr, "  FAIL statSync: expected size 12, got a different value\n");
    return false;
  }

  return true;
}

// --- Failure paths (at least one per method, per Fs.md's Acceptance
// Criteria) ---

static bool SelfTest_FsReadFileSyncNotFoundFails(JSContext* cx) {
  JS::RootedValue val(cx);

  if (EvaluateExpression(
          cx, "fs.readFileSync('forge-selftest-fs-scratch/definitely-does-not-exist.txt')",
          &val)) {
    fprintf(stderr, "  FAIL readFileSync(NotFound): expected a thrown error\n");
    return false;
  }

  const bool ok = CheckPendingErrorCode(cx, "readFileSync(NotFound)", "NotFound");
  JS_ClearPendingException(cx);
  return ok;
}

static bool SelfTest_FsWriteFileSyncInvalidDataFails(JSContext* cx) {
  JS::RootedValue val(cx);

  if (EvaluateExpression(
          cx, "fs.writeFileSync('forge-selftest-fs-scratch/invalid-data.txt', 42)", &val)) {
    fprintf(stderr, "  FAIL writeFileSync(InvalidArgument): expected a thrown error\n");
    return false;
  }

  const bool ok = CheckPendingErrorCode(cx, "writeFileSync(InvalidArgument)", "InvalidArgument");
  JS_ClearPendingException(cx);
  return ok;
}

static bool SelfTest_FsAppendFileSyncMissingParentFails(JSContext* cx) {
  JS::RootedValue val(cx);

  if (EvaluateExpression(
          cx,
          "fs.appendFileSync("
          "'forge-selftest-fs-scratch/definitely-missing-parent/child.txt', 'x')",
          &val)) {
    fprintf(stderr, "  FAIL appendFileSync(NotFound): expected a thrown error\n");
    return false;
  }

  const bool ok = CheckPendingErrorCode(cx, "appendFileSync(NotFound)", "NotFound");
  JS_ClearPendingException(cx);
  return ok;
}

// existsSync's own File::Exists call has no easy, portable way to force a
// genuine I/O-level failure from a self-test -- this instead exercises the
// binding's path-conversion failure path (ToForgePath on a Symbol, the
// same mechanism SelfTest_ToForgeStringSymbolFails above already proves
// correct), which is a real failure path through this exact binding, just
// not through File::Exists specifically. Documented here rather than
// silently presented as something it isn't.
static bool SelfTest_FsExistsSyncSymbolPathFails(JSContext* cx) {
  JS::RootedValue val(cx);

  if (EvaluateExpression(cx, "fs.existsSync(Symbol('x'))", &val)) {
    fprintf(stderr, "  FAIL existsSync(InvalidArgument): expected a thrown error\n");
    return false;
  }

  const bool ok = CheckPendingErrorCode(cx, "existsSync(InvalidArgument)", "InvalidArgument");
  JS_ClearPendingException(cx);
  return ok;
}

static bool SelfTest_FsMkdirSyncMissingParentNonRecursiveFails(JSContext* cx) {
  JS::RootedValue val(cx);

  if (EvaluateExpression(
          cx,
          "fs.mkdirSync("
          "'forge-selftest-fs-scratch/definitely-missing-parent/child')",
          &val)) {
    fprintf(stderr, "  FAIL mkdirSync(NotFound): expected a thrown error\n");
    return false;
  }

  const bool ok = CheckPendingErrorCode(cx, "mkdirSync(NotFound)", "NotFound");
  JS_ClearPendingException(cx);
  return ok;
}

static bool SelfTest_FsRmSyncNotFoundFails(JSContext* cx) {
  JS::RootedValue val(cx);

  if (EvaluateExpression(
          cx, "fs.rmSync('forge-selftest-fs-scratch/definitely-does-not-exist.txt')", &val)) {
    fprintf(stderr, "  FAIL rmSync(NotFound): expected a thrown error\n");
    return false;
  }

  const bool ok = CheckPendingErrorCode(cx, "rmSync(NotFound)", "NotFound");
  JS_ClearPendingException(cx);
  return ok;
}

static bool SelfTest_FsStatSyncNotFoundFails(JSContext* cx) {
  JS::RootedValue val(cx);

  if (EvaluateExpression(
          cx, "fs.statSync('forge-selftest-fs-scratch/definitely-does-not-exist.txt')", &val)) {
    fprintf(stderr, "  FAIL statSync(NotFound): expected a thrown error\n");
    return false;
  }

  const bool ok = CheckPendingErrorCode(cx, "statSync(NotFound)", "NotFound");
  JS_ClearPendingException(cx);
  return ok;
}

// Runs every case above in sequence, printing PASS/FAIL per case (same
// convention as RunMarshallingSmokeTests above), and returns true only if
// every one passed -- including the scratch-dir fixture setup itself.
static bool RunFsSmokeTests(JSContext* cx) {
  if (!SelfTest_FsEnsureScratchDir()) {
    printf("[self-test] %-40s %s\n", "fs scratch dir setup", "FAIL");
    return false;
  }

  struct Case {
    const char* name;
    bool (*fn)(JSContext*);
  };

  const Case cases[] = {
      {"fs.writeFileSync(string)+readFileSync(bytes)",
       SelfTest_FsWriteFileSyncStringThenReadBytes},
      {"fs.readFileSync(utf8)", SelfTest_FsReadFileSyncUtf8Encoding},
      {"fs.writeFileSync(Uint8Array)", SelfTest_FsWriteFileSyncUint8ArrayData},
      {"fs.appendFileSync concatenates", SelfTest_FsAppendFileSyncConcatenates},
      {"fs.existsSync(true)", SelfTest_FsExistsSyncTrueForCreatedFile},
      {"fs.existsSync(false)", SelfTest_FsExistsSyncFalseForMissingFile},
      {"fs.mkdirSync(non-recursive)", SelfTest_FsMkdirSyncNonRecursive},
      {"fs.mkdirSync(recursive)", SelfTest_FsMkdirSyncRecursiveNestedDirs},
      {"fs.rmSync removes file", SelfTest_FsRmSyncRemovesFile},
      {"fs.statSync reports size", SelfTest_FsStatSyncReportsSize},
      {"fs.readFileSync fails (NotFound)", SelfTest_FsReadFileSyncNotFoundFails},
      {"fs.writeFileSync fails (InvalidArgument)", SelfTest_FsWriteFileSyncInvalidDataFails},
      {"fs.appendFileSync fails (NotFound)", SelfTest_FsAppendFileSyncMissingParentFails},
      {"fs.existsSync fails (InvalidArgument)", SelfTest_FsExistsSyncSymbolPathFails},
      {"fs.mkdirSync fails (NotFound)", SelfTest_FsMkdirSyncMissingParentNonRecursiveFails},
      {"fs.rmSync fails (NotFound)", SelfTest_FsRmSyncNotFoundFails},
      {"fs.statSync fails (NotFound)", SelfTest_FsStatSyncNotFoundFails},
  };

  bool allOk = true;

  for (const Case& c : cases) {
    const bool ok = c.fn(cx);
    printf("[self-test] %-40s %s\n", c.name, ok ? "PASS" : "FAIL");
    allOk = allOk && ok;
  }

  return allOk;
}

int main(int argc, char* argv[]) {
      if (argc == 1) {
        printf("Forge JavaScript Runtime\n");
        printf("Usage:\n");
        printf("  forge <file.js>\n");
        printf("  forge --version\n");
        printf("  forge --help\n");
        return 0;
    }

    std::string command = argv[1];

    if (command == "--version") {
        printf("Forge v0.1.0\n");
        return 0;
    }

    if (command == "--help") {
        printf("Forge JavaScript Runtime\n\n");
        printf("Commands:\n");
        printf("  forge <file.js>    Run a JavaScript file\n");
        printf("  forge --version    Show runtime version\n");
        printf("  forge --help       Show this help\n");
        return 0;
    }

  if (!JS_Init()) { // this is where engine start
    return 1;
  }

  // 8MB (the original value here) is a GC-trigger threshold, not a hard
  // ceiling, but it was small enough to genuinely run out under real load:
  // microtask-bench.js queues 200,000 distinct closures before any of them
  // run, all simultaneously reachable (and therefore uncollectable) from
  // the microtask queue, which exceeded it and surfaced as a JS-level
  // "out of memory" exception. 512MB gives real workloads (Node/Bun both
  // default to well over 1GB) comfortable headroom without being an
  // arbitrary huge number.
  JSContext* cx = JS_NewContext(512L * 1024 * 1024); // new context

  if (!cx) {
    return 1;
  }

  if (!JS::InitSelfHostedCode(cx)) {
    return 1;
  } // SpiderMonkey's own JavaScript library.
  Runtime runtime(cx);

  forge::core::Result<void> initialized = runtime.Initialize();

  if (initialized.HasError()) {
    fprintf(stderr, "Forge: failed to initialize the event loop (native code %d)\n",
            initialized.Error().NativeCode());
    return 1;
  }

  // Everything from here down that touches `global` (including the
  // JSAutoRealm block below) lives in this outer scope, so `global` — a
  // JS::RootedObject, which unregisters itself from `cx`'s rooting list
  // when destroyed — is destroyed *before* JS_DestroyContext(cx) below,
  // not after. Originally `global` was declared at the same scope as
  // `cx`/`runtime`, so its destructor only ran when main() itself
  // returned, which is after JS_DestroyContext(cx) already ran a few
  // lines down — a real use-after-free (JS::Rooted<T>'s destructor
  // dereferences a now-dangling context pointer to unlink itself). This
  // was a pre-existing bug, not something the event-loop rewrite
  // introduced — it just never had a chance to fire before, because no
  // script had ever run to natural completion through a real
  // `mach`-built forge.exe until the Phase 0 benchmark scripts (see
  // HISTORY.md): hello.js's uncancelled setInterval never lets main()
  // reach this point on its own, so Ctrl+C always killed the process
  // first.
  {
    JS::RealmOptions options;
    options.creationOptions().setSharedMemoryAndAtomicsEnabled(true); // This Realm is allowed to use SharedArrayBuffer Atomics

    JS::RootedObject global(
        cx, JS_NewGlobalObject(cx, &globalClass, nullptr, JS::FireOnNewGlobalHook,
                               options)); // JavaScript universe is born
    JS::SetReservedSlot(global, RuntimeSlot, JS::PrivateValue(&runtime));
    if (!global) {
      return 1;
    }

    {
      JSAutoRealm ar(cx, global); // JSAutoRealm enters that JavaScript world.

      if (!JS::InitRealmStandardClasses(cx)) {
        return 1;
      } // Initialize standard JS class constructors, prototypes, and any top-level functions and constants associated with the standard classes
      JS_DefineFunction(cx, global, "clearTimeout", ClearTimeout, 1, 0);
      if (!JS_DefineFunction(cx, global, "setInterval", SetInterval, 2, 0)) {
        return 1;
      }
      JS_DefineFunction(cx, global, "queueMicrotask", QueueMicrotask, 1, 0);
      if (!JS_DefineFunction(cx, global, "print", Print, 0, 0)) {
        return 1;
      }
      if (!JS_DefineFunction(cx, global, "setTimeout", SetTimeout, 2, 0)) {
        return 1;
      }

      // globalThis.fs (Phase 7.3) -- defined unconditionally, same as
      // every flat global above, so it's available both to `forge
      // --self-test` (RunFsSmokeTests below) and to any real script
      // passed as argv[1].
      if (!DefineFsNamespace(cx, global)) {
        return 1;
      }

      // `forge --self-test` runs the Phase 7.2 marshalling smoke tests
      // (see RunMarshallingSmokeTests above) and the Phase 7.3 fs.*Sync
      // smoke tests (see RunFsSmokeTests above) against this real,
      // already-initialized JSContext/Realm, instead of treating argv[1]
      // as a script path below -- mirrors the --version/--help dispatch
      // above, just handled here (rather than before JS_Init()) because
      // it genuinely needs a live JSContext/Realm to run. Both suites
      // always run, even if the first one fails, so a single invocation
      // surfaces every failure at once rather than stopping at the first.
      if (command == "--self-test") {
        const bool marshallingOk = RunMarshallingSmokeTests(cx);
        const bool fsOk = RunFsSmokeTests(cx);
        const bool ok = marshallingOk && fsOk;
        printf("[self-test] %s\n", ok ? "ALL PASSED" : "FAILURES ABOVE");
        runtime.Shutdown();
        return ok ? 0 : 1;
      }

      // Note: argc < 2 is unreachable here — the argc == 1 case already
      // returned at the top of main(), so argc >= 2 is guaranteed by this
      // point. The old dead check has been removed.

      forge::core::Result<forge::core::Path> scriptPath =
          forge::core::Path::Create(forge::core::StringView(argv[1]));

      if (scriptPath.HasError()) {
        fprintf(stderr, "Forge: invalid script path %s\n", argv[1]);
        return 1;
      }

      forge::core::Result<forge::core::String> source =
          forge::core::File::ReadAllText(scriptPath.Value());

      if (source.HasError()) {
        fprintf(stderr, "Forge: cannot read %s (error code %d)\n", argv[1],
                static_cast<int>(source.Error().Code()));
        return 1;
      }

      JS::SourceText<mozilla::Utf8Unit> src;

      if (!src.init(cx, source.Value().Data(), source.Value().Size(),
                    JS::SourceOwnership::Borrowed)) {
        return 1;
      }

      JS::RootedValue rval(cx);

      JS::CompileOptions opts(cx);
      opts.setFileAndLine(argv[1], 1);

      if (!JS::Evaluate(cx, opts, src, &rval)) {
        ReportPendingException(cx, "script evaluation");
        // The script may have already called setTimeout/setInterval before
        // failing — release those before cx goes away, same reasoning as
        // the runtime.Shutdown() call after the event loop below.
        runtime.Shutdown();
        return 1;
      }

      if (rval.isNumber()) {
        printf("Result = %f\n", rval.toNumber());
      }
      runtime.run();
    }
  } // end of the `global`-owning scope — global is destroyed here, cx is still alive.

  runtime.Shutdown();
  JS_DestroyContext(cx);
  JS_ShutDown();

  return 0;
}
