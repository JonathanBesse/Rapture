#include "sys_local.h"

#define RAPTURE_DEFAULT_PORT		1750
#define RAPTURE_DEFAULT_BACKLOG		32
#define RAPTURE_DEFAULT_MAXCLIENTS	8

#define RAPTURE_NETPROTOCOL			0

namespace Network {
	using namespace ClientPacket;

	enum Netmode_e {
		Netmode_Red,		// No connections allowed on the server, and any current connections are terminated
		Netmode_Yellow,		// No new connections allowed, current connections aren't terminated
		Netmode_Green,		// Anything goes
	};

	enum Netstate_e {
		Netstate_NoConnect,		// Not connected to any network, not listening
		Netstate_Listen,		// Listening
		Netstate_NeedAuth,		// Connected to a server, need authorization packet
		Netstate_Authorized,	// Connected to a server and authorized
	};

	static Cvar*				net_port = nullptr;
	static Cvar*				net_serverbacklog = nullptr;
	static Cvar*				net_maxclients = nullptr;
	static Cvar*				net_netmode = nullptr;
	static Cvar*				net_timeout = nullptr;
	static Cvar*				net_ipv6 = nullptr;

	static Socket*				localSocket = nullptr;
	static map<int, Socket*>	mOtherConnectedClients;
	static Socket*				remoteSocket = nullptr;
	static vector<Packet>		vPacketsAwaitingSend;	// Goes in both directions
	static Netstate_e			currentNetState = Netstate_NoConnect;
	static vector<Socket*>		vTemporaryConnections;

	static int			numConnectedClients = 1;
	static int			myClientNum = 0;				// Client 0 is always the host
	static int			lastFreeClientNum = 1;

	static networkCallbackFunction	callbacks[NIC_MAX] {nullptr};

	void Netmode_Callback(int newValue) {
		/*if (newValue == Netmode_Red) {
			mOtherConnectedClients.clear();
			numConnectedClients = 1;
		}*/
	}

	// Initialize the network
	void Init() {
		Zone::NewTag("network");
		net_port = Cvar::Get<int>("net_port", "Port used for networking (TCP)", (1 << CVAR_ARCHIVE), RAPTURE_DEFAULT_PORT);
		net_serverbacklog
			= Cvar::Get<int>("net_serverbacklog", "Maximum number of waiting connections for the server", (1 << CVAR_ARCHIVE), RAPTURE_DEFAULT_BACKLOG);
		net_maxclients = Cvar::Get<int>("net_maxclients", "Maximum number of clients allowed on server", (1 << CVAR_ROM) | (1 << CVAR_ARCHIVE), RAPTURE_DEFAULT_MAXCLIENTS);
		net_netmode = Cvar::Get<int>("net_netmode", "Current netmode", 0, Netmode_Red);
		net_timeout = Cvar::Get<int>("net_timeout", "Timeout duration, in milliseconds", 0, 90000);
		net_ipv6 = Cvar::Get<bool>("net_ipv6", "Whether to use IPv6 addresses", (1 << CVAR_ARCHIVE), false);

		net_netmode->AddCallback(Netmode_Callback);
		Sys_InitSockets();

		// Create a local socket so we can serve as the host later
		localSocket = new Socket(net_ipv6->Bool() ? AF_INET6 : AF_INET, SOCK_STREAM);
	}

	// Shut down the network, delete the local socket, disconnect any clients, etc.
	void Shutdown() {
		delete localSocket;

		Sys_ExitSockets();
	}

	// Drops a client.
	// Returns the next client iterator.
	void DropClient(int clientNum) {
		// TODO: gamecode for dropped client
		R_Message(PRIORITY_MESSAGE, "DropClient: %i\n", clientNum);
		auto it = mOtherConnectedClients.find(clientNum);
		if (it == mOtherConnectedClients.end()) {
			R_Message(PRIORITY_WARNING, "Tried to drop invalid client %i\n", clientNum);
			return;
		}
		if (it->second != nullptr) {
			delete it->second;
			it->second = nullptr;
		}
		numConnectedClients--;
	}

