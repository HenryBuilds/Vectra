; Chunk-level constructs in Dockerfile.
;
; Each instruction becomes its own chunk so retrieval can land on a
; specific line — `which COPY brings in the source?`, `what does the
; second-stage FROM use?`. Multi-instruction RUN commands stay a
; single chunk; that matches how people read them as one logical
; build step.
(from_instruction) @chunk
(run_instruction) @chunk
(copy_instruction) @chunk
(add_instruction) @chunk
(workdir_instruction) @chunk
(env_instruction) @chunk
(arg_instruction) @chunk
(expose_instruction) @chunk
(volume_instruction) @chunk
(entrypoint_instruction) @chunk
(cmd_instruction) @chunk
(label_instruction) @chunk
(user_instruction) @chunk
(healthcheck_instruction) @chunk
(shell_instruction) @chunk
(stopsignal_instruction) @chunk
(maintainer_instruction) @chunk
(onbuild_instruction) @chunk
