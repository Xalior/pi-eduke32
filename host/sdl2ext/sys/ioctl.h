//
// sys/ioctl.h — the device-control call, for a board whose C library has no
// device table to control.
//
// EDuke32 bundles ENet, and ENet's single header includes this file before
// any of its code is switched on. No multiplayer is built here — the ENet
// and mmulti translation units are not in the source list at all — so this
// exists to let the header parse. The implementation, in
// host/posix_compat.cpp, fails with ENOTTY.
//
#ifndef _sys_ioctl_h
#define _sys_ioctl_h

#ifdef __cplusplus
extern "C" {
#endif

// The two request numbers ENet's socket code names, at their Linux values,
// because that is the set of names its header expects to find.
#define FIONREAD	0x541B
#define FIONBIO		0x5421

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif
