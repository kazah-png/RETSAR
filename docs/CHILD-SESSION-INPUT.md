# Child-session input experiment

This laboratory experiment runs a fixed RETSAR probe inside an already observed native child session. It does not create a child session, assign physical devices, start Roblox or establish exclusive hardware ownership.

## Admission and execution

`RETSAR-child-session-task-probe --launch --expected-child-session <id> --receipt <new-private.jsonl>` runs in the parent console. It requires enabled child-session support, a successful current child-ID query matching the expected ID, distinct active parent and child rows, and a fixed worker executable under the laboratory directory. The executable is held open without write/delete sharing during the trial and its hash is recorded.

The launcher creates a randomly named, trigger-free Task Scheduler task for the current user with `InteractiveToken` and the limited run level. The only action is the fixed input probe. A fresh nonce and new worker receipt correlate that action. `RunEx` explicitly selects the verified child session; no password or other user's credentials are supplied. Task registration and successful `RunEx` are not proof that the worker ran. [Microsoft RunEx contract](https://learn.microsoft.com/en-us/windows/win32/api/taskschd/nf-taskschd-iregisteredtask-runex).

The worker checks its actual process and token session, absence of thread impersonation, the active parent and child, the active console, and its input desktop before admitting input. It draws a small original GDI window. Before each input operation, the foreground window must be that exact worker-owned visible window on the same thread. It sends one mouse movement without a click and an F24 down/up pair, tags them, and counts only matching messages received by its own window. It restores the child's cursor only while that guard remains valid. It never releases a preexisting user's key.

## Parent observation and cleanup

A separate sampler observes the parent's cursor position and foreground handle at 50 ms intervals. It generates no parent input and records no window titles or keystrokes. A changed value or failed sample is inconclusive; it is not silently classified as isolation success. Sampling can miss movement between samples, and unchanged observations do not establish an exclusive physical device binding.

The worker's final receipt, nonce, session IDs, original process identity, task result and launcher observations must be assessed together. Merely finding a receipt or obtaining a successful scheduling HRESULT is insufficient.

The launcher verifies its exact registered task definition and path before cleanup. It never stops unrelated tasks or processes. Unconfirmed completion or altered identity remains a residual resource requiring review. Neither executable logs off sessions or changes persistent Windows input, authentication or display policy.

## Limits and tests

`SendInput` has no target HWND. A foreground observation is a guard, not an atomic lock against subsequent focus changes in the child desktop. This narrow experiment uses a harmless test key and no mouse clicks; it does not establish safe arbitrary application automation. [Microsoft SendInput contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).

The optional `-ChildSessionLab` build invokes only help and pure tests. The input contract currently passes **76 inert checks** over supplied values: argument rejection, parent/child mismatch, unknown identity, inactive sessions, impersonation and foreign desktop/window/focus conditions. These tests query no Windows session, send no input and run no task.

Live observations are recorded separately after supervised execution. Physical monitor exclusivity, hardware keyboard/mouse assignment, application compatibility and prolonged operation remain independent milestones.
## Current live status

An explicit invocation from the parent desktop rejected its wrong actual session with exit 3 before creating a receipt or admitting input. The first scheduler launch also rejected because the child had already become disconnected. The first coordinated resumption timed out before scheduling. These refusals sent no child input.

After a later resumption succeeded with manual Windows identity confirmation, the fixed scheduler worker ran in the actual child session. Its own window rendered, and `SendInput` accepted one mouse movement. The original worker observed no exact full-width marker, so it sent no keyboard input and failed its deadline. It restored the child's cursor; the launcher observed completion and removed its exact task. Parent cursor movement made that first sampling interval inconclusive.

A diagnostic repeat used an interior target separated from both the initial and current cursor. The child cursor moved from `(512,384)` to `(596,395)`, and the owned window received a mouse message. Its extra-information value exactly matched the low 32 bits of the emitted 64-bit marker, with the upper bits absent. This is an observed truncation on this input path, not proof of where Windows performed it. Exact full-width matching remained required, so the trial correctly failed without sending the F24 pair. The cursor was restored and the task removed. Across 493 parent samples, cursor and foreground were unchanged with no sampling errors; that bounded observation does not prove physical device exclusivity.

The diagnostic records only the last eight mouse messages received by the probe's own window, their marker values, and before/target/after cursor positions. It installs no global input hook and records no user keystrokes. The corrected worker generates a positive, nonzero 31-bit correlation tag before injection and still compares the received value exactly as `ULONG_PTR`. It does not mask incoming values or accept a low-bit fallback. This tag can collide and is not authentication; separate session, token and owned-window guards remain required.

The corrected supervised trial passed: the owned child window received one exact marked mouse movement, one F24 key-down and one key-up. Its final receipt recorded successful completion and cursor restoration with no pending key release or error. The scheduler observed the task running, completion with result zero and removal of the exact temporary task. Parent observation contained 39 samples with no cursor changes, foreground changes or sampling errors. The worker receipt's nonce, session IDs and SHA-256 correlated with the launcher receipt.

This demonstrates that the tested child desktop received synthetic mouse and keyboard input while the sampled parent state stayed unchanged. The short observation does not establish continuous isolation, arbitrary-application focus safety, independent physical devices or Roblox compatibility. The launcher deliberately retains `inputIsolationVerified=false`; input receipt success and bounded parent observations must be interpreted separately.

## Application readiness

A separate supervised local trial launched an existing Zzen preview build from an isolated directory into the observed child session. The process path, file hash, session ID and creation identity were correlated, and the application window appeared inside the child desktop. Its startup log contained only the initialization entry; the build's default state is paused. The window was partially covered by an unrelated startup application, so a fully unobstructed runtime-state inspection remains separate from the process observation.

The one-shot task definition was removed without stopping Zzen. The helper initially reported cleanup as unverified because PowerShell returned a null process exit code for the deletion command; independent exact-name queries then confirmed task absence and the same surviving application process. No farming was activated by this trial. The private application helper and existing application executable are not distributed as part of RETSAR.

The installed, validly signed official Roblox application was then launched without game arguments in that same active child session. Its process identity and the rendered Roblox home screen were observed. The fixed launch helper refused any preexisting Roblox process, supplied no credentials, and removed its trigger-free task with deletion result zero and independent absence verification. This establishes application startup, not game rendering, gameplay, farming or long-duration compatibility. An unrelated startup window obscured part of the viewer; preparing the game screen was handed to the user before any farming test.

### First supervised farming cycle

The user prepared the football game in the child desktop. Both an automated F8 attempt and a user-reported F8 attempt failed to activate Zzen; its own Start/Resume button did activate it. The underlying cause of the hotkey failure is unresolved. A later read-only observation found the viewer without keyboard focus, but that observation does not establish its focus at the time of the failed attempts. No keyboard-routing policy or live viewer connection was changed to guess at a fix.

After activation, Zzen's isolated session log recorded detection of **Play Again**, two bounded click attempts and **one confirmed replay transition**. A subsequent rendered view showed the next match. Roblox and Zzen remained in the child session while the user's chat application ran in the parent. The user explicitly confirmed normal mouse and typing use in the principal desktop while farming continued.

This is the first supervised practical success for one Roblox account and Zzen inside the native child desktop. Use Zzen's own button to start or pause during this pilot. The current viewer, child and applications were left running. The observation does not establish an eight-hour run, four-account capacity, automatic reconnect, independent physical keyboard/mouse ownership or a full ASTER replacement.
