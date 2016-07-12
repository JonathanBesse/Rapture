#include "sys_local.h"
#ifdef _WIN32
#include <WinSock2.h>
#include <Ws2TcpIp.h>
#endif

/* Non-static member functions */

// Creates a new socket object with family and type
// AF_UNSPEC should be used by default
Socket::Socket(int af_, int type_) {
	af = af_;
	type = type_;
	memset(&addressInfo, 0, sizeof(addressInfo));
	internalSocket = socket(af, type, IPPROTO_TCP);
	if (internalSocket < 0 && af == AF_INET6) {
		// retry using IPv4
		af = AF_INET;
		internalSocket = socket(af, type, IPPROTO_TCP);
	}

	// Set some extra options
	int yes = 1;
	int no = 0;
	setsockopt(internalSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
	setsockopt(internalSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof(yes));

	// If it's IPv6 (or unspecified) we should allow IPv4 connections as well
	if (af == AF_INET6) {
		setsockopt(internalSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&no, sizeof(no));
	}
}

// Creates a new socket object from another socket object (copy constructor)
Socket::Socket(Socket& other) {
	af = other.af;
	type = other.type;
	internalSocket = other.internalSocket;
	memcpy(&addressInfo, &other.addressInfo, sizeof(addressInfo));
}

// Creates a new socket from an internal socket and some information on the address.
Socket::Socket(addrinfo& connectingClientInfo, socket_t socket) {
	addressInfo = connectingClientInfo;
	af = connectingClientInfo.ai_family;
	type = connectingClientInfo.ai_socktype;
	internalSocket = socket;

	SetNonBlocking();

	// Set some extra options
	int yes = 1;
	int no = 0;
	setsockopt(internalSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
	setsockopt(internalSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof(yes));

	if (af == AF_INET6) {
		setsockopt(internalSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&no, sizeof(no));
	}
}

Socket::~Socket() {
	Disconnect();
	closesocket(internalSocket);
}

bool Socket::SetNonBlocking() {
	unsigned long ulMode = 1;

	// Enable non-blocking recv and write
	ioctlsocket(internalSocket, FIONBIO, &ulMode);
	if (ulMode != 1) {
		R_Message(PRIORITY_ERROR, "Couldn't establish non-blocking socket\n");
		return false;
	}

	// Also set it as a broadcast socket

	return true;
}

// Binds the socket and starts listening
bool Socket::StartListening(unsigned short port, uint32_t backlog) {
	addrinfo hints;
	addrinfo* value;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = af;
	hints.ai_socktype = type;
	hints.ai_flags = AI_PASSIVE;

	char szPort[16] = { 0 };
	sprintf(szPort, "%i", port);

	getaddrinfo(nullptr, szPort, &hints, &value);
	addressInfo = *value;
	int code = ::bind(internalSocket, value->ai_addr, value->ai_addrlen);
	if (code != 0) {
		R_Message(PRIORITY_ERROR, "Failed to bind socket on port %s (code %i)\n", szPort, code);
		return false;
	}

	if (!SetNonBlocking()) {
		return false;
	}

	listen(internalSocket, backlog);

	return true;
}

// Check for any pending connections on this line. Returns a newly created socket if we found a connection.
Socket* Socket::CheckPendingConnections() {
	sockaddr_storage clientInfo;
	int sockSize = sizeof(clientInfo);
	sockaddr* genericClientInfo = (sockaddr*)&clientInfo;
	socket_t value = accept(internalSocket, genericClientInfo, &sockSize);
	if (value < 0) {
		// No connection found
		return nullptr;
	}

	// Found a connection, fill out some preliminary data about the client that connected
	addrinfo connectingInfo = { 0 };
	connectingInfo.ai_family = clientInfo.ss_family;
	connectingInfo.ai_protocol = IPPROTO_TCP;
	connectingInfo.ai_socktype = type;
	switch (connectingInfo.ai_family) {
	case AF_INET:	// IPv4
	{
		sockaddr_in* sockIn = (sockaddr_in*)genericClientInfo;
		connectingInfo.ai_addrlen = sizeof(sockaddr_in);
		connectingInfo.ai_addr = (sockaddr*)Zone::Alloc(connectingInfo.ai_addrlen, "network");
		memcpy(connectingInfo.ai_addr, sockIn, connectingInfo.ai_addrlen);
	}
	break;
	case AF_INET6:	// IPv6
	{
		sockaddr_in6* sockIn = (sockaddr_in6*)genericClientInfo;
		connectingInfo.ai_addrlen = sizeof(sockaddr_in6);
		connectingInfo.ai_addr = (sockaddr*)Zone::Alloc(connectingInfo.ai_addrlen, "network");
		memcpy(connectingInfo.ai_addr, sockIn, connectingInfo.ai_addrlen);
	}
	break;
	}

	return new Socket(connectingInfo, value);
}

// Sends an entire chunk of data across the network, without fragmentation
bool Socket::SendEntireData(void* data, size_t dataSize) {
	size_t sent = 0;
	while (sent < dataSize) {
		int numSent = send(internalSocket, (const char*)data + sent, dataSize - sent, 0);
		if (numSent < 0) {
			return false;
		}
		sent += numSent;
	}
	return true;
}

// Send a packet across the network.
// Guaranteed delivery (no fragmentation), does not block.
bool Socket::SendPacket(Packet& outgoing) {
	SendEntireData(&outgoing.packetHead, sizeof(outgoing.packetHead));
	SendEntireData(&outgoing.packetData, outgoing.packetHead.packetSize);
	bool sent = SendEntireData(&outgoing.packetHead, sizeof(outgoing.packetHead));
	if (!sent) {
		R_Message(PRIORITY_WARNING,
			"Failed to send packet header (packet type: %i; clientNum: %i)\n",
			outgoing.packetHead.type, outgoing.packetHead.clientNum);
		return false;
	}
	sent = SendEntireData(outgoing.packetData, outgoing.packetHead.packetSize);
	if (!sent) {
		R_Message(PRIORITY_WARNING,
			"Failed to send packet data (packet type: %i; clientNum: %i\n",
			outgoing.packetHead.type, outgoing.packetHead.clientNum);
		return false;
	}
	return true;
}

// Read an entire block of memory from a socket, without fragmentation.
bool Socket::ReadEntireData(void* data, size_t dataSize) {
	size_t received = 0;
	while (received < dataSize) {
		int numRead = recv(internalSocket, (char*)data + received, dataSize - received, 0);
		if (numRead < 0) {
			return false;
		}
		received += numRead;
	}
	return true;
}

// Read a packet from a socket.
// Guaranteed delivery (no fragmentation), may block.
bool Socket::ReadPacket(Packet& incomingPacket) {
	memset(&incomingPacket, 0, sizeof(incomingPacket));

	if (!ReadEntireData(&incomingPacket.packetHead, sizeof(incomingPacket.packetHead))) {
		// TODO: make it rain fire from the sky, we dropped a packet
		return false;
	}

	incomingPacket.packetData = Zone::Alloc(incomingPacket.packetHead.packetSize, "network");
	if (!ReadEntireData(&incomingPacket.packetData, incomingPacket.packetHead.packetSize)) {
		Zone::FastFree(incomingPacket.packetData, "network");
		return false;
	}

	return true;
}

// Read all packets from a socket
// Gauranteed delivery (no fragmentation), does not block.
void Socket::ReadAllPackets(vector<Packet>& vOutPackets) {
	bool bReading, bWriting;
	do {
		Packet readPacket;
		if (!ReadPacket(readPacket)) {
			break;
		}
		vOutPackets.push_back(readPacket);
		Socket::SelectSingle(this, bReading, bWriting);
	} while (bReading);
}

// Connect this socket to a hostname and port.
// Not necessary unless the af/type constructor or the copy constructor are used.
// Also establishes this socket as being nonblocking.
bool Socket::Connect(const char* hostname, unsigned short port) {
	// First we need to resolve the hostname before we can bind the socket
	addrinfo hints;
	addrinfo* results;
	memset(&hints, 0, sizeof(hints));
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_socktype = type;
	hints.ai_family = af;

	char szPort[16] = { 0 };
	sprintf(szPort, "%i", port);

	int dwReturn = getaddrinfo(hostname, szPort, &hints, &results);
	if (dwReturn != 0) {
		R_Message(PRIORITY_ERROR, "Could not resolve hostname %s (reason: %s)\n", hostname, gai_strerror(dwReturn));
		return false;
	}
	memcpy(&this->addressInfo, results, sizeof(addrinfo));
	
	char ipBuffer[128];
	switch (results->ai_family) {
		case AF_INET:	// IPv4
		{
			sockaddr_in* address = (sockaddr_in*)results->ai_addr;
			const char* ipStr = inet_ntop(results->ai_family, &address->sin_addr, ipBuffer, sizeof(ipBuffer));

			R_Message(PRIORITY_MESSAGE, "%s resolved to %s\n", hostname, ipStr);

			int connectCode = connect(internalSocket, (sockaddr*)address, sizeof(*address));
			if (connectCode != 0) {
				
				R_Message(PRIORITY_ERROR, "Could not connect to %s (code %i)\n", hostname, connectCode);
				return false;
			}
		}
		break;
		case AF_INET6:	// IPv6
		{
			sockaddr_in6* address = (sockaddr_in6*)results->ai_addr;
			const char* ipStr = inet_ntop(results->ai_family, &address->sin6_addr, ipBuffer, sizeof(ipBuffer));

			R_Message(PRIORITY_MESSAGE, "%s resolved to %s\n", hostname, ipStr);

			int connectCode = connect(internalSocket, (sockaddr*)address, sizeof(*address));
			if (connectCode != 0) {
				R_Message(PRIORITY_ERROR, "Could not connect to %s (code %i)\n", hostname, connectCode);
				return false;
			}
		}
		break;
		default:
			R_Message(PRIORITY_WARNING, "Unidentified socket family %i\n", results->ai_family);
			return false;
	}

	if (!SetNonBlocking()) {
		return false;
	}

	return true;
}

// Disconnects a socket.
// Called automatically on destroyed.
void Socket::Disconnect() {
	shutdown(internalSocket, SD_SEND);
}

/* Static member functions */

void Socket::Select(vector<Socket*> vSockets, vector<Socket*>& vReadable, vector<Socket*>& vWriteable) {
	vReadable.clear();
	vWriteable.clear();

	fd_set readSet{ 0 };
	fd_set writeSet{ 0 };
	timeval timeout{ 0, 1 };
	for (auto it = vSockets.begin(); it != vSockets.end(); ++it) {
		Socket* sock = *it;
		if (sock == nullptr) {
			return;
		}
		FD_SET(sock->internalSocket, &readSet);
		FD_SET(sock->internalSocket, &writeSet);
	}

	select(vSockets.size(), &readSet, &writeSet, nullptr, &timeout);
	
	// add to vReadable
	for (int i = 0; i < readSet.fd_count; i++) {
		socket_t sock = readSet.fd_array[i];
		for (auto it = vSockets.begin(); it != vSockets.end(); ++it) {
			Socket* socket = *it;
			if (socket->internalSocket == sock) {
				vReadable.push_back(socket);
				break;
			}
		}
	}

	// add to vWriteable
	for (int i = 0; i < writeSet.fd_count; i++) {
		socket_t sock = writeSet.fd_array[i];
		for (auto it = vSockets.begin(); it != vSockets.end(); ++it) {
			Socket* socket = *it;
			if (socket->internalSocket == sock) {
				vWriteable.push_back(socket);
			}
		}
	}
}

void Socket::SelectSingle(Socket* pSocket, bool& bReadable, bool& bWriteable) {
	fd_set readSet{ 0 };
	fd_set writeSet{ 0 };

	if (pSocket == nullptr) {
		return;
	}
	
	FD_SET(pSocket->internalSocket, &readSet);
	FD_SET(pSocket->internalSocket, &writeSet);

	timeval timeout{ 0, 1 };
	select(1, &readSet, &writeSet, nullptr, &timeout);

	if (FD_ISSET(pSocket->internalSocket, &readSet)) {
		bReadable = true;
	}
	else {
		bReadable = false;
	}

	if (FD_ISSET(pSocket->internalSocket, &writeSet)) {
		bWriteable = false;
	}
	else {
		bWriteable = false;
	}
}