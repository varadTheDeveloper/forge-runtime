#include <stdio.h>
#include <string.h>

#include <fstream>
#include <sstream>

#include "js/CompileOptions.h"
#include "js/CompilationAndEvaluation.h"
#include "js/Context.h"
#include "js/GlobalObject.h"
#include "js/Initialization.h"
#include "js/RealmOptions.h"
#include "js/RootingAPI.h"
#include "js/SourceText.h"
#include "js/Value.h"

#include "js/CallArgs.h"
#include "js/CharacterEncoding.h"
#include "js/PropertyAndElement.h"
#include "js/Conversions.h"
#include "js/Exception.h"
#include "js/ErrorReport.h"
static const JSClass globalClass = {
    "global",
    JSCLASS_GLOBAL_FLAGS,
    &JS::DefaultGlobalClassOps,
};

static bool Print(JSContext* cx,
                  unsigned argc,
                  JS::Value* vp)
{
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    for (unsigned i = 0; i < args.length(); i++) {

        JS::RootedString str(cx, JS::ToString(cx, args[i]));

        if (!str) {
            return false;
        }

        JS::UniqueChars bytes =
            JS_EncodeStringToUTF8(cx, str);

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

int main(int argc, char* argv[])
{
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
        cx,
        JS_NewGlobalObject(
            cx,
            &globalClass,
            nullptr,
            JS::FireOnNewGlobalHook,
            options
        )
    );

    if (!global) {
        return 1;
    }

    {
        JSAutoRealm ar(cx, global);

        if (!JS::InitRealmStandardClasses(cx)) {
            return 1;
        }

        if (!JS_DefineFunction(
                cx,
                global,
                "print",
                Print,
                0,
                0))
        {
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

        if (!src.init(
                cx,
                source.c_str(),
                source.length(),
                JS::SourceOwnership::Borrowed))
        {
            return 1;
        }

        JS::RootedValue rval(cx);

        JS::CompileOptions opts(cx);
        opts.setFileAndLine(argv[1], 1);

   if (!JS::Evaluate(
        cx,
        opts,
        src,
        &rval))
{
    if (JS_IsExceptionPending(cx)) {

        JS::ExceptionStack exn(cx);

        if (JS::StealPendingExceptionStack(cx, &exn)) {

            JS::ErrorReportBuilder report(cx);

            if (report.init(cx, exn,
                            JS::ErrorReportBuilder::NoSideEffects)) {

                JS::PrintError(
                    stderr,
                    report.report(),
                    false
                );
            }
        }
    }

    return 1;
}

        if (rval.isNumber()) {
            printf("Result = %f\n", rval.toNumber());
        }
    }

    JS_DestroyContext(cx);
    JS_ShutDown();

    return 0;
}