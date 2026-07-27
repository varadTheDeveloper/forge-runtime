#include "TimerScheduler.h"

#include <utility>

namespace forge::core::platform
{

Size TimerScheduler::IndexOf(TimerId id) const noexcept
{
    for (Size index = 0; index < entries_.Size(); ++index)
    {
        if (entries_[index].id == id)
        {
            return index;
        }
    }

    return entries_.Size(); // sentinel: "not found"
}

Result<TimerId> TimerScheduler::Schedule(
    u64 nowMs,
    u64 delayMs,
    bool repeat,
    TimerCallback callback,
    void* userData) noexcept
{
    Entry entry;
    entry.id = nextId_;
    entry.dueMs = nowMs + delayMs;
    entry.delayMs = delayMs;
    entry.repeat = repeat;
    entry.cancelled = false;
    entry.callback = callback;
    entry.userData = userData;

    if (Result<void> result = entries_.PushBack(entry); result.HasError())
    {
        return Result<TimerId>(Failure(result.Error()));
    }

    return Result<TimerId>(TimerId(nextId_++));
}

void TimerScheduler::Cancel(TimerId id) noexcept
{
    Size index = IndexOf(id);

    if (index == entries_.Size())
    {
        return; // not found: already fired-and-removed, or never existed
    }

    entries_[index].cancelled = true;
}

bool TimerScheduler::Empty() const noexcept
{
    return Count() == 0;
}

Size TimerScheduler::Count() const noexcept
{
    Size count = 0;

    for (Size index = 0; index < entries_.Size(); ++index)
    {
        if (!entries_[index].cancelled)
        {
            ++count;
        }
    }

    return count;
}

bool TimerScheduler::NextDueDelay(u64 nowMs, u64& outDelayMs) const noexcept
{
    bool found = false;
    u64 earliest = 0;

    for (Size index = 0; index < entries_.Size(); ++index)
    {
        if (entries_[index].cancelled)
        {
            continue;
        }

        if (!found || entries_[index].dueMs < earliest)
        {
            earliest = entries_[index].dueMs;
            found = true;
        }
    }

    if (!found)
    {
        return false;
    }

    outDelayMs = (earliest > nowMs) ? (earliest - nowMs) : 0;
    return true;
}

Size TimerScheduler::PopDue(u64 nowMs) noexcept
{
    Size firedCount = 0;
    Size index = 0;

    while (index < entries_.Size())
    {
        if (entries_[index].cancelled)
        {
            entries_[index] = std::move(entries_[entries_.Size() - 1]);
            entries_.PopBack();
            continue; // re-check this index: it now holds a different entry
        }

        if (entries_[index].dueMs > nowMs)
        {
            ++index;
            continue;
        }

        // Copy out before touching the vector — Schedule()/Cancel() called
        // from inside the callback below may grow or shrink entries_, and
        // we must not hold a dangling reference across that call.
        Entry fired = entries_[index];

        if (fired.repeat)
        {
            // Anchor to the timer's own previous due time, not to `nowMs`
            // (when PopDue happened to be called). Anchoring to `nowMs`
            // would let the effective period drift longer under repeated
            // late polling; anchoring to `fired.dueMs` keeps the average
            // period exactly `delayMs`, at the cost of firing back-to-back
            // (across separate PopDue calls, never within one) to catch up
            // after a long pause — matching the guarantee documented on
            // Schedule() above.
            entries_[index].dueMs = fired.dueMs + fired.delayMs;
            ++index;
        }
        else
        {
            entries_[index] = std::move(entries_[entries_.Size() - 1]);
            entries_.PopBack();
            // do not advance index: whatever got swapped in still needs
            // to be checked.
        }

        fired.callback(fired.userData);
        ++firedCount;
    }

    return firedCount;
}

} // namespace forge::core::platform
