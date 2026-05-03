# Vectra Doctor

`Vectra: Doctor` is a one-shot health check. It writes its findings
into the **Vectra** output channel:

- which `vectra` binary the extension is using and what version it
  reports;
- whether `claude` is on PATH;
- which workspace folder is open and whether it has a `.vectra/`
  index;
- the embedding model selected for indexing;
- the GPU backend that was compiled into the binary;
- whether the in-process permission bridge has bound a port.

Re-run after installing or reconfiguring anything — the output
channel reflects the live state at the time of the run.
