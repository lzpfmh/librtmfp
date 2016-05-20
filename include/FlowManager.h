#pragma once

#include "Publisher.h"
#include "BandWriter.h"
#include "RTMFP.h"
#include "FlashConnection.h"
#include "Mona/Signal.h"
#include "Mona/DiffieHellman.h"
#include "Mona/UDPSocket.h"
#include "RTMFPSender.h"

// Callback typedef definitions
typedef void(*OnStatusEvent)(const char*, const char*);
typedef void(*OnMediaEvent)(const char *, const char*, unsigned int, const char*, unsigned int, int);
typedef void(*OnSocketError)(const char*);

class Invoker;
class RTMFPFlow;
/**************************************************
FlowManager is an abstract class used to manage 
lists of RTMFPFlow and RTMFPWriter
It is the base class of RTMFPConnection and P2PConnection
*/
class FlowManager : public BandWriter {
public:
	FlowManager(Invoker* invoker, OnSocketError pOnSocketError, OnStatusEvent pOnStatusEvent, OnMediaEvent pOnMediaEvent);

	~FlowManager();

	enum CommandType {
		NETSTREAM_PLAY = 1,
		NETSTREAM_PUBLISH,
		NETSTREAM_PUBLISH_P2P,
		NETSTREAM_GROUP,
		NETSTREAM_CLOSE
	};

	// Add a command to the main stream (play/publish)
	virtual void addCommand(CommandType command, const char* streamName, bool audioReliable=false, bool videoReliable=false)=0;

	// Return the peer ID (for p2p childs of RTMFPConnection)
	virtual Mona::UInt8* peerId() { return NULL; }

	// Compute keys and init encoder and decoder
	bool computeKeys(Mona::Exception& ex, const std::string& farPubKey, const std::string& initiatorNonce, const Mona::UInt8* responderNonce, Mona::UInt32 responderNonceSize, Mona::Buffer& sharedSecret, std::shared_ptr<RTMFPEngine>& pDecoder, std::shared_ptr<RTMFPEngine>& pEncoder, bool isResponder=true);

	// Latency = ping / 2
	Mona::UInt16							latency() { return (_ping >> 1); }

	virtual Mona::UDPSocket&				socket() = 0;

	/******* Internal functions for writers *******/
	virtual Mona::BinaryWriter&				writeMessage(Mona::UInt8 type, Mona::UInt16 length, RTMFPWriter* pWriter = NULL);

	virtual void							initWriter(const std::shared_ptr<RTMFPWriter>& pWriter);

	virtual const Mona::PoolBuffers&		poolBuffers();

	virtual bool							failed() const { return _died; }

	virtual bool							canWriteFollowing(RTMFPWriter& writer) { return _pLastWriter == &writer; }

	virtual void							flush() { flush(connected, connected? 0x89 : 0x0B); }

	virtual std::shared_ptr<RTMFPWriter>	changeWriter(RTMFPWriter& writer);

	// Return the size available in the current sender (or max size if there is no current sender)
	virtual Mona::UInt32					availableToWrite() { return RTMFP_MAX_PACKET_SIZE - (_pSender ? _pSender->packet.size() : RTMFP_HEADER_SIZE); }

protected:

	// Read data asynchronously
	// peerId : id of the peer if it is a p2p connection, otherwise parameter is ignored
	bool						readAsync(const std::string& peerId, Mona::UInt8* buf, Mona::UInt32 size, int& nbRead);

	// Analyze packets received from the server (must be connected)
	void						receive(Mona::Exception& ex, Mona::BinaryReader& reader);

	// Handle play request (only for P2PConnection)
	virtual bool				handlePlay(const std::string& streamName, FlashWriter& writer)=0;

	// Handle a NetGroup connection message from a peer connected (only for P2PConnection)
	virtual void				handleGroupHandshake(const std::string& groupId, const std::string& key, const std::string& id)=0;

	// Handle a P2P address exchange message (Only for RTMFPConnection)
	virtual void				handleP2PAddressExchange(Mona::Exception& ex, Mona::PacketReader& reader) = 0;

	// Handle message (after hanshake0)
	virtual void				handleMessage(Mona::Exception& ex, const Mona::PoolBuffer& pBuffer, const Mona::SocketAddress& address);

	// Close the conection properly
	virtual void				close();

	// On NetConnection success callback
	virtual bool				onConnect(Mona::Exception& ex) { return true; }

	// On NetStream.Publish.Start (only for NetConnection)
	virtual void				onPublished(FlashWriter& writer) {}

	RTMFPWriter*				writer(Mona::UInt64 id);
	RTMFPFlow*					createFlow(const std::string& signature) { return createFlow(_flows.size() + 2, signature); }
	RTMFPFlow*					createFlow(Mona::UInt64 id, const std::string& signature);

