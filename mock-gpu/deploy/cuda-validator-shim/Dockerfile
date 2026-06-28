# No-op validator image (PLAN §3, §5.6). Replaces gpu-operator-validator so all
# validations pass on the mock GPU. Build via top-level `make validator-shim`.
FROM busybox:stable
COPY entrypoint.sh /usr/local/bin/validator-shim
RUN chmod +x /usr/local/bin/validator-shim
ENTRYPOINT ["/usr/local/bin/validator-shim"]
