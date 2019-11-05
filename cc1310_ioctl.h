/*
 *   cc1310_ioctl.h
 */

#include <sys/ioctl.h>

#define	CC1310_MAJOR	153

struct cc1310_ioctl_p
{
	u8	len;
	u8	*buf;
};

//Read
#define CC1310_IOCTL_READ			_IOW(CC1310_MAJOR, 0, int)

//Write
#define CC1310_IOCTL_WRITE			_IOW(CC1310_MAJOR, 1, int)


#define CC1310_IOCTL_GET_STATUS		_IOW(CC1310_MAJOR, 2, int)