	// Dispatches a single packet that's been sent to us from the server
	void DispatchSingleServerPacket(Packet& packet) {
		switch (packet.packetHead.type) {
			case PACKET_CLIENTACCEPT:
				{
					// We have been accepted into the server
					ClientAcceptPacket* cPacket = (ClientAcceptPacket*)(packet.packetData);
					R_Message(PRIORITY_MESSAGE, "Authorization successful");
					myClientNum = cPacket->clientNum;
					currentNetState = Netstate_Authorized;
				}
				break;
			case PACKET_CLIENTDENIED:
				{
					// We have been denied from the server
					ClientDeniedPacket* cPacket = (ClientDeniedPacket*)(packet.packetData);
					R_Message(PRIORITY_MESSAGE, "Denied entry from server: %s\n", cPacket->why);
					currentNetState = Netstate_NoConnect;
				}
				break;
			case PACKET_PING:
				{
					R_Message(PRIORITY_MESSAGE, "Server PING\n");
					SendClientPacket(PACKET_PONG, nullptr, 0);
				}
				break;
			case PACKET_PONG:
				// TODO: use the difference from last PING packet to determine client latency
				R_Message(PRIORITY_MESSAGE, "Client PONG\n");
				break;
			case PACKET_DROP:
				{
					R_Message(PRIORITY_MESSAGE, "The server has forcibly terminated the connection.\n");
					DisconnectFromRemote();
				}
				break;
			default:
				if (!callbacks[NIC_INTERPRETSERVER] || !callbacks[NIC_INTERPRETSERVER](&packet)) {
					R_Message(PRIORITY_WARNING, "Unknown packet type %i\n", packet.packetHead.type);
				}
				break;
		}
	}

	// Send a server packet to a single client
	void SendServerPacketTo(packetType_e packetType, int clientNum, void* packetData, size_t packetDataSize) {
		uint64_t ticks = SDL_GetTicks();
		Packet packet = { { packetType, clientNum, Packet::PD_ServerClient, 0, packetDataSize }, packetData };
		if (clientNum == myClientNum) {
			DispatchSingleServerPacket(packet);
		}
		else {
			auto it = mOtherConnectedClients.find(clientNum);
			if (it == mOtherConnectedClients.end()) {
				R_Message(PRIORITY_WARNING, "Tried to send a packet (%i) to invalid client %i\n", packetType, clientNum);
				return;
			}
			else {
				it->second->SendPacket(packet);
				it->second->lastSpoken = ticks;
			}
		}
	}

	// Dispatches a single packet that's been sent to us from the client
	void DispatchSingleClientPacket(Packet& packet) {
		switch (packet.packetHead.type) {
			case PACKET_PING:
				R_Message(PRIORITY_MESSAGE, "Server PING from %i\n", packet.packetHead.clientNum);
				SendServerPacketTo(PACKET_PONG, packet.packetHead.clientNum, nullptr, 0);
				break;
			case PACKET_PONG:
				R_Message(PRIORITY_MESSAGE, "Server PONG\n");
				break;
			case PACKET_DROP:
				R_Message(PRIORITY_MESSAGE, "Client %i left.\n", packet.packetHead.clientNum);
				DropClient(packet.packetHead.clientNum);
				break;
			default:
				if (!callbacks[NIC_INTERPRETCLIENT] || !callbacks[NIC_INTERPRETCLIENT](&packet)) {
					R_Message(PRIORITY_WARNING, "Unknown packet type %i\n", packet.packetHead.type);
				}
				break;
		}
	}

