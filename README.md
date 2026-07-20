# GASNet-EX Software Release

GASNet-EX is the next generation of the GASNet-1 communication system. The GASNet interfaces are being redesigned to accommodate the emerging needs of exascale supercomputing, providing communication services to a variety of PGAS programming models on current and future HPC architectures.

GASNet-EX is a work-in-progress. Many features remain to be specified, implemented, and/or tuned.

Users interested in learning what's new in GASNet-EX are recommended to peruse the [ChangeLog](ChangeLog) for a summary of recent developments. The [GASNet-EX specification](docs/GASNet-EX.txt) provides more detailed information about the evolving EX specification.

GASNet-EX notably includes a backwards-compatibility layer to assist in migration of current GASNet-1 client software. Existing GASNet clients can get started by relying on this layer (provided in `gasnet.h`), and incrementally add calls to the new `gex_*` interfaces (defined in `gasnetex.h`, which is automatically included by `gasnet.h`) to access new EX features and capabilities. For details, see [docs/gasnet1_differences.md](docs/gasnet1_differences.md).

Feedback or questions on any matters related to the GASNet-EX project are welcomed at: [gasnet-devel@lbl.gov](mailto:gasnet-devel@lbl.gov)

---

# README file for GASNet

**https://gasnet.lbl.gov**

This is a user manual for GASNet. Anyone planning on using GASNet (either directly or indirectly) should consult this file for usage instructions.

## Other Documentation

