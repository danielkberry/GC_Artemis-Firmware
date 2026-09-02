#include "Home.h"
#include "Home/StatusFace.h"
#include "NetTest/NetTestScreen.h"

// Define to boot straight into the network diagnostics screen (step-3 spike measurements).
#define NET_SPIKE_AUTORUN 0

std::unique_ptr<LVScreen> makeHomeScreen(){
#if NET_SPIKE_AUTORUN
	return std::make_unique<NetTestScreen>();
#else
	return std::make_unique<StatusFace>();
#endif
}