	// Check for new incoming temporary connections
	void CheckTemporaryConnections() {
		// vTemporary contains a list of sockets that are not validated as clients. 
		// When they are validated as clients (or time out) the sockets are destroyed.
		// Here we are polling the local socket to see if there are any temporary connections awaiting acceptance
		uint64_t ticks = SDL_GetTicks();
		Packet genericPongPacket = { { PACKET_PONG, 0, Packet::PD_ServerClient, ticks, 0 }, nullptr };
		Packet outPacket;

		bool canReadLocalSocket, canWriteLocalSocket;
		Socket::SelectSingle(localSocket, canReadLocalSocket, canWriteLocalSocket);
		if (canReadLocalSocket) {
			Socket* newConnection = localSocket->CheckPendingConnections();
			newConnection->lastHeardFrom = ticks;
			vTemporaryConnections.push_back(newConnection);
		}
		
		for (auto it = vTemporaryConnections.begin(); it != vTemporaryConnections.end();) {
			bool bReadable, bWriteable;
			Socket* pSocket = *it;
			bool bNewClient = false;
			int msLastHeard;
			
			Socket::SelectSingle(pSocket, bReadable, bWriteable);
			while (bReadable && !bNewClient) {
				Packet thisPacket;
				if (!pSocket->ReadPacket(thisPacket)) {
					bWriteable = false;
					break;
				}

				pSocket->lastHeardFrom = ticks;

				switch (thisPacket.packetHead.type) {
					case PACKET_PING:
						pSocket->SendPacket(genericPongPacket);
						pSocket->lastSpoken = ticks;
						break;
					case PACKET_CLIENTATTEMPT:
						{
							if (callbacks[NIC_ACCEPTCLIENT] && callbacks[NIC_ACCEPTCLIENT](thisPacket.packetData)) {
								// Send an acceptance packet with the new client number. Also remove this socket from temporary read packets

								//
								// <<<CLIENT CONNECTED>>
								//
								outPacket.packetHead.clientNum = lastFreeClientNum++;
								outPacket.packetHead.direction = Packet::PD_ServerClient;
								outPacket.packetHead.sendTime = 0; // FIXME
								outPacket.packetHead.type = PACKET_CLIENTACCEPT;
								outPacket.packetHead.packetSize = 0; // FIXME

								pSocket->lastSpoken = ticks;

								mOtherConnectedClients[outPacket.packetHead.clientNum] = pSocket;
								numConnectedClients++;
								R_Message(PRIORITY_MESSAGE, "ClientAccept: %i\n", outPacket.packetHead.clientNum);
								bNewClient = true;
							}
							else {
								//
								// <<<CLIENT BLOCKED>>>
								//
								outPacket.packetHead.clientNum = -1;
								outPacket.packetHead.direction = Packet::PD_ServerClient;
								outPacket.packetHead.sendTime = 0; // FIXME
								outPacket.packetHead.type = PACKET_CLIENTDENIED;
								outPacket.packetHead.packetSize = 0; // FIXME
								R_Message(PRIORITY_MESSAGE, "ClientDenied --\n");
							}
							pSocket->SendPacket(outPacket);
						}
						break;
				}

				Socket::SelectSingle(pSocket, bReadable, bWriteable);
			}

			if (!bWriteable || bNewClient) {
				// If it's not writeable, then it died
				// If it's a new client then we should erase it too
				it = vTemporaryConnections.erase(it);
				if (!bNewClient) {
					delete pSocket;
				}
				continue;
			}

			// Check temporary connection for timeout
			msLastHeard = ticks - pSocket->lastHeardFrom;
			if (msLastHeard > net_timeout->Integer()) {
				R_Message(PRIORITY_MESSAGE, "Closing temporary connection due to timeout\n");
				it = vTemporaryConnections.erase(it);
				delete pSocket;
			}
			else {
				it++;
			}
		}
	}

	// Try and connect to a server
	bool ConnectToRemote(const char* hostname, int port) {
		DisconnectFromRemote();
		remoteSocket = new Socket(net_ipv6->Bool() ? AF_INET6 : AF_INET, SOCK_STREAM);

		bool connected = remoteSocket->Connect(hostname, port);
		if (!connected) {
			delete remoteSocket;
			remoteSocket = nullptr;
		}
		else {
			remoteSocket->lastHeardFrom = SDL_GetTicks();
		}
		currentNetState = Netstate_NeedAuth;
		return connected;
	}

	// Start a local server
	bool StartLocalServer() {
		return localSocket->StartListening(net_port->Integer(), net_serverbacklog->Integer());
	}

	// Join a remote server
	bool JoinServer(const char* hostname) {
		bool connected = ConnectToRemote(hostname, net_port->Integer());
		if (!connected) {
			return false;
		}
		SendClientPacket(PACKET_CLIENTATTEMPT, nullptr, 0);
		return true;
	}

