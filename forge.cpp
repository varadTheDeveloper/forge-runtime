#include <stdio.h>
#include <string.h>
#include "js/CallAndConstruct.h"
#include "js/String.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <thread>
#include <memory>
#include "js/CompileOptions.h"
#include "js/CompilationAndEvaluation.h"
#include "js/Context.h"
#include "js/GlobalObject.h"
#include "js/Initialization.h"
#include "js/RealmOptions.h"
#include "js/RootingAPI.h"
#include "js/SourceText.h"
#include "js/Promise.h"
#include "js/Value.h"

#include "js/CallArgs.h"
#include "js/CharacterEncoding.h"
#include "js/PropertyAndElement.h"
#include "js/Conversions.h"
#include "js/Exception.h"
#include "js/ErrorReport.h"
// cx points to SpiderMonkey's context object.
static const JSClass globalClass = {
    "global",
    JSCLASS_GLOBAL_FLAGS,
    &JS::DefaultGlobalClassOps,
};

struct Microtask {
  JS::Heap<JSObject*> callback;

  Microtask() {}
};

std::vector<std::unique_ptr<Microtask>> microtasks;
class ForgeJobQueue : public JS::JobQueue
{
public:
    bool empty() const override
    {
        return microtasks.empty();
    }
    bool isDrainingStopped() const override
{
    return false;
}
bool getHostDefinedData(
    JSContext* cx,
    JS::MutableHandle<JSObject*> data
) const override
{
    data.set(nullptr);

    return true;
}
void runJobs(JSContext* cx) override
{
}
};
ForgeJobQueue jobQueue;
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
struct Timer {
  int id;
  uint64_t dueTime;

  JS::Heap<JSObject*> callback;
  JS::PersistentRootedVector<JS::Value> arguments;
  uint64_t delay;
  bool repeat;
  Timer(JSContext* cx) : id(0), dueTime(0), arguments(cx) {}
};
std::vector<std::unique_ptr<Timer>> timers;
static int nextTimerId = 1;
uint64_t GetTimeMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
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

  auto timer = std::make_unique<Timer>(cx);

  JS::RootedObject callback(cx, &args[0].toObject());
  printf("Is callable: %d\n", JS::IsCallable(&args[0].toObject()));
  timer->callback = callback;

  timer->id = nextTimerId++;

  timer->delay = (uint64_t)delay;

  timer->repeat = false;

  timer->dueTime = GetTimeMs() + timer->delay;
  for (unsigned i = 2; i < args.length(); i++) {
    if (!timer->arguments.append(args[i])) {
      return false;
    }
  }

  int id = timer->id;

  timers.push_back(std::move(timer));

  printf("Timer %d registered (%0.f ms)\n", id, delay);

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
  printf("clearTimeout(%d)\n", id);

  for (auto it = timers.begin(); it != timers.end();) {
    if ((*it)->id == id) {
      printf("Removing timer %d\n", id);

      timers.erase(it);

      break;
    }

    ++it;
  }

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

  auto timer = std::make_unique<Timer>(cx);

  JS::RootedObject callback(cx, &args[0].toObject());
  printf("Is callable: %d\n", JS::IsCallable(&args[0].toObject()));
  timer->callback = callback;

  timer->id = nextTimerId++;

  timer->delay = (uint64_t)delay;

  timer->repeat = true;

  timer->dueTime = GetTimeMs() + timer->delay;
  for (unsigned i = 2; i < args.length(); i++) {
    if (!timer->arguments.append(args[i])) {
      return false;
    }
  }

  int id = timer->id;

  timers.push_back(std::move(timer));

  printf("Timer %d registered (%0.f ms)\n", id, delay);

  args.rval().setInt32(id);

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

  auto task = std::make_unique<Microtask>();

  JS::RootedObject callback(cx, &args[0].toObject());

  task->callback = callback;

  microtasks.push_back(std::move(task));
  args.rval().setUndefined();

  return true;
}
int main(int argc, char* argv[]) {
  if (!JS_Init()) {
    return 1;
  }

  JSContext* cx = JS_NewContext(8L * 1024 * 1024);

  if (!cx) {
    return 1;
  }

  if (!JS::InitSelfHostedCode(cx)) {
    return 1;
  }

  JS::RealmOptions options;
  options.creationOptions().setSharedMemoryAndAtomicsEnabled(true);

  JS::RootedObject global(
      cx, JS_NewGlobalObject(cx, &globalClass, nullptr, JS::FireOnNewGlobalHook,
                             options));

  if (!global) {
    return 1;
  }

  {
    JSAutoRealm ar(cx, global);

    if (!JS::InitRealmStandardClasses(cx)) {
      return 1;
    }
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

    if (argc < 2) {
      printf("Usage: forge.exe <file.js>\n");
      return 1;
    }

    std::ifstream file(argv[1]);

    if (!file) {
      printf("Cannot open %s\n", argv[1]);
      return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    std::string source = buffer.str();

    JS::SourceText<mozilla::Utf8Unit> src;

    if (!src.init(cx, source.c_str(), source.length(),
                  JS::SourceOwnership::Borrowed)) {
      return 1;
    }

    JS::RootedValue rval(cx);

    JS::CompileOptions opts(cx);
    opts.setFileAndLine(argv[1], 1);

    if (!JS::Evaluate(cx, opts, src, &rval)) {
      if (JS_IsExceptionPending(cx)) {
        JS::ExceptionStack exn(cx);

        if (JS::StealPendingExceptionStack(cx, &exn)) {
          JS::ErrorReportBuilder report(cx);

          if (report.init(cx, exn, JS::ErrorReportBuilder::NoSideEffects)) {
            JS::PrintError(stderr, report.report(), false);
          }
        }
      }

      return 1;
    }

    if (rval.isNumber()) {
      printf("Result = %f\n", rval.toNumber());
    }
    printf("Starting event loop\n");

    while (!timers.empty() || !microtasks.empty()) {
      while (!microtasks.empty()) {
        auto& task = microtasks.front();

        JS::RootedObject callback(cx, task->callback);

        JS::RootedValue thisValue(cx, JS::UndefinedValue());

        JS::RootedValue rval(cx);

        bool ok =
            JS::Call(cx, thisValue, callback, JS::HandleValueArray::empty(),
                     JS::MutableHandleValue(&rval));

        if (!ok) {
          return 1;
        }

        microtasks.erase(microtasks.begin());
      }
      uint64_t now = GetTimeMs();

      for (auto it = timers.begin(); it != timers.end();) {
        if (now >= (*it)->dueTime) {
          printf("Timer %d expired\n", (*it)->id);
          JS::RootedObject callback(cx, (*it)->callback);

          JS::RootedValue thisValue(cx, JS::UndefinedValue());

          JS::RootedValue rval(cx);

          printf("About to call callback...\n");

          bool ok = JS::Call(cx, thisValue, callback, (*it)->arguments,
                             JS::MutableHandleValue(&rval));

          printf("JS::Call returned: %d\n", ok);

          if (!ok) {
            printf("Callback failed\n");

            if (JS_IsExceptionPending(cx)) {
              printf("JavaScript exception pending\n");
            }
          } else {
            printf("Callback succeeded\n");
          }
          if ((*it)->repeat) {
            printf("Rescheduling timer %d\n", (*it)->id);

            (*it)->dueTime = GetTimeMs() + (*it)->delay;

            ++it;
          } else {
            printf("Removing timer %d\n", (*it)->id);

            it = timers.erase(it);
          }
        } else {
          ++it;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  JS_DestroyContext(cx);
  JS_ShutDown();

  return 0;
}