- In this README the "docs directory" means either `docs/` in the source directory or `${prefix}/share/doc/gasnet/` in an installation of GASNet.
- For GASNet licensing and usage terms, see [license.txt](license.txt).
- For documentation on a particular GASNet conduit, see the README file in the conduit directory (also installed as `README-<conduit>` in the docs directory).
- For documentation on job spawning mechanisms, see the README file in the corresponding `other/*-spawner` directory (also installed as `README-*-spawner` in the docs directory).
- For documentation on the communication-independent GASNet-tools library, see [README-tools](README-tools).
- Additional information, including the GASNet specification and our bug tracking database, is available from [https://gasnet.lbl.gov](https://gasnet.lbl.gov).
- Anyone planning to modify or add to the GASNet code base should also read the developer documents, available in the GASNet git repository at [https://github.com/BerkeleyLab/gasnet/tree/develop](https://github.com/BerkeleyLab/gasnet/tree/develop):
  - [README-devel](README-devel): GASNet design information and coding standards
  - [README-git](README-git): Rules developers are expected to follow when committing
  - [template-conduit](template-conduit/): A fill-in-the-blanks conduit code skeleton

## Table of Contents

1. [Introduction](#introduction)
2. [System Requirements](#system-requirements)
3. [Building and Installing GASNet](#building-and-installing-gasnet)
4. [Manual Control over Compile and Link Flags](#manual-control-over-compile-and-link-flags)
5. [Basic Usage Information](#basic-usage-information)
6. [Conduit Status](#conduit-status)
7. [Launching/Running GASNet Applications](#launchingrunning-gasnet-applications)
8. [Single-node Development Options](#single-node-development-options)
9. [Supported Platforms](#supported-platforms)
10. [Recognized Environment Variables](#recognized-environment-variables)
11. [GASNet exit](#gasnet-exit)
12. [GASNet Tracing & Statistical Collection](#gasnet-tracing--statistical-collection)
13. [GASNet Collectives](#gasnet-collectives)
14. [GASNet Debug Malloc Services](#gasnet-debug-malloc-services)
15. [GASNet inter-Process SHared Memory (PSHM)](#gasnet-inter-process-shared-memory-pshm)
16. [MPI Interoperability](#mpi-interoperability)
17. [Contact Info and Support](#contact-info-and-support)

---

## Introduction

GASNet is a language-independent, low-level networking layer that provides network-independent, high-performance communication primitives tailored for implementing parallel global address space SPMD languages and libraries such as UPC, UPC++, Co-Array Fortran, Legion, Chapel, and many others. The interface is primarily intended as a compilation target and for use by runtime library writers (as opposed to end users), and the primary goals are high performance, interface portability, and expressiveness. GASNet stands for "Global-Address Space Networking".

The GASNet API is defined in the specification, which is included with this archive in `docs/`, and the definitive version is located on the GASNet webpage:

> [https://gasnet.lbl.gov/](https://gasnet.lbl.gov/)

This README accompanies the GASNet source distribution, which includes implementations of the GASNet API for various popular HPC and general-purpose network hardwares. We use the term **"conduit"** to refer to any complete implementation of the GASNet API which targets a specific network device or lower-level networking layer. A conduit is comprised of any required headers, source files and supporting libraries necessary to provide the functionality of the GASNet API to GASNet clients. This distribution additionally includes a library of communication-independent portability tools called the "GASNet tools", which are used in the conduit implementations and also made available to clients (see [README-tools](README-tools) for details).

## System Requirements

GASNet is extremely portable, and runs on most systems that are relevant to HPC production or development (and many that are not).

The minimum system requirements are:

- A POSIX-like environment, e.g. Linux or another version of Unix.
  - For Mac systems, the free **Xcode command-line tools** from the Apple Store.
  - For Windows systems one needs either of two options:
    - The free **Cygwin** toolkit ([https://www.cygwin.com/](https://www.cygwin.com/))
    - **Windows 10 Subsystem for Linux (WSL)** ([docs](https://docs.microsoft.com/en-us/windows/wsl/))
- **GNU make** (version 3.79 or newer)
- **Perl** (version 5.005 or newer)
- Standard Unix tools: `awk`, `sed`, `env`, `basename`, `dirname`, and a Bourne-compatible shell (e.g. bash)
- A **C compiler** with at least minimal C99 support

We explicitly support most OS's, architectures and compilers in widespread use today. See the [Supported Platforms](#supported-platforms) section for details on systems we've recently validated.

Most distributed-memory GASNet conduits have additional requirements, based on their interactions with network hardware and other implementation details. For example:

- mpi-conduit requires an MPI-1.1 or newer compliant MPI implementation.
- udp-conduit requires POSIX socket libraries and a C++98 or newer compiler.

See each conduit README for additional details on system requirements.

## Building and Installing GASNet

### Step 0 (optional): `./Bootstrap`

Runs the autoconf tools to build a configure script (this can be done on any system and may already have been done for you). If you are keeping a copy of the GASNet sources in your own source control repository, please also see "Source Control and GASNet" in [README-devel](README-devel).

### Step 1: `./configure (options)`

Generate the Makefiles tailored to your system, creating a build tree in the current working directory. You can run configure from a different directory to place your build files somewhere other than inside the source tree (a nice option when maintaining several build trees on the same source tree).

Any compiler flags required for correct operation on your system (e.g. to select the correct ABI) should be included in the values of `CC`, `CXX` and `MPI_CC`. For example to build 32-bit code when your gcc and g++ default to 64-bit:

```sh
configure CC='gcc -m32' CXX='g++ -m32' MPI_CC='mpicc -m32'
```

or similarly, if you want libgasnet to contain debugging symbols:

```sh
configure CC='gcc -g' CXX='g++ -g'   # however also see --enable-debug, below
```

#### Useful configure options

| Option | Description |
|--------|-------------|
| `--help` | Display all available configure options |
| `--prefix=/install/path` | Set the directory where GASNet will be installed |
| `--enable-debug` | Build GASNet in a debugging mode. Turns on C-level debugger options and enables extensive error and sanity checking system-wide, which is highly recommended for developing and debugging GASNet clients (but should **never** be used for performance testing). `--enable-debug` also implies `--enable-{trace,stats,debug-malloc}`, but these can still be selectively `--disable`'d. |
| `--enable-trace` | Turn on GASNet tracing (see [usage info below](#gasnet-tracing--statistical-collection)) |
| `--enable-stats` | Turn on GASNet statistical collection (see [usage info below](#gasnet-tracing--statistical-collection)) |
| `--enable-debug-malloc` | Use GASNet debugging malloc (see [usage info below](#gasnet-debug-malloc-services)) |
| `--enable-segment-{fast,large,everything}` | Select a GASNet segment configuration (see the GASNet spec for more info) |
| `--enable-pshm` | Build GASNet with inter-Process SHared Memory (PSHM) support. See [GASNet PSHM](#gasnet-inter-process-shared-memory-pshm) below. |
| `--with-max-segsize=<val>` | Configure-time default value for `GASNET_MAX_SEGSIZE` |

Configure will detect various interesting features about your system and compilers, including which GASNet conduits are supported.

#### Cross-compilation

For cross-compilation support, look for an appropriate cross configure script in `other/contrib/` and link it into your top-level source directory and invoke it in place of configure. Example for the Cray XC with slurm:

```sh
cd <path-to-gasnet-src>
ln -s other/contrib/cross-configure-cray-xc-slurm .
cd <your-build-dir>
<path-to-gasnet-src>/cross-configure-cray-xc-slurm (configure_options)
```

#### Platform-specific recommendations

**HPE Cray EX (Shasta) systems:**

```sh
--with-cc=cc --with-cxx=CC --with-mpi-cc=cc
```

Additionally, exactly one of the following is recommended for ofi-conduit:
- Slingshot-10 (100Gbps) NICs: `--with-ofi-provider=verbs`
- Slingshot-11 (200Gbps) NICs: `--with-ofi-provider=cxi`
- BOTH NIC types: `--with-ofi-provider=generic`

**Linux clusters with Omni-Path networks** (Intel or Cornelis Networks):

```sh
--disable-ibv --enable-ofi --with-ofi-provider=psm2
```

**Linux InfiniBand clusters with InfiniPath/True Scale HCAs:**

```sh
--enable-mpi --disable-ibv --disable-ofi
```

### Step 2: `make all`

Build the GASNet libraries. A number of other useful makefile targets are available from the top-level:

| Command | Description |
|---------|-------------|
| `make {seq,par,parsync}` | Build the conduit libraries in a given mode |
| `make tests-{seq,par,parsync}` | Build all the GASNet tests in a given mode |
| `make run-tests-{seq,par,parsync}` | Build and run all the GASNet tests in a given mode |
| `make (run-)tests-installed-{seq,par,parsync}` | Use the installed library to build (and run) all the GASNet tests |
| `make run-tests` | Run whatever tests are already built in the conduit directories |
| `make run-tests TESTS="test1 test2..."` | Run specifically-listed tests that are already built |
| `make DO_WHAT="<makefile target>"` | Build selected makefile target in all supported conduit directories |

Each conduit directory also has several useful makefile targets:

| Command | Description |
|---------|-------------|
| `make {seq,par,parsync}` | Build the conduit libraries in a given mode |
| `make tests-{seq,par,parsync}` | Build the conduit tests in a given mode |
| `make testXXX` | Build just testXXX, in SEQ mode |
| `make testXXX-{seq,par,parsync}` | Build just testXXX, in a given mode |
| `make run-tests-{seq,par,parsync}` | Build and run the conduit tests in a given mode |
| `make run-tests TESTS="test1 test2..."` | Run specifically-listed tests in the conduit directory |
| `make run-testexit` | Build a script to run the testexit tester and run it |

Manual compilation and linker flags can be augmented from the command-line:

| Variable | Purpose |
|----------|---------|
| `MANUAL_CFLAGS=...` | Flags to add on the C compile |
| `MANUAL_CXXFLAGS=...` | Flags to add on the C++ compile |
| `MANUAL_MPICFLAGS=...` | Flags to add on the mpicc compile |
| `MANUAL_DEFINES=...` | Flags to add on all compiles |
| `MANUAL_LDFLAGS=...` | Linker flags to add |
| `MANUAL_LIBS=...` | Linker library flags to add |

> **Note:** This feature should be used sparingly, as some flags can invalidate the results of tests performed at configure time. The preferred way to add arbitrary flags is in the `$CC`, `$CXX` and `$MPI_CC` variables passed to configure.

Miscellaneous make variables:

- `make SEPARATE_CC=1` — Build libgasnet using separate C compiler invocations
- `make KEEPTMPS=1` — Keep temporary files generated by the C compiler, if supported

### Step 3 (optional): `make install`

Install GASNet to the directory chosen at configure time. This will create an include directory with a sub-directory for each supported conduit, and a lib directory containing a library file for each supported conduit, as well as any supporting libraries.

GASNet may also be used directly from the build directory, as a convenience to eliminate steps if you are making changes to GASNet or its configuration.

## Manual Control over Compile and Link Flags

As described in the previous section, the recommended mechanism for passing flags to the compilers used by GASNet is to include them in the definition of the compiler variable itself (e.g. `CC='gcc -m64'`). These will be followed on the command line by flags chosen by GASNet's configure script instead of using the standard `CFLAGS` and `CXXFLAGS` make variables.

If there is a need to pass flags that override ones chosen by configure, then one may set `MANUAL_CFLAGS`, `MANUAL_CXXFLAGS` and `MANUAL_MPICFLAGS` on the `make` command line, and these are guaranteed to appear on the compilation command line after the ones chosen by configure. Additionally, `MANUAL_DEFINES` is provided to pass flags to all three compilers.

Note that GASNet does not assume that `MPI_CC` is the same compiler as `CC` (though that is recommended), and therefore invokes it using a distinct `MANUAL_MPICFLAGS`, instead of `MANUAL_CFLAGS`.

All of the `MANUAL_*` make variables described above are honored by the Makefile infrastructure used to build GASNet's libraries and tests. Additionally, the makefile fragments (next section) use these variables when compiling client code.

## Basic Usage Information

See the README for each GASNet conduit implementation for specific usage information, but generally client programs should `#include <gasnetex.h>` (and nothing else), and use the conduit-provided compilation settings.

The best way to get the correct compiler flags for your GASNet client is to `include` the appropriate makefile fragment for the conduit and configuration you want in your Makefile, and use the variables it defines in the Makefile rules for your GASNet client code.

**Example:**

```makefile
include $(gasnet_prefix)/include/mpi-conduit/mpi-seq.mak

.c.o:
	$(GASNET_CC) $(GASNET_CPPFLAGS) $(GASNET_CFLAGS) -c -o $@ $<

.cc.o:
	$(GASNET_CXX) $(GASNET_CXXCPPFLAGS) $(GASNET_CXXFLAGS) -c -o $@ $<

myprog: myprog.o
	$(GASNET_LD) $(GASNET_LDFLAGS) -o $@ $< $(GASNET_LIBS)
```

### Variable breakdown

| Variable | Content |
|----------|---------|
| `GASNET_CFLAGS` | `$(GASNET_OPT_CFLAGS) $(GASNET_MISC_CFLAGS)` |
| `GASNET_CPPFLAGS` | `$(GASNET_MISC_CPPFLAGS) $(GASNET_DEFINES) $(GASNET_INCLUDES)` |
| `GASNET_CXXFLAGS` | `$(GASNET_OPT_CXXFLAGS) $(GASNET_MISC_CXXFLAGS)` |
| `GASNET_CXXCPPFLAGS` | `$(GASNET_MISC_CXXCPPFLAGS) $(GASNET_DEFINES) $(GASNET_INCLUDES)` |

Guidelines:
- `GASNET_[CC,CXX]` contain the configure-detected C and C++ compilers
- `GASNET_INCLUDES` contains only `-I` preprocessor flags
- `GASNET_DEFINES` contains only `-D` or `-U` preprocessor flags
- `GASNET_OPT_*` contain compiler-specific flags controlling the debug/opt level
- `GASNET_MISC_*` contain other needed compiler-specific compile-time flags
- `GASNET_LD` contains the compiler to use for linking
- `GASNET_LIBS` contains `-L` or `-l` link-time flags
- `GASNET_LDFLAGS` contains other needed link-time flags

### Using GASNet with pkg-config

As a convenience, the same variables are also available via the UNIX pkg-config utility. For example:

```sh
pkg-config gasnet-udp-seq --variable=GASNET_CC
```

Complete example using pkg-config in a Makefile:

```makefile
PKG_CONFIG_PATH = $(gasnet_prefix)/lib/pkgconfig
pkg = gasnet-udp-seq

.c.o:
	`pkg-config $(pkg) --variable=GASNET_CC` `pkg-config $(pkg) --cflags` -c -o $@ $<

.cc.o:
	`pkg-config $(pkg) --variable=GASNET_CXX` `pkg-config $(pkg) --variable=GASNET_CXXCPPFLAGS` \
	               `pkg-config $(pkg) --variable=GASNET_CXXFLAGS` -c -o $@ $<

myprog: myprog.o
	`pkg-config $(pkg) --variable=GASNET_LD` -o $@ $< `pkg-config $(pkg) --libs`
```

## Conduit Status

The GASNet distribution includes multiple complete implementations of the GASNet API targeting particular lower-level networking layers. Each of these implementations is called a **conduit**. The corresponding GASNet conduits can be loosely categorized as either **native** or **portable**.

### Portable Conduits

**smp-conduit** — SMP loopback (shared memory)
: The conduit of choice for GASNet operation within a single shared-memory node. Rigorously tested and supported on all current platforms.

**udp-conduit** — UDP/IP (part of the TCP/IP protocol suite)
: The conduit of choice for GASNet over Ethernet, supported on any TCP/IP-compliant network. Rigorously tested over Ethernet and supported on most current platforms.

**mpi-conduit** — MPI (Message Passing Interface)
: A portable implementation of GASNet over MPI-1.1 or later. Intended as a reference implementation for systems lacking native conduit support. Rigorously tested and supported on most current platforms.

**ucx-conduit** — Unified Communication X framework <mark>\[EXPERIMENTAL\]</mark>
: GASNet over the Unified Communication X framework (UCX). This conduit is experimental, and is not yet carefully tuned for performance. It has only been validated on NVIDIA/Mellanox InfiniBand devices starting from ConnectX-5.

**ofi-conduit** — Open Fabrics Interfaces (most providers)
: GASNet over the Open Fabrics Interface framework (libfabric). This conduit is functionally complete but not yet carefully tuned for performance. With the exception of Slingshot and Omni-Path networks (where ofi-conduit is either the best or only option and considered "native"), users are advised to use other conduits compatible with their hardware.

### Native, High-Performance Conduits

**ibv-conduit** — InfiniBand Verbs
: GASNet over the OpenFabrics Verbs API. Rigorously tested and supported over InfiniBand hardware on all supported systems.

**ofi-conduit** — Open Fabrics Interfaces (select providers/networks)
: On **Slingshot** and **Omni-Path** networks, ofi-conduit is either the best or only option available. On only those systems, ofi-conduit is categorized as a native, high-performance conduit.

## Launching/Running GASNet Applications

Often, runtimes or frameworks which are clients of GASNet provide their own utilities for application launch (aka spawning). Users of such utilities should refer to their respective documentation.

| Conduit | Spawning Mechanism | Documentation |
|---------|-------------------|---------------|
| smp | Conduit-specific | `smp-conduit/README` |
| udp | Multiple options | `udp-conduit/README` |
| mpi | MPI-based | `mpi-conduit/README`, `other/mpi-spawner/README` |
| ibv, ofi, ucx | ssh, MPI, or PMI | `*-conduit/README`, `other/ssh-spawner/README`, `other/mpi-spawner/README`, `other/pmi-spawner/README` |

When installed, documents are located in `$prefix/share/doc/GASNet` as `README-[topic]`.

## Single-node Development Options

GASNet supports hardware configurations ranging from HPC supercomputers to individual workstations and laptops. The configure option `--enable-debug` is highly recommended for finding bugs when developing GASNet client code.

### Option 1: smp-conduit
A pure shared-memory implementation — the fastest and easiest option. Works "out of the box" on common laptop, desktop, and workstation environments, including Linux, macOS, Windows with Cygwin, WSL, and Solaris.

### Option 2: udp-conduit
By default a shared-memory implementation indistinguishable from smp-conduit. One can disable shared memory (`--disable-pshm` at configure time or `GASNET_SUPERNODE_MAXSIZE=1` at runtime) for more realistic multi-node behavior testing.

### Option 3: mpi-conduit
If you have MPI installed, everything said for udp-conduit holds, with the addition of shared-memory communication via MPI internals, giving multi-node-like isolation at the GASNet level with shared-memory performance.

## Supported Platforms

Platforms where GASNet and Berkeley UPC have been successfully tested include:

| Platform | Conduits | Notes |
|----------|----------|-------|
| Linux/x86-Ethernet/{gcc,clang}/32 | smp, mpi, udp | |
| Linux/x86-InfiniBand/gcc/32 | smp, mpi, udp, ibv | |
| Linux/x86/IntelC/32 | smp, mpi, udp | |
| Linux/x86/PortlandGroupC/32 | smp, mpi, udp | |
| Linux/x86_64-Ethernet/{gcc,clang,PGI}/{32,64} | smp, mpi, udp, ofi | |
| Linux/x86_64-InfiniBand/{gcc,clang,PathScale,NVHPC,IntelC,Intel oneAPI}/64 | smp, mpi, udp, ibv | |
| Linux/x86_64-Omni-Path/gcc/64 | smp, mpi, udp, ofi | |
| Linux/PowerPC-Ethernet/{gcc,clang}/{32,64} | smp, mpi, udp | |
| Linux/PPC64le/{gcc,clang,pgi,NVHPC,xlc}/64 | smp, mpi, udp, ibv | |
| Linux/MIPS/gcc/{32,n32,64} | smp, udp | # |
| FreeBSD/{x86,amd64}/{gcc,clang}/{32,64} | smp, mpi, udp | |
| OpenBSD/{x86,amd64}/{gcc,clang}/{32,64} | smp, mpi, udp | |
| NetBSD/{x86,amd64}/{gcc,clang}/{32,64} | smp, mpi, udp | |
| Solaris10/SPARC/gcc/{32,64} | smp, udp, mpi | |
| Solaris10/x86/gcc/{32,64} | smp, udp, mpi | |
| MSWindows-Cygwin/{x86,x86_64}/{gcc,clang}/{32,64} | smp, udp, mpi | OpenMPI |
| MSWindows10-Linux(WSL)/{gcc,clang}/64 | smp, udp, mpi | |
| macOS/{x86,x86_64}/{gcc,clang}/{32,64} | smp, udp, mpi | |
| macOS/AARCH64/{gcc,clang}/64 | smp, udp | # |
| CNL/Cray-XT/{gcc,PGI,PathScale,Intel}/64 | smp, mpi | & |
| CNL/HPE-Cray-EX/{gcc,Cray}/64 | smp, mpi, ofi | |
| Linux/AARCH64/{gcc,clang}/64 | smp, udp, mpi | |

> **Legend:**
> - \# = We have not tested MPI but have no reasons to doubt that mpi-conduit would work.
> - & = System has not been tested in recent releases due to lack of access. Reports encouraged.
> - **BETA** = Support for this system is in beta state.

### Supported Compilers

| Compiler | Minimum Version |
|----------|----------------|
| GNU (gcc) | 3.0+ |
| LLVM (clang) | 3.6+ |
| Apple (Xcode) | 7.1+ |
| PGI (pgcc) | 11.0 to 20.4 |
| NVHPC | 20.9+ |
| Intel (icc) | 16+ |
| Intel oneAPI (icx) | 21+ |
| IBM XL (xlc) | 13+ |
| Cray (CCE) | 8.6+ |

## Recognized Environment Variables

In the following descriptions, a **Boolean** setting is one which accepts `1`, `y`, `yes` as TRUE and `0`, `n`, `no` as FALSE (all case-insensitive).

### General Settings

| Variable | Description |
|----------|-------------|
| `GASNET_VERBOSEENV` | Output information about environment variable settings read by the conduit |
| `GASNET_FREEZE` | Pause and wait for a debugger to attach on startup |
| `GASNET_FREEZE_ON_ERROR` | Pause and wait for a debugger on any fatal errors or fatal signals |
| `GASNET_FREEZE_SIGNAL` | Signal name (e.g. `SIGINT`, `SIGUSR1`) to trigger freeze |
| `GASNET_TRACEFILE` | File name to receive trace output (see [Tracing](#gasnet-tracing--statistical-collection)) |
| `GASNET_TRACEMASK` | Types of trace messages to report |
| `GASNET_STATSFILE` | File name to receive statistical output |
| `GASNET_STATSMASK` | Types of statistics to collect and report |
| `GASNET_TRACEFLUSH` | Force file system flush after every trace write |
| `GASNET_TRACELOCAL` | Control whether local (loopback) trace messages are recorded |
| `GASNET_TRACENODES` | Nodes on which to generate tracing output |
| `GASNET_STATSNODES` | Nodes on which to generate statistical output |
| `GASNET_TEST_POLITE_SYNC` | Enable polite-mode synchronization for tests |

### Segment & Memory Settings

| Variable | Description |
|----------|-------------|
| `GASNET_MAX_SEGSIZE` | Upper limit for FAST/LARGE segment size. Format: `size_spec ( / opt_suffix )` where size is an absolute memory size (`[0-9]+{KB,MB,GB}`) or fraction of physical memory (`0.85`). Suffix `P` = per-process, `H` = host-wide. Default: `"0.85/H"` |
| `GASNET_DISABLE_MUNMAP` | Disable mmap-based allocation for satisfying malloc (workaround for firehose bug 495) |
| `GASNET_USE_HUGEPAGES` | Enable/disable use of huge pages. Default: `1` if `HUGETLB_DEFAULT_PAGE_SIZE` is set, `0` otherwise |

### Thread & Process Settings

| Variable | Description |
|----------|-------------|
| `GASNET_MAX_THREADS` | Per-node limit on GASNet client pthreads in PAR/PARSYNC modes |
| `GASNET_SUPERNODE_MAXSIZE` | Maximum number of processes grouped into a shared-memory supernode (0 = no limit) |
| `GASNET_USE_PSHM_SINGLETON` | Control shared memory use in singleton neighborhoods (default: `0`) |
| `GASNET_PSHM_BARRIER_HIER` | Enable/disable hierarchical shared-memory barrier (default: enabled) |

### Barrier Settings

| Variable | Description |
|----------|-------------|
| `GASNET_BARRIER` | Barrier algorithm: `AMDISSEM`, `RDMADISSEM`, `DISSEM` (auto, default), or `AMCENTRAL` (debug) |
| `GASNET_PSHM_BARRIER_RADIX` | Radix for intra-node barrier tree (default: `0` = linear) |
| `GASNET_PSHM_NETWORK_DEPTH` | Depth of intra-node AM network (default: `32`, minimum: `4`) |

### Error Handling & Diagnostics

| Variable | Description |
|----------|-------------|
| `GASNET_CATCH_EXIT` | Set to `0` to prevent GASNet from forcing global job termination on `exit()` |
| `GASNET_NO_CATCH_SIGNAL` | Comma-separated list of signals to exclude from default handling |
| `GASNET_BACKTRACE` | Generate stack backtraces on most fatal errors |
| `GASNET_BACKTRACE_NODES` | Nodes on which to permit backtraces (e.g. `"0,2-4,6"`) |
| `GASNET_BACKTRACE_SIGNAL` | Signal to trigger an immediate backtrace |
| `GASNET_BACKTRACE_TYPE` | Ordered list of backtrace mechanisms to try |
| `GASNET_BACKTRACE_MT` | Enable multi-threaded backtraces |
| `GASNET_FS_SYNC` | Enable `sync()` call at exit time (default: `0`) |

### VIS & Collective Settings

| Variable | Description |
|----------|-------------|
| `GASNET_VIS_AMPIPE` | Enable AM pipelining for non-contiguous put/gets |
| `GASNET_VIS_{PUT,GET}_MAXCHUNK` | Max contiguous chunk size for AM pipelining |
| `GASNET_VIS_MAXCHUNK` | Default for `GASNET_VIS_{PUT,GET}_MAXCHUNK` |
| `GASNET_VIS_REMOTECONTIG` | Enable pack & RDMA for gather puts and scatter gets |
| `GASNET_COLL_SCRATCH_SIZE` | Scratch space size per rank for collectives (default: 2MB) |
| `GASNET_COLL_ENABLE_SEARCH` | Enable autotuning of collectives |
| `GASNET_COLL_TUNING_FILE` | File for collective autotuning data |

### Debug Settings (debug build only)

| Variable | Description |
|----------|-------------|
| `GASNET_SD_INIT` | Initialize buffers at Prepare time with canary (default: `1` in debug) |
| `GASNET_SD_INITVAL` | Initialization pattern (default: `"NAN"`) |
| `GASNET_SD_INITLEN` | Length of canary pattern (default: `128`) |

### Networking & Topology Settings

| Variable | Description |
|----------|-------------|
| `GASNET_HOST_DETECT` | Host identifier: `"gethostid"`, `"hostname"`, or `"conduit"` |
| `GASNET_NODEMAP_EXACT` | Enable exact algorithm for shared-memory node discovery (default: `1`) |
| `GASNET_SPAWN_VERBOSE` | Enable console debugging of job creation/teardown |
| `GASNET_TMPDIR` | Directory for temporary files |

### Exit-Timeout Settings

| Variable | Description |
|----------|-------------|
| `GASNET_EXITTIMEOUT` | Exit-coordination timeout value |
| `GASNET_EXITTIMEOUT_MAX` | Maximum exit timeout |
| `GASNET_EXITTIMEOUT_MIN` | Minimum exit timeout |
| `GASNET_EXITTIMEOUT_FACTOR` | Per-node factor for timeout computation |

Formula: `GASNET_EXITTIMEOUT = min(MAX, MIN + nodes * FACTOR)`

### Thread Stack Settings

| Variable | Description |
|----------|-------------|
| `GASNET_THREAD_STACK_MIN` | Minimum stack size for internal threads |
| `GASNET_THREAD_STACK_PAD` | Padding added to default stack size |

Formula: `stack_size = MAX(MIN, PAD + default)`

### Topology-Aware Environment Variables

Some conduits are capable of applying intelligence to NIC selection based on job and host topology:

- ibv-conduit: `GASNET_IBV_PORTS_TYPE` (default: `"Socket"`)
- ofi-conduit: `GASNET_OFI_DEVICE_TYPE` (default: `"Socket"`)

Valid types name either a GASNet-defined property or hwloc-defined object type:

| Type | Description |
|------|-------------|
| `JRank` | Process's jobrank |
| `HRank` | Process's host-relative rank |
| `NRank` | Process's nbrhd-relative rank |
| `Core` | CPU core(s) to which process is bound |
| `Node` / `NUMANode` | NUMA node(s) to which process is bound |
| `Socket` / `Package` | CPU package(s) to which process is bound |

Each process uses its type value to construct a computed variable name like `[BASE]_0`, `[BASE]_0_2`, etc. Arithmetic operations `/[N]` (division) and `%[N]` (modulo) can be appended after the type.

### HWLOC Query Settings

| Variable | Description |
|----------|-------------|
| `GASNET_HWLOC_QUERY` | `"thread"` (uses `HWLOC_CPUBIND_THREAD`, default) or `"process"` (uses `HWLOC_CPUBIND_PROCESS`) |

See conduit-specific documentation in conduit directories for more settings.

## GASNet exit

GASNet clients desiring robustness and portability should **not** call `_exit()`. Normal process termination should be done using `gasnet_exit()`, `exit()` or return from `main()`. Use of these paths allows GASNet to ensure proper shutdown, including efforts to avoid zombie/orphan processes and proper release of network resources.

## GASNet Tracing & Statistical Collection

GASNet includes an extensive tracing and statistical collection utility. To use:

```sh
configure --enable-stats --enable-trace   # or --enable-debug
```

Then set the environment variables below.

> **Performance note:** System performance is likely to be degraded even when output is disabled. Production builds should not enable tracing/stats at configure time.

### Tracing Environment Variables

| Variable | Description |
|----------|-------------|
| `GASNET_TRACEFILE` | File name to receive trace output (also `"stdout"` or `"stderr"`). `%` replaced by node number at runtime (e.g. `"mytrace-%"`). Unset/empty disables output. |
| `GASNET_TRACENODES` | Optional list of nodes to generate tracing output (e.g. `"0,2-4,6"`). `*` = all. |
| `GASNET_STATSFILE` | File name for statistical output (operates analogously to `GASNET_TRACEFILE`) |
| `GASNET_STATSNODES` | Limit nodes for statistical output (analogous to `GASNET_TRACENODES`) |
| `GASNET_TRACEMASK` | Types of trace messages to report |
| `GASNET_STATSMASK` | Types of statistics to collect and report |
| `GASNET_TRACEFLUSH` | Force file system flush after every write to tracefile |
| `GASNET_TRACELOCAL` | Control recording of local (loopback) put/get operations |

### Trace/Stats Mask Letters

| Letter | Event Type |
|--------|-----------|
| `G` | Gets |
| `P` | Puts |
| `R` | Remote atomics |
| `S` | Non-blocking synchronization |
| `W` | Collective operations (excluding barriers) |
| `B` | Barriers |
| `L` | Locks |
| `A` | AM requests/replies (and handler execution) |
| `X` | AMPoll |
| `I` | Informational messages / performance alerts |
| `O` | Object creation, modification and destruction |
| `C` | Conduit-specific (low-level) messages |
| `D` | Detailed message data for gets/puts/AMreqrep |
| `N` | Line number information from client source files |
| `H` | High-level messages from the client |
| `U` | Unsuppressable messages (always output — use with caution) |

Use `^` as the first character to invert the mask (e.g. `"^X"` enables all except AMPoll).

## GASNet Collectives

GASNet includes interfaces for collective operations. The design for the interfaces is located in `docs/collective_notes.txt`. Anyone planning to implement a client that uses these collectives should contact us first at [gasnet-devel@lbl.gov](mailto:gasnet-devel@lbl.gov).

GASNet introduces a mechanism for auto-tuning of collectives in which a variety of algorithms are tried for each collective operation and the best choice can be recorded in a file for use in future runs. For more information, see the file `autotuner.txt` in the docs directory.

## GASNet Debug Malloc Services

GASNet includes a debug malloc implementation for finding local heap corruption bugs. To use:

```sh
configure --enable-debug-malloc   # or --enable-debug
```

GASNet will regularly scan the heap for corruption at malloc events and AMPoll's and report any detected problems. Client writers can use `gasnett_debug_malloc` functions in `gasnet_tools.h` to tie into these services.

> **Note:** The debug malloc implementation imposes CPU and memory consumption overhead relative to the system malloc implementation.

### Debug Malloc Environment Variables

| Variable | Description |
|----------|-------------|
| `GASNET_MALLOC_INIT` | Initialize every byte of allocated memory to `GASNET_MALLOC_INITVAL` |
| `GASNET_MALLOC_CLOBBER` | Overwrite every byte of freed memory with `GASNET_MALLOC_CLOBBERVAL` |
| `GASNET_MALLOC_INITVAL` | Value for allocation init: decimal, hex (`0x...`), `NAN`, `sNAN`, or `qNAN` (default: `"NAN"`) |
| `GASNET_MALLOC_CLOBBERVAL` | Value for free clobbering (same format, default: `"NAN"`) |
| `GASNET_MALLOC_LEAKALL` | Leak all freed objects (don't re-allocate) |
| `GASNET_MALLOC_SCANFREED` | Scan freed objects for write-after-free errors (implies LEAKALL + CLOBBER) |
| `GASNET_MALLOC_EXTRACHECK` | More frequent corruption checking (costs performance) |
| `GASNET_MALLOCFILE` | File to receive malloc report at process exit (also `"stdout"`, `"stderr"`) |
| `GASNET_MALLOCNODES` | Limit which nodes generate malloc reports |

## GASNet inter-Process SHared Memory (PSHM)

GASNet's PSHM support provides mechanisms to communicate through shared memory among processes on the same compute node. Inter-process communication through shared memory is usually the fastest way for co-located processes to communicate.

PSHM support is **enabled by default** unless cross-compiling or running Cygwin prior to version 2.0. It can be disabled with `--disable-pshm` at configure time. The environment variable `GASNET_SUPERNODE_MAXSIZE` limits the number of processes grouped into a shared-memory "supernode" (set to `1` to disable shared memory at runtime).

### PSHM Mechanisms

The PSHM support can operate via three generic mechanisms:

1. **POSIX shared memory** — Recommended as the best option in most cases
2. **SystemV shared memory** — Next-best option; default on Solaris
3. **mmap()ed disk files** — Fallback; may have significant performance degradation

| Usage | Flags |
|-------|-------|
| OFF | `--disable-pshm` |
| POSIX | no flags required (default except cross-compiling) |
| SYSV | `--disable-pshm-posix --enable-pshm-sysv` |
| FILE | `--disable-pshm-posix --enable-pshm-file` |

Unless PSHM is disabled, configure probes for support via exactly one preferred mechanism (POSIX on most platforms, SystemV on Solaris). If the preferred mechanism is not supported, there is **no automatic fallback** — you must explicitly disable the default and enable the desired mechanism.

#### Platform-Specific Mechanisms

- **Cray XE, XK, XC**: PSHM-over-XPMEM enabled automatically by cross-configure scripts
- **SGI Altix**: `--enable-pshm --disable-pshm-posix --enable-pshm-xpmem`
- **Linux hugetlbfs** (experimental): `--disable-pshm-posix --enable-pshm-hugetlbfs`

### System Settings for POSIX Shared Memory

On most systems, POSIX shared memory allocations reside in a pseudo-filesystem (tmpfs, mfs, etc.). Insufficient space leads to startup failures or SIGBUS/SIGSEGV later.

| Platform | Location | Configuration |
|----------|----------|---------------|
| Linux | `/dev/shm` (tmpfs) | Distribution-specific; may not respect `/etc/fstab` |
| macOS | No settings required | Default is sufficient |
| FreeBSD | `/tmp` (tmpfs) | Sized in `/etc/fstab` via `tmpfs(5)` |
| NetBSD | `/var/shm` (tmpfs) | Sized in `/etc/fstab` via `mount_tmpfs(8)` |
| OpenBSD | `/tmp` (mfs) | Sized in `/etc/fstab` via `mount_mfs(8)` |

### System Settings for SystemV Shared Memory

Controlled by kernel parameters: `shmmax` (max segment size), `shmall` (total allocatable memory in pages), `shmmni` (max number of segments).

Configuration examples for 64-bit systems (add to `/etc/sysctl.conf`):

**Linux:**
```
kernel.shmmax=1099511627776
kernel.shmall=268435456
kernel.shmmni=128
```

**macOS:**
```
kern.sysv.shmmax=1099511627776
kern.sysv.shmall=268435456
kern.sysv.shmmni=128
```

**FreeBSD:**
```
kern.ipc.shmmax=1099511627776
kern.ipc.shmall=268435456
```

For 32-bit systems, use `2147483647` for `shmmax` to avoid potential overflow.

### macOS Warning

There is evidence of a kernel bug in all releases of macOS through at least 10.12 (Sierra) which leads to a small leak of kernel memory each time POSIX or SystemV shared memory is used. In normal usage, reboots for software updates happen frequently enough that this leak will not impact normal users.

### Cleaning PSHM Objects

If a GASNet application using PSHM is terminated before the initialization phase completes, shared memory objects may remain in the system:

| Type | Location | Prefix | Cleanup Command |
|------|----------|--------|----------------|
| POSIX (Linux) | `/dev/shm` | `GASNT` | `rm` |
| POSIX (Cygwin) | `/dev/shm` | `GASNT` | `rm` |
| POSIX (Solaris) | `/tmp` | `.SHMDGASNT` | `rm` |
| POSIX (OpenBSD) | `/tmp` | random `.shm` suffix | `rm` |
| POSIX (NetBSD) | `/var/shm` | `.shmobj_GASNT` | `rm` |
| mmap'd files | `$TMPDIR` or `/tmp` | `GASNT` | `rm` |
| hugetlbfs | hugetlbfs mount (`/dev/hugepages`) | — | `rm` |
| SystemV | System-wide | — | `ipcs` / `ipcrm` |

## MPI Interoperability

GASNet is **compatible** with MPI — both can be used together within the same network and even within the same program. GASNet does NOT use MPI for performing communication (except mpi-conduit), but may use MPI to assist in job creation and termination.

### Initialization Protocol

MPI requires exactly one initialization call before MPI communication can be used. Since GASNet may or may not automatically initialize MPI depending on the conduit and configuration, processes using both must be prepared to handle either situation:

```c
int isMPIinit;
int main(int argc, char **argv) {
  size_t segment_size = 64*1024*1024; /* want 64MB segment */
  size_t segment_max;

  gasnet_init(&argc, &argv);

  segment_max = gasnet_getMaxGlobalSegmentSize();
  if (segment_size > segment_max) segment_size = segment_max;

  gasnet_attach(NULL, 0, segment_size, 0);

  gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
  gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS);

  if (MPI_Initialized(&isMPIinit) != MPI_OK) {
    fprintf(stderr, "Error calling MPI_Initialized()\n");
    abort();
  }
  if (!isMPIinit) MPI_Init(argc, argv); /* MPI not init, so do it */

  /* ... use MPI as usual ... */
  MPI_Barrier(MPI_COMM_WORLD);
  /* ... use GASNet as usual ... */
  gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
  gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS);

  if (!isMPIinit) MPI_Finalize();
  return 0;
}
```

An extensive example is available in `tests/testmpi.c`.

### Time-Phasing Protocol

To prevent deadlock when interleaving MPI and GASNet communication, follow this protocol:

1. When starting, the first MPI or GASNet call from any node puts the application in **MPI mode** or **GASNet mode** respectively.
2. To switch from MPI to GASNet: execute `MPI_Barrier()` as the last MPI call before any GASNet communication.
3. To switch from GASNet to MPI: execute `gasnet_barrier_notify()` + `gasnet_barrier_wait()` as the last GASNet operations before any MPI calls.

### Important Caveats

- GASNet must be configured with `MPI_CC` set to the **exact same** MPI installation used by client code.
- GASNet and MPI both number processes with a unique non-negative index. Every attempt is made to ensure this numbering matches, but clients should use a collective (e.g. AllGather) to build a translation table for portability.
- GASNet language clients using pthreads should ensure they use a **pthread-safe** implementation of MPI.
- See each conduit README for network-specific MPI interoperability discussion.

## Contact Info and Support

For the latest GASNet downloads, publications, and specifications:

> [https://gasnet.lbl.gov](https://gasnet.lbl.gov)

For bug reports and feature requests, please submit a ticket in the GASNet Bugzilla:

> [https://gasnet-bugs.lbl.gov](https://gasnet-bugs.lbl.gov)

**Mailing lists:**

| List | Purpose |
|------|---------|
| [gasnet-users@lbl.gov](mailto:gasnet-users@lbl.gov) | General questions or inquiries regarding installation or use of GASNet |
| [gasnet-devel@lbl.gov](mailto:gasnet-devel@lbl.gov) | GASNet developers list (multi-institution) |
| [gasnet-announce@lbl.gov](mailto:gasnet-announce@lbl.gov) | GASNet release announcements |

---

> The canonical version of this document is located at: [https://github.com/BerkeleyLab/gasnet/blob/main/README](https://github.com/BerkeleyLab/gasnet/blob/main/README)
>
> For more information, please email: [gasnet-users@lbl.gov](mailto:gasnet-users@lbl.gov) or visit the GASNet home page at: [https://gasnet.lbl.gov](https://gasnet.lbl.gov)
