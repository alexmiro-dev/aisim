# `ui/qt` — Qt desktop front-end (a driving adapter)

Placeholder for the Qt desktop UI. Architecturally this is a **driving
adapter** (same role as `adapter/rest`, `adapter/ws`, `adapter/cli`): it
translates user actions into calls on the **driving ports** in
`aisim::application` and renders results — including streamed tokens delivered
via the `ResultPublisher` seam.

It is hoisted to `src/ui/` rather than nested under `src/adapter/` purely for
practicality — a GUI brings its own dependencies (Qt), resources, and MOC build
steps, and reads more clearly at top level. The architectural role is unchanged.

Like every driving adapter, it depends **only on driving ports** — never on
`domain::` internals, Ollama, or SQLite.

Build is opt-in: configure with `-DAISIM_BUILD_QT_UI=ON` once the target exists.
