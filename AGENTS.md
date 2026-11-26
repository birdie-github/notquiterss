# Working on NotQuiteRSS

NotQuiteRSS is a C++17 desktop RSS/Atom reader for Linux, Windows, and macOS.
It supports Qt 5 and Qt 6. Qt text widgets render articles, and links open in
the user's external browser. Use the current source as the authority on how
the application works. Consult upstream QuiteRSS for historical behavior when
that history helps answer the question.

## Development

- For an opinion, diagnosis, or design request, answer the question and wait
  for a request to implement. For an implementation request, proceed
  autonomously. Explain material ambiguities or unavoidable behavior changes
  before editing.
- Record the exact base commit. The maintainer applies patches locally, so
  check which earlier changes are present upstream and identify any local
  patch the work depends on.
- Trace the affected behavior from its normal entry point. Check existing
  components, callers, signal connections, settings, ownership, and platform
  differences as relevant. Reuse existing subsystems and keep the diff focused.
- Preserve Qt 5 and Qt 6 support and existing platform behavior. Update build,
  installation, and CI packaging definitions when the change affects them.
- If work is interrupted, inspect what was completed before resuming.

## Patch delivery

- Give the base commit, patch dependencies, and the `git apply` command.
  Verify clean application against the recorded base with any prerequisites
  applied. Check whitespace and affected references, declarations, and
  connections as relevant.
- Briefly explain the change, supply a ready-to-use commit message, and give
  a targeted manual check with an observable expected result. Report what
  was verified and distinguish static checks from builds and runtime tests.
- Compile or launch NotQuiteRSS only on explicit request. Do not push, open
  a pull request, or paste an entire patch into the conversation.

## Writing the proposed commit message

Write for a future maintainer reading `git log` without the diff or the
conversation. Use an informative, imperative title and a body proportional
to the change. Someone who reads the body should be able to explain what
prompted the change, how the implementation works at a high level, and why
that mechanism produces the intended behavior.

Describe the concrete decision that connects the problem to the result.
For example, “Keep the Options sidebar compact” gives the intent. The body
can explain that the dialog measures the visible category columns after
layout and uses their combined width as the initial splitter position.
Leaving the minimum width independent lets the user drag the divider past
the longest label. Those details explain both what happens on opening and
what happens when the user takes control.

Use a few class, function, file, or setting names where they help a reader
locate the mechanism. Preserve rationale that would be hard to infer from
the diff, including relevant ownership, threading, cache invalidation,
persistence, or compatibility decisions. A small change may need only two
sentences. For a change spanning components, explain how the pieces interact.

Describe the final implementation. Omit development chronology, line-by-line
inventories, padding, and unsupported correctness or performance claims.
Keep verification in the delivery notes unless a result explains a design
decision.
