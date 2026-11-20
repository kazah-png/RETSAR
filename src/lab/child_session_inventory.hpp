#pragma once
#include <Windows.h>
#include <wtsapi32.h>

namespace seats::lab::child {
// A WTS listener is not a logon session. Preserve its row without querying
// user information. Unknown states remain applicable, including future
// states: a failed query must not hide an unfamiliar signed-in session.
inline bool RequiresUserObservation(int state)noexcept {return state!=WTSListen;}
}
