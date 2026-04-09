# treeland-demo-client

SDL3 + Wayland `xdg-foreign-unstable-v2` demo client.

It supports two flows:

- export its own SDL toplevel and print the exported handle
- import an external handle and set that foreign toplevel as the parent of this window

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

Start one exporter:

```bash
./build/treeland-demo-client
```

Start another instance and import the printed handle:

```bash
./build/treeland-demo-client --import <HANDLE>
```

Optional title:

```bash
./build/treeland-demo-client --title "child window"
```

## Dependencies

- `SDL3`
- `wayland-client`
- `wayland-scanner`

