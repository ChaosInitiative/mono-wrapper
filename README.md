## Mono Wrapper

Simple C++ wrapper around the Mono runtime. This is still work in progress, so you'll probably want to hold off on using it!

### Requirements

C++17 compliant compiler

### TODO

* Move to CMake
* Support windows
* CI

### Building

```
make
```

### Running

```
./mono-tst <path to managed assembly>
```

You should see some performance metrics printed to the console, and that's it.
