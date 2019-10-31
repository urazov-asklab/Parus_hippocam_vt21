/*
 *   cc1310_ioctl.h
 */

#include <sys/ioctl.h>

#define	CC1310_MAJOR	153

struct cc1310_ioctl_p
{
	u8	reg;
	u8	len;
	u8	*buf;
};

//Read
#define CC1310_IOCTL_READ			_IOW(CC1310_MAJOR, 0, int)

//Write
#define CC1310_IOCTL_WRITE			_IOW(CC1310_MAJOR, 1, int)

//Read burst(with addr increment)
#define CC1310_IOCTL_READ_BURST		_IOR(CC1310_MAJOR, 2, int)

//Write burst(with addr increment)
#define CC1310_IOCTL_WRITE_BURST 	_IOW(CC1310_MAJOR, 3, int)

//Read status byte
#define CC1310_IOCTL_GET_STATUS		_IOW(CC1310_MAJOR, 4, int)
