## ChaosScript Proof of Concept

Currently this repo contains small ChaosScript tests, a C++ Mono wrapper and a script system interface.

Only tested on Linux, as this is a proof of concept and where I do my development. Everything is being written
with standard C++, so porting to Windows should be as simple as linking against the mono library.

### Building

```
make
```

### Running

```
./mono-tst <path to managed assembly>
```

You should see some performance metrics printed to the console, and that's it.