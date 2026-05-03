# Vectra vs Claude Code · edit-task benchmark

**Repo under test:** [kubernetes/kubernetes](https://github.com/kubernetes/kubernetes) (shallow clone, ~17k Go files, ~245k symbol chunks).

Each task asks for a small, well-scoped code edit (add a comment, rename a helper, add a one-line log). Both pipelines run with `--permission-mode bypassPermissions` so Edit / Write / Bash all execute without interactive approval. Between every run the harness `git reset --hard HEAD`s the kubernetes clone so the two pipelines start from an identical tree. Verifier checks the captured diff: was `verify.touched_path` modified, and do the changed lines contain `must_contain_after`?

**Configs included.** `symbol-only`.

- `symbol-only` — `(no flags)`

## Aggregate

| metric | Vectra · symbol-only | Claude alone |
|---|---:|---:|
| **answers correct** | **5 / 5** | **5 / 5** |
| context tokens (sum) | 692.5k (-8%) | 756.7k |
| output tokens (sum) | 5.4k (+23%) | 4.4k |
| total cost USD (sum) | $0.9022 (+65%) | $0.5454 |
| wall-clock (sum) | 101.8s (+38%) | 73.8s |
| turns (sum) | 17 (-23%) | 22 |

"answers correct" = the verifier accepts the answer (research: anchors found in text; edit: diff touched the right file and added the right contents). Generic anchors are avoided so a wrong path does not pass.

"context tokens" = `input_tokens + cache_creation + cache_read`, i.e. the actual prompt volume Claude saw (cache hits are still work, just billed cheaper).

Percentages in Vectra cells are relative to Claude-alone — lower is usually better for tokens / cost / time.

## Per-task detail

### edit-add-comment-getpodqos

> In the Kubernetes source under `pkg/apis/core/helper/qos/qos.go`, add a single-line comment immediately above the `GetPodQOS` function declaration that says: `// GetPodQOS classifies a pod into Guaranteed, Burstable, or BestEffort.`

**Verifier (in `pkg/apis/core/helper/qos/qos.go`):** `// GetPodQOS classifies a pod into Guaranteed, Burstable, or BestEffort.` · `func GetPodQOS`

| metric | Vectra · symbol-only | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens | 122.0k | 106.3k |
| output tokens | 952 | 600 |
| cost (USD) | $0.2121 | $0.0870 |
| wall-clock | 14.8s | 10.7s |
| turns | 3 | 3 |

<details><summary>Vectra · symbol-only diff</summary>

```diff
diff --git a/pkg/apis/core/helper/qos/qos.go b/pkg/apis/core/helper/qos/qos.go
index 4f81d646..60b31aad 100644
--- a/pkg/apis/core/helper/qos/qos.go
+++ b/pkg/apis/core/helper/qos/qos.go
@@ -34,6 +34,7 @@ func isSupportedQoSComputeResource(name core.ResourceName) bool {
 
 // GetPodQOS returns the QoS class of a pod persisted in the PodStatus.QOSClass field.
 // If PodStatus.QOSClass is empty, it returns value of ComputePodQOS() which evaluates pod's QoS class.
+// GetPodQOS classifies a pod into Guaranteed, Burstable, or BestEffort.
 func GetPodQOS(pod *core.Pod) core.PodQOSClass {
 	if pod.Status.QOSClass != "" {
 		return pod.Status.QOSClass
```

</details>

<details><summary>Claude alone diff</summary>

```diff
diff --git a/pkg/apis/core/helper/qos/qos.go b/pkg/apis/core/helper/qos/qos.go
index 4f81d646..60b31aad 100644
--- a/pkg/apis/core/helper/qos/qos.go
+++ b/pkg/apis/core/helper/qos/qos.go
@@ -34,6 +34,7 @@ func isSupportedQoSComputeResource(name core.ResourceName) bool {
 
 // GetPodQOS returns the QoS class of a pod persisted in the PodStatus.QOSClass field.
 // If PodStatus.QOSClass is empty, it returns value of ComputePodQOS() which evaluates pod's QoS class.
+// GetPodQOS classifies a pod into Guaranteed, Burstable, or BestEffort.
 func GetPodQOS(pod *core.Pod) core.PodQOSClass {
 	if pod.Status.QOSClass != "" {
 		return pod.Status.QOSClass
```

</details>

### edit-rename-helper-priority

> In `pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go`, find the unexported helper named `isPreemptionAllowed` and rename it to `isVictimEligibleForPreemption` everywhere it is defined or called *within that single file*. Do not modify any other files.

**Verifier (in `pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go`):** `isVictimEligibleForPreemption`

| metric | Vectra · symbol-only | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens | 131.9k | 206.7k |
| output tokens | 786 | 1.1k |
| cost (USD) | $0.1451 | $0.1437 |
| wall-clock | 15.6s | 18.1s |
| turns | 3 | 6 |

<details><summary>Vectra · symbol-only diff</summary>

```diff
diff --git a/pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go b/pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go
index 34a1b681..134cbfcf 100644
--- a/pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go
+++ b/pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go
@@ -277,7 +277,7 @@ func (pl *DefaultPreemption) SelectVictimsOnNode(
 	// As the first step, remove all pods eligible for preemption from the node and
 	// check if the given pod can be scheduled without them present.
 	for _, pi := range nodeInfo.GetPods() {
-		if pl.isPreemptionAllowed(nodeInfo, pi, pod) {
+		if pl.isVictimEligibleForPreemption(nodeInfo, pi, pod) {
 			potentialVictims = append(potentialVictims, pi)
 		}
 	}
@@ -376,7 +376,7 @@ func (pl *DefaultPreemption) PodEligibleToPreemptOthers(_ context.Context, pod *
 
 		if nodeInfo, _ := nodeInfos.Get(nomNodeName); nodeInfo != nil {
 			for _, p := range nodeInfo.GetPods() {
-				if pl.isPreemptionAllowed(nodeInfo, p, pod) && podTerminatingByPreemption(p.GetPod()) {
+				if pl.isVictimEligibleForPreemption(nodeInfo, p, pod) && podTerminatingByPreemption(p.GetPod()) {
 					// There is a terminating pod on the nominated node.
 					return false, "not eligible due to a terminating pod on the nominated node."
 				}
@@ -391,8 +391,8 @@ func (pl *DefaultPreemption) OrderedScoreFuncs(ctx context.Context, nodesToVicti
 	return nil
 }
 
-// isPreemptionAllowed returns whether the victim residing on nodeInfo can be preempted by the preemptor
-func (pl *DefaultPreemption) isPreemptionAllowed(nodeInfo fwk.NodeInfo, victim fwk.PodInfo, preemptor *v1.Pod) bool {
+// isVictimEligibleForPreemption returns whether the victim residing on nodeInfo can be preempted by the preemptor
+func (pl *DefaultPreemption) isVictimEligibleForPreemption(nodeInfo fwk.NodeInfo, victim fwk.PodInfo, preemptor *v1.Pod) bool {
 	// The victim must have lower priority than the preemptor, in addition to any filtering implemented by IsEligiblePod
 	return corev1helpers.PodPriority(victim.GetPod()) < corev1helpers.PodPriority(preemptor) && pl.IsEligiblePod(nodeInfo, victim, preemptor)
 }
```

</details>

<details><summary>Claude alone diff</summary>

```diff
diff --git a/pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go b/pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go
index 34a1b681..134cbfcf 100644
--- a/pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go
+++ b/pkg/scheduler/framework/plugins/defaultpreemption/default_preemption.go
@@ -277,7 +277,7 @@ func (pl *DefaultPreemption) SelectVictimsOnNode(
 	// As the first step, remove all pods eligible for preemption from the node and
 	// check if the given pod can be scheduled without them present.
 	for _, pi := range nodeInfo.GetPods() {
-		if pl.isPreemptionAllowed(nodeInfo, pi, pod) {
+		if pl.isVictimEligibleForPreemption(nodeInfo, pi, pod) {
 			potentialVictims = append(potentialVictims, pi)
 		}
 	}
@@ -376,7 +376,7 @@ func (pl *DefaultPreemption) PodEligibleToPreemptOthers(_ context.Context, pod *
 
 		if nodeInfo, _ := nodeInfos.Get(nomNodeName); nodeInfo != nil {
 			for _, p := range nodeInfo.GetPods() {
-				if pl.isPreemptionAllowed(nodeInfo, p, pod) && podTerminatingByPreemption(p.GetPod()) {
+				if pl.isVictimEligibleForPreemption(nodeInfo, p, pod) && podTerminatingByPreemption(p.GetPod()) {
 					// There is a terminating pod on the nominated node.
 					return false, "not eligible due to a terminating pod on the nominated node."
 				}
@@ -391,8 +391,8 @@ func (pl *DefaultPreemption) OrderedScoreFuncs(ctx context.Context, nodesToVicti
 	return nil
 }
 
-// isPreemptionAllowed returns whether the victim residing on nodeInfo can be preempted by the preemptor
-func (pl *DefaultPreemption) isPreemptionAllowed(nodeInfo fwk.NodeInfo, victim fwk.PodInfo, preemptor *v1.Pod) bool {
+// isVictimEligibleForPreemption returns whether the victim residing on nodeInfo can be preempted by the preemptor
+func (pl *DefaultPreemption) isVictimEligibleForPreemption(nodeInfo fwk.NodeInfo, victim fwk.PodInfo, preemptor *v1.Pod) bool {
 	// The victim must have lower priority than the preemptor, in addition to any filtering implemented by IsEligiblePod
 	return corev1helpers.PodPriority(victim.GetPod()) < corev1helpers.PodPriority(preemptor) && pl.IsEligiblePod(nodeInfo, victim, preemptor)
 }
```

</details>

### edit-add-log-rankmemory

> In `pkg/kubelet/eviction/helpers.go`, add a single `klog.V(5).InfoS("rankMemoryPressure: ranking pods", "count", len(pods))` line as the very first statement inside the body of `rankMemoryPressure`. Do not modify any other function. Make sure the existing `k8s.io/klog/v2` import stays valid.

**Verifier (in `pkg/kubelet/eviction/helpers.go`):** `rankMemoryPressure: ranking pods` · `func rankMemoryPressure`

| metric | Vectra · symbol-only | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens | 148.1k | 170.7k |
| output tokens | 775 | 1.0k |
| cost (USD) | $0.1984 | $0.1200 |
| wall-clock | 14.2s | 18.0s |
| turns | 4 | 5 |

<details><summary>Vectra · symbol-only diff</summary>

```diff
diff --git a/pkg/kubelet/eviction/helpers.go b/pkg/kubelet/eviction/helpers.go
index 6807dd97..5e79bad2 100644
--- a/pkg/kubelet/eviction/helpers.go
+++ b/pkg/kubelet/eviction/helpers.go
@@ -817,6 +817,7 @@ func cmpBool(a, b bool) int {
 // It ranks by whether or not the pod's usage exceeds its requests, then by priority, and
 // finally by memory usage above requests.
 func rankMemoryPressure(pods []*v1.Pod, stats statsFunc) {
+	klog.V(5).InfoS("rankMemoryPressure: ranking pods", "count", len(pods))
 	orderedBy(exceedMemoryRequests(stats), priority, memory(stats)).Sort(pods)
 }
```

</details>

<details><summary>Claude alone diff</summary>

```diff
diff --git a/pkg/kubelet/eviction/helpers.go b/pkg/kubelet/eviction/helpers.go
index 6807dd97..5e79bad2 100644
--- a/pkg/kubelet/eviction/helpers.go
+++ b/pkg/kubelet/eviction/helpers.go
@@ -817,6 +817,7 @@ func cmpBool(a, b bool) int {
 // It ranks by whether or not the pod's usage exceeds its requests, then by priority, and
 // finally by memory usage above requests.
 func rankMemoryPressure(pods []*v1.Pod, stats statsFunc) {
+	klog.V(5).InfoS("rankMemoryPressure: ranking pods", "count", len(pods))
 	orderedBy(exceedMemoryRequests(stats), priority, memory(stats)).Sort(pods)
 }
```

</details>

### edit-extend-leader-election-comment

> In `cmd/kube-controller-manager/app/controllermanager.go`, find the function `leaderElectAndRun` and add a doc comment immediately above its declaration. The comment must say: `// leaderElectAndRun runs the controller manager under a Lease-based leader election.` Replace any existing single-line doc comment on that declaration; do not add a duplicate.

**Verifier (in `cmd/kube-controller-manager/app/controllermanager.go`):** `// leaderElectAndRun runs the controller manager under a Lease-based leader election.` · `func leaderElectAndRun`

| metric | Vectra · symbol-only | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens | 116.6k | 136.3k |
| output tokens | 2.1k | 849 |
| cost (USD) | $0.2323 | $0.0972 |
| wall-clock | 42.8s | 13.4s |
| turns | 3 | 4 |

<details><summary>Vectra · symbol-only diff</summary>

```diff
diff --git a/cmd/kube-controller-manager/app/controllermanager.go b/cmd/kube-controller-manager/app/controllermanager.go
index 016b00bb..e4f96012 100644
--- a/cmd/kube-controller-manager/app/controllermanager.go
+++ b/cmd/kube-controller-manager/app/controllermanager.go
@@ -835,8 +835,7 @@ func createClientBuilders(c *config.CompletedConfig) (clientBuilder clientbuilde
 	return
 }
 
-// leaderElectAndRun runs the leader election, and runs the callbacks once the leader lease is acquired.
-// TODO: extract this function into staging/controller-manager
+// leaderElectAndRun runs the controller manager under a Lease-based leader election.
 func leaderElectAndRun(ctx context.Context, c *config.CompletedConfig, lockIdentity string, electionChecker *leaderelection.HealthzAdaptor, resourceLock string, leaseName string, callbacks leaderelection.LeaderCallbacks) {
 	logger := klog.FromContext(ctx)
 	rl, err := resourcelock.NewFromKubeconfig(resourceLock,
```

</details>

<details><summary>Claude alone diff</summary>

```diff
diff --git a/cmd/kube-controller-manager/app/controllermanager.go b/cmd/kube-controller-manager/app/controllermanager.go
index 016b00bb..6387c664 100644
--- a/cmd/kube-controller-manager/app/controllermanager.go
+++ b/cmd/kube-controller-manager/app/controllermanager.go
@@ -835,7 +835,7 @@ func createClientBuilders(c *config.CompletedConfig) (clientBuilder clientbuilde
 	return
 }
 
-// leaderElectAndRun runs the leader election, and runs the callbacks once the leader lease is acquired.
+// leaderElectAndRun runs the controller manager under a Lease-based leader election.
 // TODO: extract this function into staging/controller-manager
 func leaderElectAndRun(ctx context.Context, c *config.CompletedConfig, lockIdentity string, electionChecker *leaderelection.HealthzAdaptor, resourceLock string, leaseName string, callbacks leaderelection.LeaderCallbacks) {
 	logger := klog.FromContext(ctx)
```

</details>

### edit-add-helper-pdb-eviction

> In `pkg/registry/core/pod/storage/eviction.go`, add a small unexported helper function named `pdbAllowsEviction` immediately above `checkAndDecrement`. The helper takes a single `*policyv1.PodDisruptionBudget` argument and returns `b.Status.DisruptionsAllowed > 0`. Do not modify any other function in the file, and do not change any imports.

**Verifier (in `pkg/registry/core/pod/storage/eviction.go`):** `func pdbAllowsEviction` · `b.Status.DisruptionsAllowed > 0` · `func (r *EvictionREST)`

| metric | Vectra · symbol-only | Claude alone |
|---|---:|---:|
| answer correct | ✓ | ✓ |
| context tokens | 173.9k | 136.8k |
| output tokens | 757 | 812 |
| cost (USD) | $0.1142 | $0.0975 |
| wall-clock | 14.5s | 13.6s |
| turns | 4 | 4 |

<details><summary>Vectra · symbol-only diff</summary>

```diff
diff --git a/pkg/registry/core/pod/storage/eviction.go b/pkg/registry/core/pod/storage/eviction.go
index 412b7ef5..f9e8eebd 100644
--- a/pkg/registry/core/pod/storage/eviction.go
+++ b/pkg/registry/core/pod/storage/eviction.go
@@ -421,6 +421,10 @@ func createTooManyRequestsError(name string) error {
 	return err
 }
 
+func pdbAllowsEviction(b *policyv1.PodDisruptionBudget) bool {
+	return b.Status.DisruptionsAllowed > 0
+}
+
 // checkAndDecrement checks if the provided PodDisruptionBudget allows any disruption.
 func (r *EvictionREST) checkAndDecrement(namespace string, podName string, pdb policyv1.PodDisruptionBudget, dryRun bool) error {
 	if pdb.Status.ObservedGeneration < pdb.Generation {
```

</details>

<details><summary>Claude alone diff</summary>

```diff
diff --git a/pkg/registry/core/pod/storage/eviction.go b/pkg/registry/core/pod/storage/eviction.go
index 412b7ef5..f9e8eebd 100644
--- a/pkg/registry/core/pod/storage/eviction.go
+++ b/pkg/registry/core/pod/storage/eviction.go
@@ -421,6 +421,10 @@ func createTooManyRequestsError(name string) error {
 	return err
 }
 
+func pdbAllowsEviction(b *policyv1.PodDisruptionBudget) bool {
+	return b.Status.DisruptionsAllowed > 0
+}
+
 // checkAndDecrement checks if the provided PodDisruptionBudget allows any disruption.
 func (r *EvictionREST) checkAndDecrement(namespace string, podName string, pdb policyv1.PodDisruptionBudget, dryRun bool) error {
 	if pdb.Status.ObservedGeneration < pdb.Generation {
```

</details>