	// Create a flow for special signatures (NetGroup)
	virtual RTMFPFlow*			createSpecialFlow(Mona::UInt64 id, const std::string& signature) = 0;

	// Initialize the packet in the RTMFPSender
	Mona::UInt8*				packet();

	// Clear the packet and flush the connection
	void						flush(Mona::UInt8 marker, Mona::UInt32 size);

	// Flush the connection
	// marker values can be :
	// - 0B for handshake
	// - 09 for raw request
	// - 89 for AMF request
	virtual void				flush(bool echoTime, Mona::UInt8 marker);

	// Manage handshake messages (marker 0x0B)
	virtual void				manageHandshake(Mona::Exception& ex, Mona::BinaryReader& reader) = 0;

	enum HandshakeType {
		BASE_HANDSHAKE = 0x0A,
		P2P_HANDSHAKE = 0x0F
	};

	// Send the first handshake message (with rtmfp url/peerId + tag)
	// TODO: see if we can move this in RTMFPConnection
	void sendHandshake0(HandshakeType type, const std::string& epd, const std::string& tag);

	virtual RTMFPEngine*	getDecoder(Mona::UInt32 idStream, const Mona::SocketAddress& address) { return (idStream == 0) ? _pDefaultDecoder.get() : _pDecoder.get(); }

	void flushWriters();

	Mona::UInt8							_handshakeStep; // Handshake step (3 possible states)
	Mona::UInt16						_timeReceived; // last time received
	Mona::UInt32						_farId; // Session id

	Mona::SocketAddress					_outAddress; // current address used for sending
	Mona::SocketAddress					_hostAddress; // host address
	Mona::Time							_lastPing;
	Mona::UInt64						_nextRTMFPWriterId;
	Mona::Time							_lastKeepAlive; // last time a keepalive request has been received

	bool								_died; // True if is the connection is died

	// Encryption/Decryption
	std::shared_ptr<RTMFPEngine>		_pEncoder;
	std::shared_ptr<RTMFPEngine>		_pDecoder;
	std::shared_ptr<RTMFPEngine>		_pDefaultDecoder; // used for id stream 0

	Mona::DiffieHellman					_diffieHellman;
	Mona::Buffer						_sharedSecret; 
	std::string							_tag;
	Mona::Buffer						_pubKey;
	Mona::Buffer						_nonce;

	// External Callbacks to link with parent
	OnStatusEvent						_pOnStatusEvent;
	OnMediaEvent						_pOnMedia;
	OnSocketError						_pOnSocketError;

	// Events
	FlashConnection::OnStatus::Type						onStatus; // NetConnection or NetStream status event
	FlashConnection::OnMedia::Type						onMedia; // Received when we receive media (audio/video)
	FlashConnection::OnPlay::Type						onPlay; // Received when we receive media (audio/video)
	FlashConnection::OnGroupHandshake::Type				onGroupHandshake; // Received when a connected peer send us the Group hansdhake (only for P2PConnection)
	Mona::UDPSocket::OnError::Type						onError; // TODO: delete this if not needed
	Mona::UDPSocket::OnPacket::Type						onPacket; // Main input event, received on each raw packet

	// Job Members
	std::shared_ptr<FlashConnection>						_pMainStream; // Main Stream (NetConnection or P2P Connection Handler)
	std::map<Mona::UInt64, RTMFPFlow*>						_flows;
	std::map<Mona::UInt64, std::shared_ptr<RTMFPWriter> >	_flowWriters;
	std::map<Mona::UInt16, RTMFPFlow*>						_waitingFlows; // Map of id streams to new RTMFP flows (before knowing the flow id)
	RTMFPWriter*											_pLastWriter; // Write pointer used to check if it is possible to write
	Invoker*												_pInvoker;
	std::unique_ptr<RTMFPFlow>								_pFlowNull; // Null flow for some messages
	std::shared_ptr<RTMFPSender>							_pSender; // Current sender object
	Mona::PoolThread*										_pThread; // Thread used to send last message

private:

	// Update the ping value
	void setPing(Mona::UInt16 time, Mona::UInt16 timeEcho);

	Mona::UInt16											_ping;

	// Asynchronous read
	struct RTMFPMediaPacket {

		RTMFPMediaPacket(const Mona::PoolBuffers& poolBuffers, const Mona::UInt8* data, Mona::UInt32 size, Mona::UInt32 time, bool audio);

		Mona::PoolBuffer	pBuffer;
	};
	std::map<std::string, std::deque<std::shared_ptr<RTMFPMediaPacket>>>		_mediaPackets;
	std::recursive_mutex														_readMutex;
	bool																		_firstRead;
	static const char															_FlvHeader[];

	// Read
	bool																		_firstMedia;
	Mona::UInt32																_timeStart;
	bool																		_codecInfosRead; // Player : False until the video codec infos have been read
};