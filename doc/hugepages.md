# Backing Seastar memory with huge pages

Seastar's allocator hands out memory from one large, per-shard region. Backing that
region with huge pages - 1GB pages on x86, or 512MB pages on aarch64 with 64kB base
pages - shrinks the TLB footprint of an application from millions of entries to a
handful, which measurably reduces the cost of pointer-chasing workloads.

Seastar cannot allocate huge pages by itself: the pool of huge pages is a
system-wide resource that only root can size, and gigantic pages in particular need
physically contiguous memory, which is easiest to come by early in a machine's
uptime. `perftune.py` therefore does the privileged part, and the application
consumes what it prepared.

## Reserving the pages

Give `perftune.py` the same shard and memory configuration the application will be
started with:

```console
# perftune.py --tune=hugepages --smp 16 --memory 240G --hugepages-user seastar-app
Writing '240' to /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages
Huge pages for prefix 'default' are ready, run the application with --hugepages /run/seastar/hugepages/default
```

perftune.py works out how much memory each shard will get, splits it into as many
of the largest huge pages the kernel offers as fit, then into the next size down,
and so on; it reserves the resulting number of pages on the NUMA node each shard
will run on. Whatever the kernel cannot hand out - gigantic pages are often
unobtainable on a machine that has been up for a while, even after the compaction
perftune.py asks for - is retried in the next smaller size, so the application still
gets the best pages the machine can manage.

The pages are exposed as one hugetlbfs mount per page size, owned by
`--hugepages-user` (the user that will run the application):

```console
$ ls /run/seastar/hugepages/default
1048576kB  2048kB
```

If the pages must be guaranteed, reserve them on the kernel command line instead
(`hugepagesz=1G hugepages=240`) and let perftune.py mount and hand them out.

## Running the application

Point the application at the directory holding the mounts:

```console
$ seastar-app --smp 16 --memory 240G --hugepages /run/seastar/hugepages/default
```

Each shard is backed by the largest pages available, using smaller ones for the
remainder and ordinary anonymous memory if the pools run dry (in which case a
warning is logged). `--hugepages` also still accepts a single hugetlbfs mount, which
is how it behaved before per-page-size mounts existed.

Note that memory in huge pages cannot be `mprotect()`ed at a finer granularity than
the huge page size, so Seastar's thread stack guards (enabled in the `debug` and
`dev` build modes) are silently skipped for stacks that land in huge pages.

## Several applications on one machine

Each application gets its own `--hugepages-prefix`, which names both its mount
directory and the record perftune.py keeps of what it asked for:

```console
# perftune.py --tune=hugepages --hugepages-prefix db  --smp 16 --memory 120G --hugepages-user db
# perftune.py --tune=hugepages --hugepages-prefix web --smp 8  --memory 60G  --hugepages-user web
```

The size of each huge page pool is the sum of what all prefixes asked for, so tuning
one application does not take pages away from another. The mounts carry a `min_size`
option, which keeps a prefix's share reserved for it even while its application is
not running.

Note that this makes perftune.py the owner of the huge page pools for the sizes it
uses: huge pages reserved by other means may be released.

Use `--release-hugepages` to give a prefix's pages back, and `--get-hugepages-path`
to print the directory to pass to `--hugepages`:

```console
# perftune.py --tune=hugepages --hugepages-prefix web --release-hugepages
$ perftune.py --tune=hugepages --hugepages-prefix db --get-hugepages-path
/run/seastar/hugepages/db
```

The reservations live in `/run`, so they do not survive a reboot; run perftune.py
again after one, ideally from the same unit that starts the application.
