# Cosmos

The styling and animation layer for Chaos.

## Branches

- `main` is reserved for the canonical implementation. It currently contains only
  this README; the canonical implementation has not been started.
- [`proto`](https://github.com/DeanWilsonDev/cosmos/tree/proto) contains the actively
  developed Lustre prototype, including its complete history from `lustre`.

To build or consume Lustre, check out `proto`:

```sh
git clone --branch proto --recurse-submodules https://github.com/DeanWilsonDev/cosmos.git
```

Lustre retains its existing names, `.lustre` files and CMake targets on the
prototype branch. Its README documents its build, tests and editor integration.
Prototype dependencies should track `proto`, rather than this README-only `main`.
