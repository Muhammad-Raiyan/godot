/* zeromq_wrapper.h */
#ifndef ZEROMQ_WRAPPER_H
#define ZEROMQ_WRAPPER_H

#include "reference.h"
#include <string>
#include <iostream>
#include "zhelpers.hpp"

using namespace zmq;

class Zeromq_wrapper : public Reference {
	GDCLASS(Zeromq_wrapper, Reference);

	int guiPubPort;
	int networkSubPort;
	int tabId;
	void synchronize_publisher(int syncPort);
	void synchronize_subscription(int syncPort);
	
protected:
	static void _bind_methods();

public:
	Zeromq_wrapper();
	Zeromq_wrapper(int guiPort, int guiSyncPort, int networkPort, int networkSyncPort);
	~Zeromq_wrapper();

	int getNetworkSubPort();
	void setNetworkSubPort(int port);
	int getGuiPubPort();
	void setGuiPubPort(int port);

	String searchRequest(String data_to_search);
	void publish(String envelope, String data);
	String receive();
};

#endif
