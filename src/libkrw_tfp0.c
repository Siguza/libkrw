#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <mach/mach.h>
#include "libkrw.h"
#include "libkrw_plugin.h"

extern kern_return_t mach_vm_read_overwrite(task_t task, mach_vm_address_t addr, mach_vm_size_t size, mach_vm_address_t data, mach_vm_size_t *outsize);
extern kern_return_t mach_vm_write(task_t task, mach_vm_address_t addr, mach_vm_address_t data, mach_msg_type_number_t dataCnt);
extern kern_return_t mach_vm_allocate(task_t task, mach_vm_address_t *addr, mach_vm_size_t size, int flags);
extern kern_return_t mach_vm_deallocate(task_t task, mach_vm_address_t addr, mach_vm_size_t size);

static task_t gKernelTask = MACH_PORT_NULL;

__attribute__((destructor)) static void unload(void)
{
    if(gKernelTask != MACH_PORT_NULL)
    {
        (void)mach_port_deallocate(mach_task_self(), gKernelTask);
        gKernelTask = MACH_PORT_NULL;
    }
}

static int assure_ktask(void)
{
    if(gKernelTask != MACH_PORT_NULL)
    {
        return 0;
    }

    // hsp4
    task_t port = MACH_PORT_NULL;
    host_t host = mach_host_self();
    kern_return_t ret = host_get_special_port(host, HOST_LOCAL_NODE, 4, &port);
    if(ret == KERN_SUCCESS)
    {
        if(MACH_PORT_VALID(port))
        {
            gKernelTask = port;
            return 0;
        }
    }
    else if(ret == KERN_INVALID_ARGUMENT)
    {
        return EPERM;
    }
    else
    {
        return EDEVERR;
    }

    // tfp0
    port = MACH_PORT_NULL;
    ret = task_for_pid(mach_task_self(), 0, &port);
    if(ret == KERN_SUCCESS)
    {
        if(MACH_PORT_VALID(port))
        {
            gKernelTask = port;
            return 0;
        }
        return EDEVERR;
    }
    // This is ugly, but task_for_pid really doesn't tell us what's wrong,
    // so the best we can do is guess? :/
    return EPERM;
}

static int tfp0_kbase(uint64_t *addr)
{
    int r = assure_ktask();
    if(r != 0)
    {
        return r;
    }

    task_dyld_info_data_t info = {};
    uint32_t count = TASK_DYLD_INFO_COUNT;
    kern_return_t ret = task_info(gKernelTask, TASK_DYLD_INFO, (task_info_t)&info, &count);
    if(ret != KERN_SUCCESS)
    {
        return EDEVERR;
    }
    // Backwards-compat for jailbreaks that didn't set this
    if(info.all_image_info_addr == 0 && info.all_image_info_size == 0)
    {
        return ENOTSUP;
    }
    *addr = 0xfffffff007004000 + info.all_image_info_size;
    return 0;
}

static int tfp0_kread(uint64_t from, void *to, size_t len)
{
    // Overflow
    if(from + len < from || (mach_vm_address_t)to + len < (mach_vm_address_t)to)
    {
        return EINVAL;
    }

    int r = assure_ktask();
    if(r != 0)
    {
        return r;
    }

    mach_vm_address_t dst = (mach_vm_address_t)to;
    for(mach_vm_size_t chunk = 0; len > 0; len -= chunk)
    {
        chunk = len > 0xff0 ? 0xff0 : len;
        kern_return_t ret = mach_vm_read_overwrite(gKernelTask, from, chunk, dst, &chunk);
        if(ret == KERN_INVALID_ARGUMENT || ret == KERN_INVALID_ADDRESS)
        {
            return EINVAL;
        }
        if(ret != KERN_SUCCESS || chunk == 0)
        {
            // Check whether we read any bytes at all
            return dst == (mach_vm_address_t)to ? EDEVERR : EIO;
        }
        from += chunk;
        dst  += chunk;
    }
    return 0;
}

static int tfp0_kwrite(void *from, uint64_t to, size_t len)
{
    // Overflow
    if((mach_vm_address_t)from + len < (mach_vm_address_t)from || to + len < to)
    {
        return EINVAL;
    }

    int r = assure_ktask();
    if(r != 0)
    {
        return r;
    }

    mach_vm_address_t src = (mach_vm_address_t)from;
    for(mach_vm_size_t chunk = 0; len > 0; len -= chunk)
    {
        chunk = len > 0xff0 ? 0xff0 : len;
        kern_return_t ret = mach_vm_write(gKernelTask, to, src, chunk);
        if(ret == KERN_INVALID_ARGUMENT || ret == KERN_INVALID_ADDRESS)
        {
            return EINVAL;
        }
        if(ret != KERN_SUCCESS)
        {
            // Check whether we wrote any bytes at all
            return src == (mach_vm_address_t)from ? EDEVERR : EIO;
        }
        src += chunk;
        to  += chunk;
    }
    return 0;
}

static int tfp0_kmalloc(uint64_t *addr, size_t size)
{
    int r = assure_ktask();
    if(r != 0)
    {
        return r;
    }

    mach_vm_address_t va = 0;
    kern_return_t ret = mach_vm_allocate(gKernelTask, &va, size, VM_FLAGS_ANYWHERE);
    if(ret == KERN_SUCCESS)
    {
        *addr = va;
        return 0;
    }
    if(ret == KERN_INVALID_ARGUMENT)
    {
        return EINVAL;
    }
    if(ret == KERN_NO_SPACE || ret == KERN_RESOURCE_SHORTAGE)
    {
        return ENOMEM;
    }
    return EDEVERR;
}

static int tfp0_kdealloc(uint64_t addr, size_t size)
{
    int r = assure_ktask();
    if(r != 0)
    {
        return r;
    }

    kern_return_t ret = mach_vm_deallocate(gKernelTask, addr, size);
    if(ret == KERN_SUCCESS)
    {
        return 0;
    }
    if(ret == KERN_INVALID_ARGUMENT)
    {
        return EINVAL;
    }
    return EDEVERR;
}

__attribute__((visibility("hidden")))
int libkrw_initialization(krw_handlers_t handlers) {
    // Make sure structure version is not lower than what we compiled with
    if (handlers->version < LIBKRW_HANDLERS_VERSION) {
        return EPROTONOSUPPORT;
    }
    // Set the version in the struct that libkrw will read to the version we compled as
    // so that it can test if needed
    handlers->version = LIBKRW_HANDLERS_VERSION;
    int r = assure_ktask();
    if (r != 0)
    {
        return r;
    }
    handlers->kbase = &tfp0_kbase;
    handlers->kread = &tfp0_kread;
    handlers->kwrite = &tfp0_kwrite;
    handlers->kmalloc = &tfp0_kmalloc;
    handlers->kdealloc = &tfp0_kdealloc;
    return 0;
}
