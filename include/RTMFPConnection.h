
#pragma once

#include "P2PConnection.h"
#include <list>

/**************************************************
RTMFPConnection represents a connection to the
RTMFP Server
*/
class NetGroup;
class RTMFPConnection : public FlowManager {
public:
	RTMFPConnection(Invoker* invoker, OnSocketError pOnSocketError, OnStatusEvent pOnStatusEvent, OnMediaEvent pOnMediaEvent);

	~RTMFPConnection();

	// Connect to the specified url, return true if the command succeed
	bool connect(Mona::Exception& ex, const char* url, const char* host);

	// Connect to a peer passing by the RTMFP server and start playing streamName
	void connect2Peer(const char* peerId, const char* streamName);

	// Connect to a peer directly and start playing streamName (Called by NetGroup)
	void connect2Peer(const char* peerId, const char* streamName, const std::string& rawId, const Mona::SocketAddress& address, const Mona::SocketAddress& hostAddress);

	// Connect to the NetGroup with netGroup ID (in the form G:...)
	void connect2Group(const char* netGroup, const char* streamName, bool publisher, double availabilityUpdatePeriod, Mona::UInt16 windowDuration);

	// Asynchronous read (buffered)
	// return false if end of buf has been reached
	bool read(const char* peerId, Mona::UInt8* buf, Mona::UInt32 size, int& nbRead);

	// Write media (netstream must be published)
	// return false if the client is not ready to publish, otherwise true
	bool write(const Mona::UInt8* buf, Mona::UInt32 size, int& pos);

	// Call a function of a server, peer or NetGroup
	// param peerId If set to 0 the call we be done to the server, if set to "all" to all the peers of a NetGroup, and to a peer otherwise
	// return 1 if the call succeed, 0 otherwise
	unsigned int callFunction(const char* function, int nbArgs, const char** args, const char* peerId = 0);

	// Called by Invoker every second to manage connection (flush and ping)
	void manage();

	virtual Mona::UDPSocket& socket() { return *_pSocket; }

	// Add a command to the main stream (play/publish)
	virtual void addCommand(CommandType command, const char* streamName, bool audioReliable = false, bool videoReliable = false);
		
	// Return listener if started successfully, otherwise NULL (only for RTMFP connection)
	template <typename ListenerType, typename... Args>
	ListenerType* startListening(Mona::Exception& ex, const std::string& streamName, const std::string& peerId, Args... args) {
		if (!_pPublisher || _pPublisher->name() != streamName) {
			ex.set(Mona::Exception::APPLICATION, "No publication found with name ", streamName);
			return NULL;
		}

		_pPublisher->start();
		return _pPublisher->addListener<ListenerType, Args...>(ex, peerId, args...);
	}

	// Push the media packet to write into a file
	void pushMedia(const std::string& stream, Mona::UInt32 time, const Mona::UInt8* data, Mona::UInt32 size, double lostRate, bool audio) { 
		Mona::PacketReader reader(data, size);
		onMedia("", stream, time, reader, lostRate, audio); 
	}

	// Remove the listener with peerId
	void stopListening(const std::string& peerId);

	// Set the p2p publisher as ready (used for blocking mode)
	void setP2pPublisherReady() { p2pPublishSignal.set(); p2pPublishReady = true; }

	// Called by P2PConnection when the responder receive the caller peerId to update the group if needed
	void addPeer2HeardList(const Mona::SocketAddress& peerAddress, const Mona::SocketAddress& hostAddress, const std::string& peerId, const char* rawId);

	// Called by P2PConnection when we are connected to the peer
	bool addPeer2Group(const Mona::SocketAddress& peerAddress, const std::string& peerId);

	// Return the peer ID in hex format
	const std::string peerId() { return _peerTxtId; }

	// Return the peer ID in bin format
	const Mona::UInt8* rawId() { return _rawId; }

	// Return the server address (for NetGroup)
	const Mona::SocketAddress& serverAddress() { return _targetAddress; }

	// Return the public key for Diffie Hellman encryption/decryption
	const Mona::Buffer& publicKey() { return _pubKey; }

	// Blocking members (used for ffmpeg to wait for an event before exiting the function)
	Mona::Signal							connectSignal; // signal to wait connection
	Mona::Signal							p2pPublishSignal; // signal to wait p2p publish
	Mona::Signal							publishSignal; // signal to wait publication
	bool									p2pPublishReady; // true if the p2p publisher is ready
	bool									publishReady; // true if the publisher is ready
	bool									connectReady; // Ready if we have received the NetStream.Connect.Success event

protected:

	// Send the connection message (after the answer of handshake1)
	virtual bool sendConnect(Mona::Exception& ex, Mona::BinaryReader& reader);
	
