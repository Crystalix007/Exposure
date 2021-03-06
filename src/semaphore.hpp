#pragma once

// This is a utility header file written because glibc semaphores were not
// cooperating. Not all threads waiting on a semaphore were woken up when the
// semaphore was released repeatedly, resulting in very poor utilisation once
// jobs ran out.

#include <semaphore.h>

class POSIXSemaphore {
public:
	POSIXSemaphore(unsigned int initialValue);
	virtual ~POSIXSemaphore();

	void acquire();
	void release(unsigned int diff = 1);

protected:
	sem_t semaphore;
};

namespace std {
	using binary_semaphore = POSIXSemaphore;
} // namespace std
