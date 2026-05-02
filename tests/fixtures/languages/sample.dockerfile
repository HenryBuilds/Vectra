# Minimal Dockerfile fixture for language parse-validation tests.

FROM debian:bookworm AS builder
WORKDIR /src
COPY . .
RUN apt-get update && apt-get install -y build-essential
RUN make release

FROM debian:bookworm-slim
COPY --from=builder /src/build/demo /usr/local/bin/demo
USER nobody
ENTRYPOINT ["/usr/local/bin/demo"]
CMD ["--help"]