	// Handle stream creation
	void handleStreamCreated(Mona::UInt16 idStream);
	
	// Handle play request (only for P2PConnection)
	virtual bool handlePlay(const std::string& streamName, FlashWriter& writer);

	// Handle new peer in a Netgroup : connect to the peer
	void handleNewGroupPeer(const std::string& groupId, const std::string& peerId);

	// Handle a Writer close message (type 5E)
	virtual void handleWriterFailed(RTMFPWriter* pWriter);

	// Handle a P2P address exchange message
	void handleP2PAddressExchange(Mona::Exception& ex, Mona::PacketReader& reader);

	// Handle message (after hanshake0)
	virtual void handleMessage(Mona::Exception& ex, const Mona::PoolBuffer& pBuffer, const Mona::SocketAddress& address);

	// Manage handshake messages (marker 0x0B)
	virtual void manageHandshake(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Return the decoder engine for the following address (can be P2P or Normal connection)
	virtual RTMFPEngine*	getDecoder(Mona::UInt32 idStream, const Mona::SocketAddress& address);

	// On NetConnection success callback
	virtual bool onConnect(Mona::Exception& ex);

	// On NetStream.Publish.Start (only for NetConnection)
	virtual void onPublished(FlashWriter& writer);

	// Create a flow for special signatures (NetGroup)
	virtual RTMFPFlow*			createSpecialFlow(Mona::UInt64 id, const std::string& signature);

private:

	// Finish the initiated p2p connection (when handshake 70 is received)
	bool handleP2PHandshake(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Send the p2p requests to each available address
	// TODO: see if we need to implement it
	bool sendP2pRequests(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Read the redirection addresses and send new handshake 30 if not connected
	void handleRedirection(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Send the second handshake message
	void sendHandshake1(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Handle the first P2P responder handshake message (P2P connection initiated by peer)
	void responderHandshake0(Mona::Exception& ex, Mona::BinaryReader& reader);

	// If there is at least one request of command : create the stream
	void createWaitingStreams();

	// Send waiting Connections (P2P or normal)
	void sendConnections();

	// Send handshake for group connection
	void sendGroupConnection(const std::string& netGroup);

	// Create a P2PConnection
	std::shared_ptr<P2PConnection> createP2PConnection(const char* peerId, const char* streamOrTag, const Mona::SocketAddress& address, bool responder);

	Mona::Buffer													_pubKey; // Diffie Hellman public key for server and P2P handshakes

	bool															_waitConnect; // True if we are waiting for a normal connection request to be sent
	Mona::UInt8														_connectAttempt; // Counter of connection attempts to the server
	Mona::Time														_lastAttempt; // Last attempt to connect to the server
	std::vector<std::string>										_waitingPeers; // queue of tag from waiting p2p connection request (initiators)
	std::deque<std::string>											_waitingGroup; // queue of waiting connections to groups
	std::recursive_mutex											_mutexConnections; // mutex for waiting connections (normal or p2p)

	std::map<Mona::SocketAddress, std::shared_ptr<P2PConnection>>	_mapPeersByAddress; // P2P connections by Address
	std::map<std::string, std::shared_ptr<P2PConnection>>			_mapPeersByTag; // Initiator connections waiting an answer (70 or 71)

	std::string														_url; // RTMFP url of the application (base handshake)
	std::string														_rawUrl; // Header (420A) + Url to be sent in handshake 30
	Mona::UInt8														_rawId[PEER_ID_SIZE+2]; // my peer ID (computed with HMAC-SHA256) in binary format
	std::string														_peerTxtId; // my peer ID in hex format

	std::unique_ptr<Mona::UDPSocket>								_pSocket; // Sending socket established with server
	std::unique_ptr<Publisher>										_pPublisher; // Unique publisher used by connection & p2p
	FlashListener*													_pListener; // Listener of the main publication (only one by intance)

	std::shared_ptr<NetGroup>										_group;


	FlashConnection::OnStreamCreated::Type							onStreamCreated; // Received when stream has been created and is waiting for a command
	FlashConnection::OnNewPeer::Type								onNewPeer; // Received when a we receive the ID of a new peer in a NetGroup

	// Publish/Play commands
	struct StreamCommand {
		StreamCommand(CommandType t, const char* v, bool aReliable, bool vReliable) : type(t), value(v), audioReliable(aReliable), videoReliable(vReliable) {}

		CommandType		type;
		std::string		value;
		bool			audioReliable;
		bool			videoReliable;
	};
	std::list<StreamCommand>										_waitingCommands;
	std::recursive_mutex											_mutexCommands;
	Mona::UInt16													_nbCreateStreams; // Number of streams to create
};