	// Send packet from server -> client
	void SendServerPacket(packetType_e packetType, int clientNum, void* packetData, size_t packetDataSize) {
		if (clientNum != -1) {
			SendServerPacketTo(packetType, clientNum, packetData, packetDataSize);
		}
		else for (auto it = mOtherConnectedClients.begin(); it != mOtherConnectedClients.end(); ++it) {
			SendServerPacketTo(packetType, it->first, packetData, packetDataSize);
		}
	}

	// Send packet from client -> server
	void SendClientPacket(packetType_e packetType, void* packetData, size_t packetDataSize) {
		// FIXME: use the correct timestamp
		uint64_t ticks = SDL_GetTicks();

		Packet packet = { { packetType, myClientNum, Packet::PD_ClientServer, 0, packetDataSize }, packetData };
		if (remoteSocket == nullptr) {
			// Not connected to a remote server, send it to ourselves instead
			DispatchSingleClientPacket(packet);
		}
		else {
			remoteSocket->SendPacket(packet);
			remoteSocket->lastSpoken = ticks;
		}
	}

	// Determine if the local server is full.
	bool LocalServerFull() {
		return numConnectedClients >= net_maxclients->Integer();
	}

	void Connect(const char* hostname) {
		int port = net_port->Integer();
		R_Message(PRIORITY_MESSAGE, "Connecting to %s:%i\n", hostname, port);
		if (ConnectToRemote(hostname, port)) {
			R_Message(PRIORITY_MESSAGE, "Connection established.\n", hostname, port);
		}
		else {
			R_Message(PRIORITY_MESSAGE, "Could not connect to %s:%i\n", hostname, port);
			DisconnectFromRemote();
		}
	}

	// Disconnect from current remote server
	void DisconnectFromRemote() {
		if (currentNetState == Netstate_NoConnect) {
			// Not connected in the first place
			return;
		}
		if (remoteSocket != nullptr) {
			delete remoteSocket;
			remoteSocket = nullptr;
		}
		if (callbacks[NIC_EXIT]) {
			callbacks[NIC_EXIT](nullptr);
		}
		currentNetState = Netstate_NoConnect;
		R_Message(PRIORITY_NOTE, "--- Disconnected ---\n");
	}

	// Run all of the server stuff
	void ServerFrame() {
		uint64_t ticks = SDL_GetTicks();

		// Try listening for any temporary connections
		CheckTemporaryConnections();

		// Poll each single socket individually, so we can guarantee they're valid packets
		for (auto it = mOtherConnectedClients.begin(); it != mOtherConnectedClients.end();) {
			bool bReadable, bWriteable;
			Socket* pSocket = it->second;
			if (pSocket == nullptr) {
				it = mOtherConnectedClients.erase(it);
				continue;
			}
			int clientNum = it->first;
			Socket::SelectSingle(pSocket, bReadable, bWriteable);
			while (bReadable) {
				Packet packet;
				if (!pSocket->ReadPacket(packet)) {
					R_Message(PRIORITY_MESSAGE, "Client %i dropped.\n", clientNum);
					DropClient(clientNum);
					break;
				}
				else {
					int packetClNum = packet.packetHead.clientNum;
					if (packetClNum != clientNum) {
						R_Message(PRIORITY_WARNING, "Client %i tried to send packet with invalid clientNum %i -- packet dropped\n", clientNum, packetClNum);
					}
					else {
						DispatchSingleClientPacket(packet);
					}
				}
				Socket::SelectSingle(pSocket, bReadable, bWriteable);
			}
			++it;
		}

		// Deal with sockets that need writing (they're always writeable according to Didz)
		for (auto& packet : vPacketsAwaitingSend) {
			int8_t clientNum = packet.packetHead.clientNum;
			if (clientNum != -1) {
				auto found = mOtherConnectedClients.find(clientNum);
				if (found == mOtherConnectedClients.end()) {
					R_Message(PRIORITY_WARNING,
						"Tried to send packet %i with bad client %i\n",
						packet.packetHead.type,
						packet.packetHead.clientNum);
					continue;
				}
				found->second->SendPacket(packet);
				found->second->lastSpoken = ticks;
			}
			else {
				for (auto it = mOtherConnectedClients.begin(); it != mOtherConnectedClients.end(); ++it) {
					it->second->SendPacket(packet);
				}
				DispatchSingleServerPacket(packet);	// Don't forget to send to ourselves!
			}
		}
		vPacketsAwaitingSend.clear();

		if (callbacks[NIC_SERVERFRAME]) {
			callbacks[NIC_SERVERFRAME](nullptr);
		}

		// Iterate through all sockets, make sure they are still listening.
		for (auto it = mOtherConnectedClients.begin(); it != mOtherConnectedClients.end();) {
			Socket* socket = it->second;
			if (socket == nullptr) {
				it = mOtherConnectedClients.erase(it);
				continue;
			}
			int difference = ticks - socket->lastHeardFrom;
			if (difference > net_timeout->Integer()) {
				// Been longer than the timeout, drop it.
				R_Message(PRIORITY_MESSAGE, "Client %i timed out.\n", it->first);
				DropClient(it->first);
				it = mOtherConnectedClients.erase(it);
				continue;
			}
			else if (difference > net_timeout->Integer() / 2 &&
				ticks - socket->lastSpoken > net_timeout->Integer() / 2) {
				// Been longer than half the time, send a PING packet.
				R_Message(PRIORITY_MESSAGE, "Client %i hasn't sent anything in a while, pinging...\n", it->first);
				SendServerPacketTo(PACKET_PING, it->first, nullptr, 0);
				socket->lastSpoken = ticks;
			}
			it++;
		}
	}

