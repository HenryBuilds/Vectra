# Vectra vs Claude Code · proof-of-concept run

**Repo under test:** [kubernetes/kubernetes](https://github.com/kubernetes/kubernetes) (shallow clone, ~17k Go files, ~245k symbol chunks).

**What this measures.** Each task is run twice in the same shell, in the same kubernetes/ directory:

- **Vectra path** — `vectra ask "$task" --stream-json --permission-mode plan`. Vectra runs hybrid retrieval (FTS5 over symbol-only index, no embeddings) and hands the top chunks to `claude -p` as labeled context.
- **Claude-only path** — `claude -p "$task" --output-format=stream-json --permission-mode plan`. Claude Code does its own retrieval via its built-in `Glob` / `Grep` / `Read` tools.

Plan mode keeps both runs read-only, so the comparison is purely *can the system find the right code and reason about it*. Token / cost / wall-clock totals come from each run's final `result` event.

## Aggregate

| metric | Vectra → Claude | Claude alone | Δ |
|---|---:|---:|---:|
| **answers correct** (verifier hits) | **10 / 10** | **10 / 10** | — |
| context tokens processed (sum) | 2429.3k | 1848.3k | (+31%) |
| output tokens (sum) | 53.5k | 60.9k | (-12%) |
| total cost (USD) | $6.6951 | $7.0157 | (-5%) |
| wall-clock (sum) | 1149.0s | 1399.7s | (-18%) |
| turns (sum) | 63 | 59 | (+7%) |

"answers correct" = the assistant's text answer contains every `verify.must_contain` anchor for that task (case-insensitive). Anchors are picked so a wrong path does not accidentally pass.

"context tokens processed" = `input_tokens + cache_creation + cache_read`, the actual prompt volume Claude saw (cache hits are still work, just billed cheaper).

"Δ" = Vectra path relative to Claude-alone (lower is usually better for tokens / cost / time; turns is mixed signal).

## Per-task detail

### scheduler-priority

> In the kube-scheduler, where is a Pod's priority used to decide which existing pods to preempt when there is no room? Name the function and the file.

**Verifier anchors:** `SelectVictimsOnNode` · `default_preemption.go`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 114.9k | 261.5k |
| output tokens | 2.0k | 10.3k |
| total cost (USD) | $0.3891 | $0.9870 |
| wall-clock | 44.6s | 191.9s |
| turns | 3 | 11 |

<details><summary>Vectra → Claude answer</summary>

```
**Function:** `SelectVictimsOnNode`

**File:** `pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go`

The pod-priority comparison happens inside the helper `isPreemptionAllowed` that `SelectVictimsOnNode` uses to filter candidates:

```go
return corev1helpers.PodPriority(victim.GetPod()) < corev1helpers.PodPriority(preemptor) && pl.IsEligiblePod(nodeInfo, victim, preemptor)
```

`SelectVictimsOnNode` removes all lower-priority eligible pods from the node, checks whether the preemptor now fits, and then greedily adds victims back in descending priority order — keeping the minimum set whose removal still allows the preemptor to schedule.

Since this is a read-only Q&A (no code changes needed), there is nothing to plan or implement.
The answer is provided above — `SelectVictimsOnNode` in `pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go`. No further action needed since this was a research question.
```

</details>

<details><summary>Claude alone answer</summary>

```
I'll research these four kube questions in parallel using Explore agents, then write the findings to the plan file.
Good — this is the upstream Kubernetes source tree. Launching three Explore agents in parallel for the four questions.
Let me spot-check the most load-bearing citations before writing the plan.
All four citations verified. Writing the findings to the plan file.
Since this is a research task (not an implementation), I won't call ExitPlanMode — its docs explicitly say not to use it for "gathering information, searching files, reading files or in general trying to understand the codebase." The full answers (with verified file:line citations) are written to:

`C:\Users\German Classic\.claude\plans\in-the-kube-scheduler-where-humble-puppy.md`

Quick summary of each:

**1. Scheduler preemption by priority** — `DefaultPreemption.SelectVictimsOnNode` at `pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go:251`. The priority comparison itself is in `isPreemptionAllowed` at line 395 of the same file (uses `corev1helpers.PodPriority`). Cross-node tiebreak: `pickOneNodeForPreemption` at `pkg/scheduler/framework/preemption/preemption.go:311`.

**2. Kubelet memory-pressure ranking** — `rankMemoryPressure` at `pkg/kubelet/eviction/helpers.go:819` does `orderedBy(exceedMemoryRequests, priority, memory).Sort(pods)` — three tiers: (1) over-request first, (2) lower priority first, (3) larger absolute usage above requests. Driven from `managerImpl.synchronize` (`pkg/kubelet/eviction/eviction_manager.go:248`), which dispatches via `signalToRankFunc` built in `buildSignalToRankFunc` (`helpers.go:1151`).

**3. Admission ordering** — Mutating runs before validating. The chain iterates plugins in slice order in `chainAdmissionHandler.{Admit,Validate}` at `staging/src/k8s.io/apiserver/pkg/admission/chain.go:31` and `:47`. The slice order is the constant `AllOrderedPlugins` in `pkg/kubeapiserver/options/plugins.go:74`. Mutating-then-validating is enforced by the create handler at `staging/src/k8s.io/apiserver/pkg/endpoints/handlers/create.go:203` (mutate) followed by the validate callback in `staging/src/k8s.io/apiserver/pkg/registry/rest/create.go:233`.

**4. controller-manager leader election** — Wired in `cmd/kube-controller-manager/app/controllermanager.go:856` (`leaderElectAndRun` → `leaderelection.RunOrDie`). The renewal interval is `RenewDeadline` (default **10s**), retried every `RetryPeriod` (default **2s**), with `LeaseDuration` (**15s**) as the takeover threshold. Defaults: `staging/src/k8s.io/component-base/config/v1alpha1/defaults.go:35`. Struct: `staging/src/k8s.io/component-base/config/types.go:41`. Renewal loop: `LeaderElector.renew` at `staging/src/k8s.io/client-go/tools/leaderelection/leaderelection.go:279` (uses `PollUntilContextTimeout` with `RetryPeriod` as interval, `RenewDeadline` as timeout).
```

</details>

### kubelet-eviction

> How does the kubelet decide which pods to evict under memory pressure? Walk me through the ranking logic and name the relevant types and functions.

**Verifier anchors:** `rankMemoryPressure` · `exceedMemoryRequests`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 319.9k | 233.8k |
| output tokens | 9.1k | 9.6k |
| total cost (USD) | $0.8780 | $1.1136 |
| wall-clock | 177.6s | 189.3s |
| turns | 7 | 7 |

<details><summary>Vectra → Claude answer</summary>

```
I'll research the kubelet eviction logic in this repo, then write up the explanation as a plan file.
Let me verify the key code citations before writing this up.
The Explore findings line up with what I just verified. This task is explanatory rather than an implementation, so I'll capture the walkthrough as the plan-file deliverable.
The walkthrough is in `C:\Users\German Classic\.claude\plans\task-how-does-the-vivid-shannon.md`. Short version:

**The policy lives in one line** — `pkg/kubelet/eviction/helpers.go:819`:

```go
func rankMemoryPressure(pods []*v1.Pod, stats statsFunc) {
    orderedBy(exceedMemoryRequests(stats), priority, memory(stats)).Sort(pods)
}
```

**Three comparators, applied in order** (top wins; lower-down only used as tiebreakers):

1. `exceedMemoryRequests(stats)` (`helpers.go:693`) — pods over their memory request sort first. Pods with missing stats are also pushed to the front.
2. `priority` (`helpers.go:680`) — lower `Pod.Spec.Priority` sorts first.
3. `memory(stats)` (`helpers.go:712`) — final tiebreak by `usage − request`, larger over-consumer first.

These are composed via `orderedBy(...)` → `multiSorter` (`helpers.go:629–677`); `Less` walks the comparators and returns on the first non-zero result.

**How it gets called** — `(*managerImpl).synchronize` (`eviction_manager.go:248`) gathers stats, evaluates `Threshold`s, picks the highest-priority unmet one via `byEvictionPriority` (`helpers.go:836`), tries `reclaimNodeLevelResources` first, then looks up the ranker in `m.signalToRankFunc` (built by `buildSignalToRankFunc` at `helpers.go:1150`, mapping `SignalMemoryAvailable` and `SignalAllocatableMemoryAvailable` → `rankMemoryPressure`), sorts active pods, and kills exactly one (`eviction_manager.go:421–443` → `evictPod` at line 611 → `killPodFunc`). Hard thresholds get a 0-second grace period; soft thresholds get `MaxPodGracePeriodSeconds` clamped against the pod's own `TerminationGracePeriodSeconds`.

**Key types**: `Manager`/`managerImpl`, `Threshold`, `Signal`, `rankFunc`, `statsFunc`, `cmpFunc`, `multiSorter`, `byEvictionPriority` — locations in the table at the bottom of the plan file.

Since this was an explanatory/research task rather than an implementation, I'm not calling `ExitPlanMode` (its own guidance says to skip it for research). Let me know if you want me to drill into any piece — e.g. how `statsFunc` resolves per-container memory, or how soft-threshold grace-period bookkeeping works in `nodeConditionsObservedAt` / `thresholdsObservedAt`.
```

</details>

<details><summary>Claude alone answer</summary>

```
I'll research these three Kubernetes subsystems in parallel using Explore agents, then write up the findings as a research document.
I have detailed findings from all three explorations. Writing up the research document now.
Research writeup is in `C:\Users\German Classic\.claude\plans\how-does-the-kubelet-replicated-hennessy.md`. Quick recap of the answers:

**1. Kubelet memory eviction ranking** — `managerImpl.synchronize` (`pkg/kubelet/eviction/eviction_manager.go:248`) detects pressure, then dispatches to `rankMemoryPressure` (`pkg/kubelet/eviction/helpers.go:819`), which calls `orderedBy(exceedMemoryRequests, priority, memory).Sort(pods)`. The three comparators run via the `multiSorter` (`helpers.go:629`) and act as a primary key + tiebreakers: pods over their memory request first, then lower priority, then largest excess-over-request. `evictPod` (`eviction_manager.go:611`) then invokes `killPodFunc`.

**2. Admission ordering on `CREATE Pod`** — Phase order is mutating → schema → validating, orchestrated by `createHandler` (`staging/src/k8s.io/apiserver/pkg/endpoints/handlers/create.go:53`) calling `mutatingAdmission.Admit` directly (line 202), then `Store.Create` running `rest.BeforeCreate` (line 509) followed by the validating callback (line 514). The plugin order *within* a phase is the explicit positional list `AllOrderedPlugins` in `pkg/kubeapiserver/options/plugins.go:73-120` — webhooks sit at lines 115/117, ResourceQuota and AlwaysDeny last. The chain runner is `chainAdmissionHandler` (`staging/src/k8s.io/apiserver/pkg/admission/chain.go:23`).

**3. KCM leader election** — `leaderElectAndRun` (`cmd/kube-controller-manager/app/controllermanager.go:856`) calls `leaderelection.RunOrDie` with a `LeasesResourceLock` backed by a `coordination.k8s.io/v1` Lease. Defaults from `staging/src/k8s.io/component-base/config/v1alpha1/defaults.go:35-44`: `LeaseDuration=15s`, `RenewDeadline=10s`, `RetryPeriod=2s`. **The renewal interval is `RetryPeriod`**, configurable via `--leader-elect-retry-period` (bound in `staging/src/k8s.io/component-base/config/options/leaderelectionconfig.go:24-52`). The renewal loop itself is `LeaderElector.renew` (`staging/src/k8s.io/client-go/tools/leaderelection/leaderelection.go:278-312`) — a `wait.PollUntilContextTimeout` that retries `tryAcquireOrRenew` every `RetryPeriod` until `RenewDeadline` expires; if it expires, `OnStoppedLeading` fires and KCM exits via `klog.FlushAndExit` (line 412).
```

</details>

### admission-flow

> When a CREATE Pod request hits the API server, in what order do mutating and validating admission plugins run, and where is that ordering enforced in code?

**Verifier anchors:** `AllOrderedPlugins` · `chainAdmissionHandler`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 378.8k | 172.5k |
| output tokens | 9.6k | 7.6k |
| total cost (USD) | $1.1276 | $0.9608 |
| wall-clock | 238.0s | 174.9s |
| turns | 10 | 5 |

<details><summary>Vectra → Claude answer</summary>

```
I'll research how admission plugin ordering works for CREATE Pod requests in the Kubernetes API server. Let me explore the relevant code.
Let me verify the key file paths and line numbers before finalizing my plan.
All key facts verified. This is a research question rather than a code-change task, so the "plan" deliverable is the answer documenting the ordering with concrete code citations. Let me write it.
The plan file documents the answer with verified citations. Let me load the ExitPlanMode tool to finalize.
The plan file at `C:\Users\German Classic\.claude\plans\task-when-a-create-parsed-wadler.md` contains the full answer with verified file:line citations. Short summary:

- **Order:** mutating phase first (every `MutationInterface` plugin in `AllOrderedPlugins` order), then validating phase (every `ValidationInterface` plugin in the same order). Webhooks and `ResourceQuota` sit at the end of the slice by design.
- **Where it's enforced:**
  - `pkg/kubeapiserver/options/plugins.go:73-120` — `AllOrderedPlugins` is the source of truth.
  - `staging/src/k8s.io/apiserver/pkg/server/options/admission.go:228-245` — `enabledPluginNames()` filters that slice while preserving order.
  - `staging/src/k8s.io/apiserver/pkg/admission/plugins.go:127-163` — `NewFromPlugins` builds a single ordered handler chain.
  - `staging/src/k8s.io/apiserver/pkg/admission/chain.go:21-60` — `chainAdmissionHandler.Admit` and `.Validate` walk the same slice for the two phases.
  - `staging/src/k8s.io/apiserver/pkg/endpoints/handlers/create.go:202-209` invokes the mutating phase, then `staging/src/k8s.io/apiserver/pkg/registry/rest/create.go:202-235` (`AdmissionToValidateObjectFunc`) runs the validating phase from inside `genericregistry.Store.Create`, just before the etcd write.
```

</details>

<details><summary>Claude alone answer</summary>

```
I'll research the admission plugin ordering in the Kubernetes codebase. Let me launch parallel Explore agents to find where the order is defined and enforced.
I have comprehensive findings. Since this is a knowledge question rather than an implementation task, I'll write up the answer as a research document at the plan file location, then exit plan mode.
# Order

For a CREATE Pod the apiserver runs the admission chain **twice** over the same ordered slice of plugins:

1. **Mutating pass** — every plugin implementing `MutationInterface.Admit`, in `RecommendedPluginOrder`
2. Strategy `PrepareForCreate` + `Validate` (defaults, schema validation)
3. **Validating pass** — every plugin implementing `ValidationInterface.Validate`, in the **same** `RecommendedPluginOrder`
4. Write to etcd

A single plugin can be in both passes (e.g. `LimitRanger`); the type assertion at iteration time decides whether it participates in a given pass.

# Where ordering is enforced

Two places — one defines the order, one executes it.

**Definition** — `pkg/kubeapiserver/options/plugins.go:73-120`, the `AllOrderedPlugins` slice. The load-bearing comment at lines 111-112 forces the tail order:

```
// new admission plugins should generally be inserted above here
// webhook, resourcequota, and deny plugins must go at the end
```

So `MutatingAdmissionPolicy` → `MutatingAdmissionWebhook` → `ValidatingAdmissionPolicy` → `ValidatingAdmissionWebhook` → `ResourceQuota` → `AlwaysDeny` are pinned last (ResourceQuota must see the fully-mutated object).

This slice is wired via `pkg/kubeapiserver/options/admission.go:56-68` (`options.RecommendedPluginOrder = AllOrderedPlugins`) and instantiated by `Plugins.NewFromPlugins(pluginNames, ...)` in `staging/src/k8s.io/apiserver/pkg/server/options/admission.go`.

**Execution** — `staging/src/k8s.io/apiserver/pkg/admission/chain.go:31-60`. `chainAdmissionHandler` is just `[]Interface`; `Admit` and `Validate` each iterate it in slice order, with a type-assert filter:

```go
if mutator, ok := handler.(MutationInterface); ok { ... }   // Admit pass
if validator, ok := handler.(ValidationInterface); ok { ... } // Validate pass
```

Interfaces at `staging/src/k8s.io/apiserver/pkg/admission/interfaces.go:129-144` (note the explicit "is NOT allowed to mutate" on `ValidationInterface.Validate`).

**Sequencing for CREATE** is in the REST handler, `staging/src/k8s.io/apiserver/pkg/endpoints/handlers/create.go:183-206`: mutating admission is called inline, then validating admission is wrapped via `rest.AdmissionToValidateObjectFunc` (`staging/src/k8s.io/apiserver/pkg/registry/rest/create.go:202-235`) and handed to `r.Create`. The store invokes it after `BeforeCreate` (strategy validation) and before the etcd write at `staging/src/k8s.io/apiserver/pkg/registry/generic/registry/store.go:485-518`.

Full notes with code excerpts saved to `~/.claude/plans/when-a-create-pod-serialized-whistle.md`. Since this was a research question rather than an implementation task, I'm not calling ExitPlanMode — let me know if you want me to dig into any specific plugin (e.g. how `ServiceAccount` mutates the pod, or how `ResourceQuota` handles the late-stage bookkeeping).
```

</details>

### leader-election

> How does kube-controller-manager's leader election work, and where is the lease-renewal interval configured?

**Verifier anchors:** `leaderElectAndRun` · `RetryPeriod`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 232.6k | 220.9k |
| output tokens | 7.1k | 8.6k |
| total cost (USD) | $0.8533 | $0.8707 |
| wall-clock | 135.0s | 149.0s |
| turns | 9 | 11 |

<details><summary>Vectra → Claude answer</summary>

```
I'll research how KCM's leader election works and where the lease-renewal interval is configured. Let me explore the codebase in parallel.
Let me verify the key files directly before writing up the explanation.
I have enough to answer the question. Writing up findings to the plan file.
Here's the answer — the full trace is also in the plan file at `C:\Users\German Classic\.claude\plans\task-how-does-kube-controller-manager-s-spicy-kurzweil.md`.

## How KCM's leader election works

KCM uses the generic `client-go` leader-election library, backed by a `coordination.k8s.io/v1` **`Lease`** named `kube-controller-manager` in `kube-system`.

The entry point is `leaderElectAndRun()` at `cmd/kube-controller-manager/app/controllermanager.go:840`, which builds a resource lock via `resourcelock.NewFromKubeconfig` and calls `leaderelection.RunOrDie` with three durations pulled from `c.ComponentConfig.Generic.LeaderElection`:

```go
leaderelection.RunOrDie(ctx, leaderelection.LeaderElectionConfig{
    Lock:          rl,
    LeaseDuration: …LeaseDuration.Duration,   // line 858
    RenewDeadline: …RenewDeadline.Duration,   // line 859
    RetryPeriod:   …RetryPeriod.Duration,     // line 860
    …
})
```

The actual renewal loop lives in `staging/src/k8s.io/client-go/tools/leaderelection/leaderelection.go:279` (`renew`):

```go
wait.PollUntilContextTimeout(ctx,
    le.config.RetryPeriod,    // poll cadence — fires the renew RPC
    le.config.RenewDeadline,  // overall budget before giving up
    true, …tryAcquireOrRenew…)
```

So the leader hits the apiserver every `RetryPeriod` to bump `Lease.spec.renewTime`. If renewals can't succeed within `RenewDeadline`, it stops leading. `NewLeaderElector` enforces `LeaseDuration > RenewDeadline > RetryPeriod * 1.2`.

## Where the lease-renewal interval is configured

Three CLI flags, defined in `staging/src/k8s.io/component-base/config/options/leaderelectionconfig.go:25` (`BindLeaderElectionFlags`):

| Flag | Field | Default | Meaning |
|---|---|---|---|
| `--leader-elect-lease-duration` | `LeaseDuration` | **15 s** | How long followers wait before taking over an unrenewed lease |
| `--leader-elect-renew-deadline` | `RenewDeadline` | **10 s** | Total budget for the leader to keep retrying renewals |
| `--leader-elect-retry-period`   | `RetryPeriod`   | **2 s**  | Interval between individual renewal RPCs (the "renewal interval") |

Defaults come from `RecommendedDefaultLeaderElectionConfiguration` at `staging/src/k8s.io/component-base/config/v1alpha1/defaults.go:35`. Flags are registered for KCM via `staging/src/k8s.io/controller-manager/options/generic.go:70`.

When people say "lease-renewal interval" they usually mean **`--leader-elect-retry-period`** (how often the renew RPC fires); **`--leader-elect-renew-deadline`** is the surrounding deadline. Both are set on the kube-controller-manager process (static pod manifest or its Deployment).

Since this was a research/code-walkthrough question rather than an implementation task, I'm not calling `ExitPlanMode` — let me know if you'd like me to dig deeper into `tryAcquireOrRenew`, the `Coordinated`/`ReleaseOnCancel` feature gates, or the `NewFromKubeconfig` lock construction.
```

</details>

<details><summary>Claude alone answer</summary>

```
I'll research how kube-controller-manager's leader election works in this codebase. Since this is an informational question, the "plan" will document the findings rather than propose changes.
Let me verify the key sites by reading the actual code.
I have everything I need. Let me write the findings to the plan file.
This is a research question, not an implementation task, so I'll answer directly rather than request plan approval (per `ExitPlanMode`'s own guidance: "do NOT use it" for research tasks). Full write-up is in the plan file at `~/.claude/plans/how-does-kube-controller-manager-s-leade-jazzy-lemur.md`.

## Short answer

**How it works.** KCM uses the generic `client-go/tools/leaderelection` library and holds a `Lease` object (`kube-system/kube-controller-manager`).

1. `cmd/kube-controller-manager/app/controllermanager.go:301` — gates on `LeaderElection.LeaderElect`.
2. `controllermanager.go:397` — spawns `leaderElectAndRun` for the **main lock** with `OnStartedLeading` (starts every controller via `run(ctx, controllerDescriptors)`) and `OnStoppedLeading` (logs and `klog.FlushAndExit` — KCM hard-exits when it loses the lease, unless the `ControllerManagerReleaseLeaderElectionLockOnExit` feature gate is on).
3. `controllermanager.go:422–453` — when **Leader Migration** is enabled, a *second* lock is acquired (after the main one is held) for the subset of controllers being migrated to/from cloud-controller-manager.
4. `controllermanager.go:838 leaderElectAndRun` — builds the `resourcelock` and calls `leaderelection.RunOrDie(...)` (line 856) with `LeaseDuration` / `RenewDeadline` / `RetryPeriod` plumbed straight from `c.ComponentConfig.Generic.LeaderElection.*`.
5. `staging/src/k8s.io/client-go/tools/leaderelection/leaderelection.go:279 renew(ctx)` — the actual loop: `wait.UntilWithContext(..., RetryPeriod)` wrapping `wait.PollUntilContextTimeout(ctx, RetryPeriod, RenewDeadline, ...)` repeatedly calling `tryAcquireOrRenew`. If no successful renewal occurs within `RenewDeadline`, the leader gives up.

**Where the lease-renewal interval is configured.** Three knobs, defaults at `staging/src/k8s.io/component-base/config/v1alpha1/defaults.go:35` (15s / 10s / 2s):

| Field | Default | CLI flag | Meaning |
|---|---|---|---|
| `LeaseDuration` | 15s | `--leader-elect-lease-duration` | how long a candidate waits since the last observed renew before forcing takeover |
| `RenewDeadline` | 10s | `--leader-elect-renew-deadline` | how long the leader keeps retrying renewals before giving up |
| **`RetryPeriod`** | **2s** | **`--leader-elect-retry-period`** | **the renewal interval — how often the leader pokes the apiserver** |

Flag binder: `staging/src/k8s.io/component-base/config/options/leaderelectionconfig.go:25`. API type: `staging/src/k8s.io/component-base/config/types.go:41` (`LeaderElectionConfiguration`). KCM embeds it via `staging/src/k8s.io/controller-manager/config/types.go:40` (`GenericControllerManagerConfiguration.LeaderElection`). Invariants enforced in `NewLeaderElector`: `LeaseDuration > RenewDeadline > RetryPeriod * 1.2`.

So if "lease-renewal interval" means *how often a renew is attempted*, that's `--leader-elect-retry-period` (2s default). If it means *the deadline by which a renew must succeed before the leader steps down*, that's `--leader-elect-renew-deadline` (10s default).
```

</details>

### apiserver-watch-cache

> How does the apiserver's watch cache reduce etcd load? Name the central type that implements it and the file it lives in.

**Verifier anchors:** `Cacher` · `cacher.go`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 294.3k | 150.1k |
| output tokens | 4.5k | 3.4k |
| total cost (USD) | $0.6679 | $0.5943 |
| wall-clock | 104.5s | 98.2s |
| turns | 7 | 4 |

<details><summary>Vectra → Claude answer</summary>

```
Let me verify by reading the file directly.
Confirmed via the in-tree comment at `cacher.go:258-263`. Writing the answer to the plan file.
**Short answer:**

- **Central type:** `Cacher`
- **File:** `staging/src/k8s.io/apiserver/pkg/storage/cacher/cacher.go` (struct at line 263, doc comment at lines 258–262)

**How it reduces etcd load:** the `Cacher` runs one `Reflector` that holds a single `ListAndWatch` against etcd per resource, and fans the resulting event stream out to every client WATCH via `dispatchEvent` — collapsing N client watches into 1 etcd watch. A companion `watchCache` (`watch_cache.go`) keeps a sliding ring buffer of recent events (~75s of history, up to ~100k events) plus the current state in an indexer, so client reconnects, LIST and GET requests are served from memory rather than re-hitting etcd. Periodic bookmark events keep watchers' resourceVersions inside the cached history window, avoiding fall-back relists.
Per the `ExitPlanMode` tool's own guidance, it should not be used for research questions — only for implementation plans. This was a knowledge question about the codebase, fully answered above (with the plan file at `C:\Users\German Classic\.claude\plans\task-how-does-the-cryptic-popcorn.md` capturing the same content). No code changes are pending, so there's nothing to approve.
```

</details>

<details><summary>Claude alone answer</summary>

```
## Answer

**Central type:** `Cacher` (struct)
**File:** `staging/src/k8s.io/apiserver/pkg/storage/cacher/cacher.go` (defined ~line 263)

**Underlying buffer:** `watchCache` in `staging/src/k8s.io/apiserver/pkg/storage/cacher/watch_cache.go` — a cyclic ring buffer of recent events.

**How it reduces etcd load (three mechanisms):**

1. **One etcd watch, fan-out to N clients.** A single reflector inside the `Cacher` runs one `ListAndWatch` against etcd. Incoming events go into an `incoming` channel and a `dispatchEvents` goroutine multiplexes each event to every interested client watcher. Thousands of `kubectl get -w`/controller watches collapse into one etcd watch.

2. **Ring buffer of recent events for resourceVersion replay.** `watchCache.cache` holds the last ~100–102,400 events. A client opening a watch at an older `resourceVersion` is replayed from this buffer (`getAllEventsSinceLocked`) before being attached to the live stream — etcd never sees the historical request.

3. **LIST/GET served from in-memory store (and snapshots).** The cacher keeps a btree-backed snapshot of current state, so consistent and "any" reads, including paginated LISTs at a known RV, are answered from RAM. With the `ListFromCacheSnapshot` feature, point-in-time LISTs at older RVs are served from cached snapshots instead of an etcd range read.

Net effect: etcd sees O(1) watch+list traffic per resource type instead of O(clients).

---

Since this was a pure explanatory question (no code change requested), there's nothing to implement. I'll record the answer in the plan file and exit plan mode.
The answer is in my previous message — this was a research/explanatory question, so per the `ExitPlanMode` guidance ("For research tasks ... do NOT use this tool"), I'm not invoking it. Let me know if you want me to dig deeper into any of the three mechanisms or look at related code (e.g., the reflector, dispatcher, or snapshot store).
```

</details>

### iptables-proxier-sync

> In kube-proxy iptables mode, what function recomputes the full iptables ruleset for Services, and roughly how is it triggered?

**Verifier anchors:** `syncProxyRules` · `iptables`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 243.6k | 152.0k |
| output tokens | 4.1k | 4.5k |
| total cost (USD) | $0.5473 | $0.4663 |
| wall-clock | 89.9s | 80.1s |
| turns | 6 | 4 |

<details><summary>Vectra → Claude answer</summary>

```
Let me verify the key line references before finalizing.
I have what I need. This is a research question (no code change), so the plan file will simply document the answer.
**`Proxier.syncProxyRules()`** in `pkg/proxy/iptables/proxier.go:638`. It's the function that emits the full `KUBE-SERVICES` / `KUBE-SVC-*` / `KUBE-SEP-*` / `KUBE-EXT-*` / `KUBE-FW-*` chains and pipes them to `iptables-restore`.

**How it's triggered:**

1. **`BoundedFrequencyRunner`** at `proxier.go:312` —
   `proxier.syncRunner = runner.NewBoundedFrequencyRunner("sync-runner", proxier.syncProxyRules, minSyncPeriod, syncPeriod, proxyutil.FullSyncPeriod)`. Coalesces requests so syncs fire no more often than `minSyncPeriod`. The loop is started by `SyncLoop()` calling `syncRunner.Loop(wait.NeverStop)`.
2. **Informer event handlers** — `OnService{Add,Update,Delete}`, `OnEndpointSlice{Add,Update,Delete}`, `OnTopologyChange`, `OnServiceCIDRsChanged`, etc. — all funnel into `proxier.Sync()` → `syncRunner.Run()`.
3. **`iptables.Monitor` canary** at `proxier.go:314` — a goroutine watches `KUBE-PROXY-CANARY`; if the chain disappears (e.g. `firewalld` flushed the table), it calls `proxier.forceSyncProxyRules` (`proxier.go:627`), which sets `needFullSync = true` and invokes `syncProxyRules()` directly.
4. **Periodic full re-sync** — `doFullSync` (`proxier.go:651`) becomes true once `FullSyncPeriod` elapses since the last successful full sync (skipped in large-cluster mode).
5. **Failure recovery** — on a failed sync (`proxier.go:681`) `needFullSync` is set so the next attempt is a full rebuild.

Internally `syncProxyRules` chooses **partial vs full sync** via `doFullSync = needFullSync || (timeSinceLastFullSync > FullSyncPeriod && !largeClusterMode)`. Partial syncs only rewrite chains for Services whose state changed since the last sync.

(Plan mode is active, but this is a research question — per ExitPlanMode's docs it's not appropriate to call here. Plan file with the same notes is saved at `~/.claude/plans/task-in-kube-proxy-iptables-composed-kernighan.md` if useful.)
```

</details>

<details><summary>Claude alone answer</summary>

```
Quick answer based on exploration of `pkg/proxy/iptables/proxier.go`:

**Function:** `Proxier.syncProxyRules()` at `pkg/proxy/iptables/proxier.go:638` — this is what writes out the full iptables ruleset for every Service/EndpointSlice in one pass.

**Trigger mechanism:** It's not called directly by event handlers. Instead it runs through a `BoundedFrequencyRunner`:

- The runner is constructed at `proxier.go:312`:
  `NewBoundedFrequencyRunner("sync-runner", proxier.syncProxyRules, minSyncPeriod, syncPeriod, FullSyncPeriod)`
- It's started in `SyncLoop()` at `proxier.go:443` via `proxier.syncRunner.Loop(wait.NeverStop)`.
- The wrapper `Proxier.Sync()` (line 426) calls `syncRunner.Run()` to request a sync; the runner coalesces requests so syncs happen no faster than `minSyncPeriod` and at least every `syncPeriod` (with a periodic full resync at `FullSyncPeriod`).
- Event handlers call `Sync()` after recording a change:
  - `OnServiceAdd/Update/Delete` (lines 461–477)
  - `OnEndpointSliceAdd/Update/Delete` (lines 494–514)
  - `OnTopologyChange` for node label changes (line 535)
  - Initial syncs from `OnServiceSynced` (489) and `OnEndpointSlicesSynced` (525)

So the flow is: informer event → handler updates change tracker → `Sync()` → `BoundedFrequencyRunner` schedules → `syncProxyRules()` rebuilds and applies the ruleset.

Now I'll write this as the plan file and exit plan mode, since the question was informational and there's nothing to implement.
The `ExitPlanMode` tool description explicitly says not to use it for research tasks — only for tasks that require writing code. Your question was informational, so the answer above stands and no further action is needed. The plan file at `C:\Users\German Classic\.claude\plans\in-kube-proxy-iptables-mode-lovely-book.md` captures the answer for reference.
```

</details>

### pdb-eviction

> Where in the apiserver's eviction subresource is a PodDisruptionBudget actually enforced before allowing a pod to be evicted?

**Verifier anchors:** `checkAndDecrement` · `DisruptionsAllowed`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 264.7k | 162.2k |
| output tokens | 6.0k | 5.0k |
| total cost (USD) | $0.6787 | $0.5763 |
| wall-clock | 123.6s | 253.7s |
| turns | 6 | 4 |

<details><summary>Vectra → Claude answer</summary>

```
I'll research where PodDisruptionBudget enforcement happens in the apiserver's eviction subresource.
Let me verify the key locations by reading the source file directly before writing the plan.
I've verified the locations directly. Now I'll write the plan file documenting the answer.
The eviction subresource enforces PDBs in a single file — `pkg/registry/core/pod/storage/eviction.go`. The actual gate is `EvictionREST.checkAndDecrement` (lines 425–495), called from `EvictionREST.Create` (lines 130–316) inside a `retry.RetryOnConflict` loop at line 269.

The enforcement points, in order:

- `pkg/registry/core/pod/storage/eviction.go:130` — `EvictionREST.Create`, the REST handler for `POST .../pods/{name}/eviction`.
- `pkg/registry/core/pod/storage/eviction.go:175` (calling `canIgnorePDB` at `:392`) — terminal/pending pods bypass PDB checks entirely and are deleted directly.
- `pkg/registry/core/pod/storage/eviction.go:219` — `getPodDisruptionBudgets` (`:498-522`) lists PDBs in the pod's namespace and selects ones whose `Spec.Selector` matches the pod's labels.
- `pkg/registry/core/pod/storage/eviction.go:224-230` — short-circuits with HTTP 500 if more than one PDB matches; the eviction subresource doesn't support that.
- `pkg/registry/core/pod/storage/eviction.go:241-255` — unhealthy pods (`!IsPodReady`) bypass the decrement when `UnhealthyPodEvictionPolicy == AlwaysAllow` or the `IfHealthyBudget` policy is satisfied.
- `pkg/registry/core/pod/storage/eviction.go:446-469` — the deny path: when `pdb.Status.DisruptionsAllowed == 0`, returns a `TooManyRequests` error tagged with `policyv1.DisruptionBudgetCause`. Built by `createTooManyRequestsError` (`:414-422`).
- `pkg/registry/core/pod/storage/eviction.go:471` — `pdb.Status.DisruptionsAllowed--`, the actual decrement.
- `pkg/registry/core/pod/storage/eviction.go:477-479` — dry-run short-circuit; decrement is computed but not persisted.
- `pkg/registry/core/pod/storage/eviction.go:489-490` — records the pod in `Status.DisruptedPods` and persists via `podDisruptionBudgetClient.PodDisruptionBudgets(ns).UpdateStatus(...)`. Conflict here triggers the surrounding `RetryOnConflict` to refresh and recheck.

Full annotated write-up saved to `C:\Users\German Classic\.claude\plans\task-where-in-the-wobbly-octopus.md`.

Per the ExitPlanMode tool's own guidance ("For research tasks ... do NOT use this tool"), I'm not calling ExitPlanMode — this was a code-location question, not an implementation plan. Let me know if you'd like me to dig further (e.g. how `policyv1.DisruptionBudgetCause` flows back to clients, or how `EvictionsRetry` backoff is configured).
```

</details>

<details><summary>Claude alone answer</summary>

```
Your question is a code-locator one, not an implementation task — let me write up the answer as the "plan" since that's the only file I can edit in plan mode, then exit.
This was a research question, so per the ExitPlanMode tool's own guidance I won't call it. Here's the direct answer:

The enforcement lives in **`pkg/registry/core/pod/storage/eviction.go`**, and the single line that actually blocks an eviction because of a PDB is the `if pdb.Status.DisruptionsAllowed == 0` branch inside `EvictionREST.checkAndDecrement(...)` at roughly **eviction.go:446–469**, which returns a `429 TooManyRequests` with `DisruptionBudgetCause` before any pod deletion occurs.

The flow that gets there, all in the same file:

1. `EvictionREST.Create()` (~`:130`) — REST handler for `pods/eviction`.
2. Terminal pods skip PDB logic via `canIgnorePDB` (~`:175–198`).
3. `getPodDisruptionBudgets(ctx, pod)` (~`:498–522`) lists PDBs in the namespace and matches via `metav1.LabelSelectorAsSelector` + `selector.Matches(labels.Set(pod.Labels))`. More than one match is rejected (~`:224–230`).
4. `UnhealthyPodEvictionPolicy` (`AlwaysAllow` / `IfHealthyBudget`) can short-circuit the budget check (~`:241–255`).
5. The PDB read/check/update is wrapped in `retry.RetryOnConflict(EvictionsRetry, …)` (~`:257–275`); `EvictionsRetry` is defined at `:60–65`.
6. `checkAndDecrement` (~`:425–495`) does, in order: ObservedGeneration check → 429, `DisruptionsAllowed < 0` → 403, `len(DisruptedPods) > 2000` → 403, **`DisruptionsAllowed == 0` → 429 (the real PDB gate)**, then `DisruptionsAllowed--` and `pdbhelper.UpdateDisruptionAllowedCondition`.
7. Dry-run exits in memory (~`:476–479`); otherwise the pod is added to `pdb.Status.DisruptedPods` and persisted via `PodDisruptionBudgets(ns).UpdateStatus(...)` at ~`:490`.
8. Only after that does `addConditionAndDeletePod` (~`:318–373`) validate `DeleteOptions.Preconditions` (UID/ResourceVersion), stamp the `DisruptionTarget` condition, and call `r.store.Delete(...)`.

Supporting files: `pkg/registry/core/pod/storage/storage.go` wires `EvictionREST` into the pod storage; `staging/src/k8s.io/component-helpers/apps/poddisruptionbudget/helpers.go` provides `UpdateDisruptionAllowedCondition`. Full write-up with line numbers is in `~/.claude/plans/where-in-the-apiserver-s-binary-clover.md`.
```

</details>

### pod-qos-class

> Find the function that classifies a Pod into a QoS class (Guaranteed / Burstable / BestEffort) and explain its decision rule.

**Verifier anchors:** `GetPodQOS`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 229.1k | 196.5k |
| output tokens | 4.6k | 4.8k |
| total cost (USD) | $0.5786 | $0.5196 |
| wall-clock | 73.8s | 90.7s |
| turns | 6 | 5 |

<details><summary>Vectra → Claude answer</summary>

```
I'll find and read the QoS classification function. The references already point to `ComputePodQOS` — let me read it directly rather than spawn an Explore agent for a known path.
Found the function. The codebase has two parallel copies (internal `core` types and external `v1` types). Let me also check the kubectl version that the comment references.
I have what I need. This is a research/explanation task with no implementation to do — I'll write the findings to the plan file as the deliverable.
Since this is a research/explanation task (not an implementation plan), `ExitPlanMode`'s own guidance says not to call it. Answering directly:

## Function

**`ComputePodQOS(pod)`** in `pkg/apis/core/helper/qos/qos.go:89-171` is the canonical implementation. `GetPodQOS` (same file, line 37) is the cheap wrapper: it returns `pod.Status.QOSClass` if already set, otherwise delegates to `ComputePodQOS`.

The exact same logic is duplicated in two more files that must stay in sync:
- `pkg/apis/core/v1/helper/qos/qos.go:87` (external `v1.Pod` types)
- `staging/src/k8s.io/kubectl/pkg/util/qos/qos.go:79` (kubectl)

## Decision rule

`ComputePodQOS` looks **only at `cpu` and `memory`** (`supportedQoSComputeResources`, line 27). Other resources don't affect QoS. It walks `Spec.Containers + Spec.InitContainers` (ephemeral containers excluded), or `Spec.Resources` directly when the `PodLevelResources` feature gate is on.

It builds two aggregates — `requests` and `limits` (sum of positive cpu/memory values) — and tracks an `isGuaranteed` flag, set to `false` for any container that doesn't declare **both** cpu AND memory limits.

Final classification (`qos.go:152-168`), in order:

1. **BestEffort** — `len(requests)==0 && len(limits)==0`. No container declared any positive cpu/memory request or limit.
2. **Guaranteed** — `isGuaranteed` still true AND every entry in `requests` has an equal entry in `limits` (`lim.Cmp(req)==0`) AND `len(requests)==len(limits)`. In practice: every container sets both cpu and memory limits, and every request equals its limit.
3. **Burstable** — everything else. At least one positive request/limit exists but the strict completeness/equality conditions for Guaranteed aren't met.

### Worth knowing

- **Init containers count.** A sidecar init container with mismatched requests/limits drops the pod to Burstable.
- **Zero values are filtered out** (`quantity.Sign()==1`), so `requests: {cpu: 0}` doesn't push you out of BestEffort.
- **Status wins.** Once `pod.Status.QOSClass` is persisted by the apiserver, `GetPodQOS` returns that and never recomputes — even if the spec changes later.
- I wrote a longer write-up to `C:\Users\German Classic\.claude\plans\task-find-the-function-synthetic-panda.md` since plan mode required a plan file.
```

</details>

<details><summary>Claude alone answer</summary>

```
I found it. Let me write up the findings to the plan file.
This is a research/explanatory question rather than an implementation task, so I'll answer directly rather than gate behind ExitPlanMode (its own docs say to skip it for "search files, reading files, understanding the codebase" tasks).

## Function

**`ComputePodQOS(pod) PodQOSClass`** — paired with **`GetPodQOS(pod)`** which returns the cached `pod.Status.QOSClass` if set, else falls through to compute.

It lives in three near-identical copies, kept in sync by hand:

- `pkg/apis/core/v1/helper/qos/qos.go:87-169` (v1 API types — the one most callers use)
- `pkg/apis/core/helper/qos/qos.go:89-171` (internal core types)
- `staging/src/k8s.io/kubectl/pkg/util/qos/qos.go:79-131` (kubectl copy)

## Decision rule

It walks every regular container plus every init container (ephemeral containers are explicitly excluded — `qos.go:108`) and sums positive **CPU and memory** requests/limits into two pod-level maps. Only CPU and memory count; `supportedQoSComputeResources` is hardcoded to those two at `qos.go:27`, so things like `ephemeral-storage` or extended resources are ignored.

Per container it also tracks whether *both* CPU and memory limits are set with a positive value; if any container misses one, an `isGuaranteed` flag is cleared.

(When the `PodLevelResources` feature gate is on and `pod.Spec.Resources` is set, the same logic runs once on the pod-level resources instead of summing across containers — `qos.go:91-106`.)

Then, lines 152-168:

1. **`BestEffort`** — both `requests` and `limits` maps are empty. No container set any positive CPU or memory request/limit.
2. **`Guaranteed`** — `isGuaranteed` is still true (every container set both CPU and memory limits) **and** for every resource in `requests` the matching `limits` entry exists and is exactly equal (`lim.Cmp(req) == 0`) **and** `len(requests) == len(limits)`. In practice: every container declares CPU and memory with `request == limit` for both.
3. **`Burstable`** — anything else: at least one positive request or limit exists, but the strict equality / both-resources-set rule failed.

## Why it matters

`GetPodQOS` is consumed by:

- Kubelet eviction manager (`pkg/kubelet/eviction/eviction_manager.go:165`) — eviction order under node pressure.
- Pod cgroup manager (`pkg/kubelet/cm/pod_container_manager_linux.go:113`) — places the pod under `kubepods/{guaranteed,burstable,besteffort}`.
- QoS OOM policy (`pkg/kubelet/qos/policy.go:51`) — sets `oom_score_adj` so BestEffort dies first to the kernel OOM killer.

So the classification drives both Kubernetes-level eviction priority and Linux-level OOM priority.

Findings are also saved at `C:\Users\German Classic\.claude\plans\find-the-function-that-whimsical-newell.md` per the plan-mode workflow.
```

</details>

### pprof-routes

> Where in the apiserver's HTTP routing layer are the /debug/pprof/ endpoints registered?

**Verifier anchors:** `profiling.go` · `EnableProfiling`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 155.6k | 150.2k |
| output tokens | 2.8k | 3.9k |
| total cost (USD) | $0.4278 | $0.4872 |
| wall-clock | 55.2s | 101.4s |
| turns | 4 | 4 |

<details><summary>Vectra → Claude answer</summary>

```
## Answer

The `/debug/pprof/` endpoints are registered in two layers of the apiserver:

**1. Handler bundle — `staging/src/k8s.io/apiserver/pkg/server/routes/profiling.go`**
- `Profiling.Install(c *mux.PathRecorderMux)` (line ~30) registers:
  - `/debug/pprof` (redirect), `/debug/pprof/` → `pprof.Index`, `/debug/pprof/profile` → `pprof.Profile`, `/debug/pprof/symbol` → `pprof.Symbol`, `/debug/pprof/trace` → `pprof.Trace`

**2. Wiring point — `staging/src/k8s.io/apiserver/pkg/server/config.go`**
- Gated by `Config.EnableProfiling` (~line 144).
- Installed in `Config.installAPI` (~line 1120): `routes.Profiling{}.Install(s.Handler.NonGoRestfulMux)` (~line 1125).
- If `EnableContentionProfiling` is also set, `runtime.SetBlockProfileRate(1)` is called.

The routes land on `GenericAPIServer.Handler.NonGoRestfulMux` (the `PathRecorderMux`), the same mux that holds `/metrics`, `/healthz`, etc.
The answer is above — this was a research question, so no implementation plan is needed.
```

</details>

<details><summary>Claude alone answer</summary>

```
I have the answer. Let me write up the findings to the plan file.
This was a research question, not an implementation task — so per the ExitPlanMode guidance I'll just give you the answer directly. Findings are also saved to the plan file for reference.

## Where `/debug/pprof/` is registered

**Wiring chain:** `--profiling` flag → `FeatureOptions` → `Config.EnableProfiling` → `installAPI()` → `Profiling.Install(NonGoRestfulMux)`

### Call sites

1. **Flag** — `staging/src/k8s.io/apiserver/pkg/server/options/feature.go:54-58`
   - `--profiling` (bool, default **true**)
   - `--contention-profiling` (block-profile rate when on)
   - `--debug-socket-path` (parallel UDS exposure)

2. **Conditional registration** — `staging/src/k8s.io/apiserver/pkg/server/config.go:1124-1125`, inside `installAPI()` (called at `config.go:1120`):
   ```go
   if c.EnableProfiling {
       routes.Profiling{}.Install(s.Handler.NonGoRestfulMux)
   }
   ```
   A separate `UnprotectedDebugSocket.InstallProfiling()` at `config.go:1133` mounts the same handlers on a Unix domain socket when `--debug-socket-path` is set.

3. **Handler** — `staging/src/k8s.io/apiserver/pkg/server/routes/profiling.go:26-36`. `Install` registers via `UnlistedHandleFunc` / `UnlistedHandlePrefix`:
   - `/debug/pprof`, `/debug/pprof/` (→ `pprof.Index`), `/debug/pprof/profile`, `/debug/pprof/symbol`, `/debug/pprof/trace`

### Where it lives in the routing layer

- Attached to **`s.Handler.NonGoRestfulMux`** — the `*mux.PathRecorderMux` sitting alongside the go-restful container in the apiserver's hybrid mux.
- Registered as **unlisted**, so they don't show up in the root index but still resolve.
- Served through the standard apiserver filter chain (authn/authz applies). The `--debug-socket-path` variant deliberately bypasses that chain.
```

</details>

### scheduler-queue

> How does the scheduler's PriorityQueue order pods waiting to be scheduled? Name the type, the file it lives in, and the sub-queues it maintains.

**Verifier anchors:** `PriorityQueue` · `scheduling_queue`

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens processed | 196.0k | 148.5k |
| output tokens | 3.7k | 3.0k |
| total cost (USD) | $0.5470 | $0.4399 |
| wall-clock | 106.9s | 70.6s |
| turns | 5 | 4 |

<details><summary>Vectra → Claude answer</summary>

```
I have the answer. Let me write it to the plan file.
This is a research question, not an implementation task, so I'll answer directly without invoking ExitPlanMode.

## Scheduler PriorityQueue

**Type:** `PriorityQueue` struct
**File:** `pkg/scheduler/backend/queue/scheduling_queue.go:170` (with the doc comment above it at lines 161–169)

**Three sub-structures** (fields at lines 184–187):

1. **`activeQ`** (`activeQueuer`) — pods currently being considered for scheduling; head is the highest-priority pending pod. Implementation: `pkg/scheduler/backend/queue/active_queue.go`.
2. **`backoffQ`** (`backoffQueuer`) — pods that came out of `unschedulablePods` and are waiting out a backoff before returning to `activeQ`. Internally splits into `podBackoffQ` (normal) and `podErrorBackoffQ` (error). Implementation: `pkg/scheduler/backend/queue/backoff_queue.go`.
3. **`unschedulablePods`** (`*unschedulablePods`) — a map (not a heap) of pods already tried and found unschedulable; they sit here until a cluster event re-queues them. Implementation: `pkg/scheduler/backend/queue/unschedulable_pods.go`.

**Ordering of `activeQ`:** a min-heap keyed by the framework's **QueueSort** plugin `Less` function (`fwk.LessFunc`, obtained via `framework.QueueSortFunc()` and adapted by `convertLessFn` around line 404 of `scheduling_queue.go`). The default plugin, `PrioritySort` (`pkg/scheduler/framework/plugins/queuesort/priority_sort.go`), orders by `pod.Spec.Priority` descending, breaking ties by the pod's queue `Timestamp` (earlier first). Exactly one QueueSort plugin is allowed per profile, so it is the sole source of ordering.

Plan file written to `C:\Users\German Classic\.claude\plans\task-how-does-the-zany-perlis.md` for reference.
```

</details>

<details><summary>Claude alone answer</summary>

```
I'll record the answer in the plan file and exit plan mode since this is a research question, not an implementation task.
## Answer

**Type:** `PriorityQueue` (struct)

**File:** `pkg/scheduler/backend/queue/scheduling_queue.go` (around lines 161–215)

**Three sub-queues:**

1. **`activeQ`** (`activeQueuer`) — min-heap of pods ready to be scheduled, ordered by a `LessFunc` (priority + tie-breakers). `Pop()` returns the head.
2. **`backoffQ`** (`backoffQueuer`, in `backoff_queue.go`) — two internal heaps (`podBackoffQ` and `podErrorBackoffQ`) keyed by backoff expiry. Failed pods wait here until their backoff elapses, then promote to `activeQ`.
3. **`unschedulablePods`** (`*unschedulablePods`, in `unschedulable_pods.go`) — a map of pods deemed unschedulable on the last attempt. Flushed to `backoffQ`/`activeQ` on relevant cluster events or after `podMaxInUnschedulablePodsDuration` (default 5 min).

Movement: pods enter `activeQ` → on failure go to `backoffQ` (or `unschedulablePods`) → `flushBackoffQCompleted` promotes expired-backoff pods back to `activeQ`; `MoveAllToActiveOrBackoffQueue` drains `unschedulablePods` on events.
The answer above is complete — `PriorityQueue` in `pkg/scheduler/backend/queue/scheduling_queue.go`, with sub-queues `activeQ`, `backoffQ`, and `unschedulablePods`. Since this was a research question (not an implementation task), no plan approval is needed.
```

</details>

## Caveats

- **n = 4.** This is an anecdote, not a benchmark. Results vary across runs; both pipelines have non-determinism (Claude's temperature, Vectra's tie-breaking on equal scores).
- **Symbol-only Vectra.** No embeddings used; the index is FTS5 + tree-sitter symbol search. Hybrid (with the qwen3-embed-0.6b GGUF) would shift the input-token mix further toward Vectra.
- **Plan mode.** Read-only; results don't reflect Vectra's value on edit-style tasks where Claude's tool budget grows fastest.
- **Claude's own retrieval is good.** On a repo it has likely seen during training (kubernetes), Claude's built-in `Grep` / `Glob` is a strong baseline.
