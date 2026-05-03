# Vectra vs Claude Code · proof-of-concept run

**Repo under test:** [kubernetes/kubernetes](https://github.com/kubernetes/kubernetes) (shallow clone, ~17k Go files, ~245k symbol chunks).

**What this measures.** Each task is run twice in the same shell, in the same kubernetes/ directory:

- **Vectra path** — `vectra ask "$task" --stream-json --permission-mode plan`. Vectra runs hybrid retrieval (FTS5 over symbol-only index, no embeddings) and hands the top chunks to `claude -p` as labeled context.
- **Claude-only path** — `claude -p "$task" --output-format=stream-json --permission-mode plan`. Claude Code does its own retrieval via its built-in `Glob` / `Grep` / `Read` tools.

Plan mode keeps both runs read-only, so the comparison is purely *can the system find the right code and reason about it*. Token / cost / wall-clock totals come from each run's final `result` event.

## Aggregate

| metric | Vectra → Claude | Claude alone | Δ |
|---|---:|---:|---:|
| context tokens processed (sum) | 1046.1k | 888.8k | (+18%) |
| output tokens (sum) | 27.8k | 36.1k | (-23%) |
| total cost (USD) | $3.2478 | $3.9321 | (-17%) |
| wall-clock (sum) | 595.1s | 705.0s | (-16%) |
| turns (sum) | 29 | 34 | (-15%) |

"context tokens processed" = `input_tokens + cache_creation + cache_read`, the actual prompt volume Claude saw (cache hits are still work, just billed cheaper).

"Δ" = Vectra path relative to Claude-alone (lower is usually better for tokens / cost / time; turns is mixed signal).

## Per-task detail

### scheduler-priority

> In the kube-scheduler, where is a Pod's priority used to decide which existing pods to preempt when there is no room? Name the function and the file.

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
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

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
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

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
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

| metric | Vectra → Claude | Claude alone |
|---|---:|---:|
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

## Caveats

- **n = 4.** This is an anecdote, not a benchmark. Results vary across runs; both pipelines have non-determinism (Claude's temperature, Vectra's tie-breaking on equal scores).
- **Symbol-only Vectra.** No embeddings used; the index is FTS5 + tree-sitter symbol search. Hybrid (with the qwen3-embed-0.6b GGUF) would shift the input-token mix further toward Vectra.
- **Plan mode.** Read-only; results don't reflect Vectra's value on edit-style tasks where Claude's tool budget grows fastest.
- **Claude's own retrieval is good.** On a repo it has likely seen during training (kubernetes), Claude's built-in `Grep` / `Glob` is a strong baseline.
