# libkrw

An attempt at standardising an iOS kernel r/w API.

### Background

Under Mach/Darwin/XNU, task ports are an API that, among other things, let you read and write memory of other processes. The idea is that every process has such a task port, even the kernel - referred to as tfp0 (from the `task_for_pid(0)` call that would historically yield that port).  
On macOS this port always been available to root processes in some form, whereas on iOS you were never able to obtain it. But since iOS jailbreaks needed to gain kernel r/w anyway, most of them would use that to defeat this restriction and set up a well-known interface by which root processes could once more obtain tfp0.

Starting with iOS 10.3, Apple began combatting this from a different angle: rather than prevent the lookup or creation of a task port pointing at the kernel's process struct, they would make the interface detect these capabilities and refuse to operate on them. For a long time, bypassing those checks were as simple as swapping out some pointers or creating a second virtual mapping for the same physical memory, but with iOS 14 it seems Apple's had enough: they are now going so far as to panic the kernel if they detect use of a task port whose `task->map->pmap == kernel_pmap`. This is non-trivial to bypass on A12 and later due to [PPL](http://newosxbook.com/articles/CasaDePPL.html).

On [checkra1n](https://checkra.in) we of course patched this back into place as if nothing was amiss, but other jailbreaks don't have this luxury.  
Given that any bypass is likely to get patched once it's used publicly, I think the time has come to abstract away the internal workings or kernel r/w and export a new API.

### The role of libkrw

Libkrw serves two purposes:

- Defining a common API (via header file and .tbd linkable)
- Providing a default drop-in implementation around tfp0

This means that:

- On any existing jailbreak that supports tfp0, you should be able to install this library and things should just work.
- On any jailbreak going forward, the jailbreak author(s) would ship their own version of libkrw with a custom implementation, should they choose to conform to this API.

### Structure

See [`include/libkrw.h`](https://github.com/Siguza/libkrw/blob/master/include/libkrw.h) for function declarations and API specification.

##### For building against libkrw:

1. Copy [`include/libkrw.h`](https://github.com/Siguza/libkrw/blob/master/include/libkrw.h) and [`libkrw.tbd`](https://github.com/Siguza/libkrw/blob/master/libkrw.tbd) to your project.
2. Compile with `-I. -L. -lkrw`.
3. Don't forget the `task_for_pid-allow` entitlement.
4. If you're building a deb file, add this to your `control`:  
   ```
   Depends: libkrw (>= 1.0.0)
   ```

##### For writing your own implementation of libkrw:

Starting with version 1.1.0, libkrw supports a plugin interface so you no longer need to create a package to replace it.

1. See [`include/libkrw_plugin.h`](https://github.com/Siguza/libkrw/blob/master/include/libkrw_plugin.h) for documentation.
2. Implement and export either a function called `krw_initializer` or `kcall_initializer` that takes a `krw_handlers_t` argument.
3. Install all handlers that you support.
4. Compile with `-Wl,-bundle`.
5. Name it `/usr/lib/libkrw/[name].dylib`.
6. Add this to the `control` of your deb file:  
   ```
   Depends: libkrw
   ```

##### For using the default implementation:

The default implementation resides in [`src/libkrw_tfp0.c`](https://github.com/Siguza/libkrw/blob/master/src/libkrw_tfp0.c).  
It is automatically selected as a fallback when no plugins are present.  
If you want to build it yourself, you can do so with:

    make all    # builds the dylib and tbd
    make deb    # builds the deb packages

The binary release is available from `apt.bingner.com`.  
But you're free to rebuild and host this library wherever you please.

### License

[MIT](https://github.com/Siguza/libkrw/blob/master/LICENSE).
