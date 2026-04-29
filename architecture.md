```mermaid
flowchart TD
    subgraph INDEXING["1. Indexing Pipeline · runs on startup & file change"]
        A["Project files\nsrc/, headers, tests"] -->|filter build/ .git/ node_modules| B["Tree-Sitter Parser\nAST extraction · 20+ languages"]
        B -->|functions, classes, blocks| C["Merkle Index\nblake3 hash per node"]
        C -->|changed nodes only| D1["Level 1\nFunctions & methods\nsignature + body + docstring"]
        C -->|changed nodes only| D2["Level 2\nClass headers\nfields + method signatures"]
        C -->|changed nodes only| D3["Level 3\nCall-graph context\ncalled_by + calls"]
        C -->|once per file change| D4["Level 4\nFile summary\nLLM-generated, cached"]
        C -->|identifier extraction| D5["Symbol Index\nidentifiers, paths, types\nBM25 / trigram"]
    end

    subgraph EMBED["2. Embedding Engine · GPU-accelerated via llama.cpp"]
        E1["GPU Detection\ncudaMemGetInfo / hipMemGetInfo / Metal"] -->|VRAM < 6GB| E_S["Qwen3-Embedding-0.6B\nQ8_0 · ~1.5GB VRAM"]
        E1 -->|VRAM 8–12GB| E_M["Qwen3-Embedding-4B\nQ8_0 · ~6GB VRAM"]
        E1 -->|VRAM 16GB+| E_L["Qwen3-Embedding-8B\nFP16 · ~16GB VRAM"]
        E1 -->|no GPU| E_C["Qwen3-Embedding-0.6B\nQ8_0 · CPU AVX2/NEON"]
        E_S & E_M & E_L & E_C --> EF["Embedding Forward Pass\nllama_decode · pooling=LAST_TOKEN (EOS)\nL2-normalized · dim: 1024 / 2560 / 4096"]
    end

    subgraph STORE["3. Vector Store · fully local, zero-config"]
        V1["usearch Index\nHNSW · AVX2/NEON · single-header"]
        V2["SQLite DB\nhash TEXT PK · embedding BLOB\nfile_path · chunk_type · updated_at\nmodel_id · embed_dim"]
        V3["Symbol Index\nFTS5 + trigram\nfor exact identifier match"]
        V1 <-->|sync on index build| V2
        V2 <-.->|model/dim mismatch → full reindex| V1
    end

    subgraph QUERY["4. Query Processing · target 150–300ms GPU / 1–2s CPU"]
        Q1["User query\nnatural language"] -->|Instruct prefix ONLY on query side\n'Given a code search query, retrieve relevant code'| Q2["Query embedding\nsame model as index\npooling=LAST_TOKEN"]
        Q1 -->|extract symbols / paths| QS["Symbol search\nBM25 + trigram"]
        Q2 -->|cosine similarity ANN| Q3["usearch k-NN search\ntop-50 candidates"]
        QS -->|exact identifier hits| QF["Hybrid fusion\nReciprocal Rank Fusion\nvector + BM25"]
        Q3 --> QF
        QF -->|top-50 fused| QR["Cross-Encoder Reranker\nQwen3-Reranker-0.6B\ntop-10 after rerank"]
        QR -->|rerank score + chunk type| Q4["Context builder\nfn body + signature\n+ callers + callees + imports"]
        Q4 -->|context budget enforcement\ntoken-aware truncation| Q5["Prompt assembly\n[FILE][TYPE] header\ncode + imports + callers"]
    end

    subgraph PLAN["5. Planning Step · for multi-file edits"]
        P1["Task classifier\nsingle-edit vs multi-file"] -->|trivial| P2["Direct patch path"]
        P1 -->|complex| P3["Tool-using agent loop\nread_file · grep · list_callers · run_tests"]
        P3 -->|gathered context| P4["Edit plan\nordered file changes\nwith rationale"]
    end

    subgraph EXEC["6. Execution Loop · self-healing"]
        X1["LLM Inference\nllama.cpp · DeepSeek-Coder / Qwen2.5-Coder\nn_gpu_layers = -1"] -->|generated patch| X2["Diff Generator\nMyers diff · libgit2\nvalidate syntax via tree-sitter first"]
        X2 -->|patch valid| X3["Auto Apply\natomic write\nbackup before patch"]
        X2 -->|patch invalid| X4["Error Handler\ncollect compiler/linter output\nbuild retry context"]
        X3 -->|build & test via language adapter| XA["Language Adapter\ncargo · cmake · npm · pytest\nmaven · go test · ..."]
        XA --> X5{"Success?"}
        X5 -->|yes| X6["Done\nupdate Merkle index"]
        X5 -->|no| X4
        X4 -->|retry with error context\nmax 3 attempts · then escalate| X1
    end

    D1 & D2 & D3 & D4 -->|format: raw passage, NO instruct prefix| EF
    D5 --> V3
    EF -->|store vector + metadata| V2
    EF -->|insert into HNSW graph| V1
    V1 -->|ANN lookup| Q3
    V3 -->|symbol lookup| QS
    Q5 --> P1
    P2 --> X1
    P4 --> X1
    X6 -.->|incremental re-index changed files| C
```

## Notes on key design decisions

**Pooling strategy.** Qwen3-Embedding is trained with last-token (EOS) pooling. Mean pooling silently degrades retrieval quality by 5–15% on code benchmarks — we use last-token pooling end-to-end.

**Asymmetric instruct prefix.** Qwen3-Embedding expects an instruction prefix on the **query** side only. Documents (code chunks) are embedded as raw passages. Mixing this up makes the model perform worse than no prefix at all.

**Quantization.** Generation models tolerate Q4_K_M well. Embedding models do not — Q4 collapses the fine-grained similarity geometry that retrieval depends on. We use Q8_0 minimum, FP16 for the 8B model where VRAM allows.

**Hybrid retrieval.** Pure vector search misses exact identifier matches (function names, file paths, error strings). The BM25/trigram symbol index runs in parallel and is fused via Reciprocal Rank Fusion before reranking.

**Cross-encoder reranker.** Heuristic reranking on "recency" is a weak signal for code relevance. Qwen3-Reranker-0.6B reorders the top-50 candidates by true semantic match against the query, which is where most quality gains live.

**Index migration.** Embedding dimension is tied to model size. The SQLite schema records `model_id` and `embed_dim`; on mismatch we trigger a full reindex rather than silently mixing geometries.

**Planning step.** Trivial single-file edits go straight to the patch path. Anything multi-file routes through a tool-using agent loop (read_file, grep, list_callers, run_tests) before generating a structured edit plan.

**Language adapters.** The self-healing loop needs structured build/test output per language. Adapters wrap cargo/cmake/npm/pytest/maven/go test/etc. and normalize errors into a common retry-context format.

**Realistic latency.** End-to-end query (embed + ANN + rerank + context build) is 150–300ms on GPU, 1–2s on CPU. The previous <50ms target was not achievable with a real embedding model in the loop.