	// Run all of the client stuff
	void ClientFrame() {
		uint64_t ticks = SDL_GetTicks();

		// Only do this stuff if we're on a remote server
		if (remoteSocket) {
			bool bRead, bWrite;
			Socket::SelectSingle(remoteSocket, bRead, bWrite);

			// Read packets from the server
			if (bRead) {
				do {
					Packet packet;
					if (!remoteSocket->ReadPacket(packet)) {
						// Close the connection?
						R_Message(PRIORITY_MESSAGE, "Connection lost.\n");
						DisconnectFromRemote();
						return;
					}
					else {
						DispatchSingleServerPacket(packet);
					}
					Socket::SelectSingle(remoteSocket, bRead, bWrite);
				} while (bRead);
			}

			// Write packets to the server
			if (bWrite) {
				for (auto& packet : vPacketsAwaitingSend) {
					remoteSocket->SendPacket(packet);
				}
				vPacketsAwaitingSend.clear();
			}

			// Check for timeout
			int msLastHeard = ticks - remoteSocket->lastHeardFrom;
			int msLastSpoken = ticks - remoteSocket->lastSpoken;
			if (msLastHeard > net_timeout->Integer()) {
				// Drop due to timeout
				R_Message(PRIORITY_MESSAGE, "No response from server in %i milliseconds, dropping...\n", msLastHeard);
				DisconnectFromRemote();
			}
			else if (msLastHeard > net_timeout->Integer() / 2 && msLastSpoken > net_timeout->Integer() / 2) {
				// Send a PING packet to make sure we're still alive
				R_Message(PRIORITY_MESSAGE, "No server response in %i milliseconds, pinging...\n", msLastHeard);
				SendClientPacket(PACKET_PING, nullptr, 0);
			}
		}

		// Run gamecode frame
		if (callbacks[NIC_CLIENTFRAME]) {
			callbacks[NIC_CLIENTFRAME](nullptr);
		}
	}

	void AddCallback(NetworkInterfaceCallbacks callback, networkCallbackFunction func) {
		if (callback == NIC_ALL) {
			R_Message(PRIORITY_WARNING, "Callback added to NIC_ALL (invalid)\n");
			return;
		}
		callbacks[callback] = func;
	}

	void RemoveCallback(NetworkInterfaceCallbacks callback) {
		if (callback == NIC_ALL) {
			memset(callbacks, 0, sizeof(networkCallbackFunction) * NIC_MAX);
		}
		else {
			callbacks[callback] = nullptr;
		}
	}
}

