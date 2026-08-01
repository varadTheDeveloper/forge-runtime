# Manifesto

## Why Forge exists

Every major JavaScript runtime — Node, Deno, Bun — is built on an engine designed for the browser, not for servers, edge compute, or the agent workloads that make up a growing share of what JavaScript runs today. The industry keeps layering faster APIs and better tooling on top of that shared foundation, but almost nobody questions whether the foundation itself is still the right choice.

Forge starts from a different question: what if the runtime were built from scratch for this era, instead of inherited from one?

That's why Forge runs on SpiderMonkey instead of V8, and why everything beneath it — memory management, containers, the event loop, the filesystem layer — is custom C++20 with zero external dependencies outside the engine itself. Not because existing tools are bad, but because inherited constraints become inherited ceilings, and I wanted to see what's possible without one.

## Why I'm building it this way

I care about Forge reaching as many people as possible far more than I care about running a company or being called "the founder." I'd rather stay deep in the code for the long haul than manage a business around it. I'm not chasing a title, funding, or recognition — I want to be the person still writing this code years from now, watching it get used by people I'll never meet.

I don't think about money as a goal. I think about it as a constraint I need to clear so I can keep building — enough to live on, enough to keep this moving, nothing more. I'd rather put whatever I have back into the project than sit on it.

## What I'm afraid of

Not failing technically. I'm afraid of building something genuinely good and it just sitting there, unused, because it never reached the people who'd actually use it. The gap between "this works" and "this is adopted" is the part I have the least control over — and it's the part that matters most.

## What I'm working toward

One real company running Forge in production. Not a toy project, not a benchmark demo — an actual team choosing Forge to run something that matters to their business, at real load, with real users behind it. That's the moment I'd know this isn't just a fast runtime on paper. Everything else is downstream of that one thing happening.

## Is Forge sustainable?

If you're wondering whether Forge will still be around in a few years: it isn't backed by a funding runway with a clock on it, and it isn't a side project I'll abandon when it gets hard. I'm building this as a lifetime commitment. The code is MIT-licensed, so even in the worst case, nothing about Forge disappears — anyone can fork it and keep building independently of me.

Is it proven at scale yet? No — it's early, and I'll say that plainly rather than oversell it. But the intent, the license, and the roadmap all point toward permanence, not abandonment.

## Where it's headed

Async filesystem · Networking + HTTP server · Module system · Worker threads · Linux & macOS support · Node.js compatibility.

---

*Forge — built by [Varad Modhekar](https://github.com/varadTheDeveloper). MIT Licensed.*
