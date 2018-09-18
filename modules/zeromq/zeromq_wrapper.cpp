#include "zeromq_wrapper.h"

#define SUBSCRIBERS_EXPECTED 1

context_t context(1);

socket_t syncservice(context, ZMQ_REP);
socket_t syncclient(context, ZMQ_REQ);

socket_t publisher(context, ZMQ_PUB);
socket_t subscriber(context, ZMQ_SUB);
socket_t render(context, ZMQ_SUB);

int sndhwm = 0;

char *String_to_charPtr(String str) {
	CharString tempAscii = str.ascii();
	char *ptr = (char *)tempAscii.get_data();
	return ptr;
}

int Zeromq_wrapper::getNetworkSubPort() {
	return networkSubPort;
}

void Zeromq_wrapper::setNetworkSubPort(int port) {
	networkSubPort = port;
	std::cout << networkSubPort << std::endl;
}

int Zeromq_wrapper::getGuiPubPort() {
	return guiPubPort;
}

void Zeromq_wrapper::setGuiPubPort(int port) {
	guiPubPort = port;
	std::cout << guiPubPort << std::endl;
}

void Zeromq_wrapper::publish(String envelope, String data) {
	// Convert String to std::string
	const char *jsonCharPtr = String_to_charPtr(data);
	const char *envlpCharPtr = String_to_charPtr(envelope);
	std::string envlpString(envlpCharPtr);
	std::string jsonString(jsonCharPtr);
	std::cout <<"Envelope: " << envlpString << " Data: " << jsonString << std::endl;

	// Start Broadcast
	s_sendmore(publisher, envlpString);
	s_send(publisher, jsonString);
}

String Zeromq_wrapper::receive() {
	//std::string res_envelope = s2_recv(subscriber);
	std::string res_data = s2_recv(subscriber);
	if (res_data.empty()) {
		return "";
	} else {
		res_data = s2_recv(subscriber);
		//std::cout << res_data << std::endl;
		return res_data.c_str();
	}	
}

String Zeromq_wrapper::render_call() {
	//std::string res_envelope = s2_recv(subscriber);
	std::string res_data = s2_recv(render);
	if (res_data.empty()) {
		return "";
	} else {
		res_data = s2_recv(render);
		return res_data.c_str();
	}
}

void Zeromq_wrapper::_bind_methods() {
	ClassDB::bind_method(D_METHOD("publish", "envelope", "data"), &Zeromq_wrapper::publish);
	ClassDB::bind_method(D_METHOD("receive"), &Zeromq_wrapper::receive);
	ClassDB::bind_method(D_METHOD("render_call"), &Zeromq_wrapper::render_call);
}

Zeromq_wrapper::Zeromq_wrapper() {
	guiPubPort = 5101;
	networkSubPort = 6101;
	renderSubPort = 7101;

	publisher.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
	publisher.bind("tcp://*:" + std::to_string(guiPubPort));

	subscriber.connect("tcp://localhost:" + std::to_string(networkSubPort));
	subscriber.setsockopt(ZMQ_SUBSCRIBE, "gui_backend", std::strlen("gui_backend"));

	render.connect("tcp://localhost:" + std::to_string(networkSubPort));
	render.setsockopt(ZMQ_SUBSCRIBE, "gui_backend", std::strlen("gui_backend"));

	std::cout << "pub-sub sockets initialized" << std::endl;

	synchronize_publisher(5100);
	synchronize_subscription(6100);
	synchronize_subscription(7100);
}

Zeromq_wrapper::Zeromq_wrapper(int guiPort, int guiSyncPort, int networkPort, int networkSyncPort) {
	guiPubPort = guiPort;
	networkSubPort = networkPort;

	std::cout << "guiPubPort: " << guiPubPort;
	std::cout << " networkSubPort: " << networkSubPort << std::endl;
	std::cout << "guiSyncPort: " << guiSyncPort;
	std::cout << " networkSyncPort: " << networkSyncPort << std::endl;

	publisher.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
	publisher.bind("tcp://*:" + std::to_string(guiPubPort));

	subscriber.connect("tcp://*:" + std::to_string(networkSubPort));
	subscriber.setsockopt(ZMQ_SUBSCRIBE, "gui_backend");

	synchronize_publisher(guiSyncPort);
	synchronize_subscription(networkSyncPort);
}

Zeromq_wrapper::~Zeromq_wrapper() {
	std::cout << "destructor call";
	std::string kill_msg = "{\"token\":\"KILL\",\"url\":\" \"}";
	s_sendmore(publisher, "network_backend");
	s_send(publisher, kill_msg);
	Sleep(1); //  Give 0MQ time to flush output

	syncclient.close();
	syncservice.close();
	publisher.close();
	subscriber.close();
	render.close();

	context.close();
}

void Zeromq_wrapper::synchronize_publisher(int syncPort) {
	std::cout << "syncPort: " << syncPort << std::endl;
	
	syncservice.bind("tcp://*:" + std::to_string(syncPort));

	//  Get synchronization from subscribers
	int subscribers = 0;
	while (subscribers < SUBSCRIBERS_EXPECTED) {

		//  - wait for a sub to start
		s_recv(syncservice);

		//  - send synchronization reply
		//s_send(syncservice, "");

		subscribers++;
	}
	std::cout << "Publisher Synced" << std::endl;
}

void Zeromq_wrapper::synchronize_subscription(int syncPort) {
	std::cout << "syncPort: " << syncPort << std::endl;
	
	syncclient.connect("tcp://localhost:" + std::to_string(syncPort));

	//  - send a synchronization request
	s_send(syncclient, "");

	//  - wait for synchronization reply
	s_recv(syncclient);
	std::cout << "Subscriptions Synced" << std::endl;
}
