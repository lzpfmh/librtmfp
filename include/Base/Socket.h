/*
This file is a part of MonaSolutions Copyright 2017
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or
modify it under the terms of the the Mozilla Public License v2.0.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
Mozilla Public License v. 2.0 received along this program for more
details (or else see http://mozilla.org/MPL/2.0/).

*/

#pragma once

#include "Base/Mona.h"
#include "Base/SocketAddress.h"
#include "Base/ByteRate.h"
#include "Base/Packet.h"
#include "Base/Handler.h"
#include <deque>

namespace Base {

struct Socket : virtual Object, Net::Stats {
	typedef Event<void(shared<Buffer>& pBuffer, const SocketAddress& address)>	  OnReceived;
	typedef Event<void(const shared<Socket>& pSocket)>							  OnAccept;
	typedef Event<void(const Exception&)>										  OnError;
	typedef Event<void()>														  OnFlush;
	typedef Event<void()>														  OnDisconnection;

	/*!
	Decoder offers to decode data in the reception thread when socket is used with IOSocket
	decode returns the size of data decoded passing to onReceived, if pBuffer is entierly captured nothing is passing to onReceived
	/!\ pSocket must never be "attached" to the decoder in a instance variable otherwise a memory leak could happen (however a weak attachment stays acceptable) */
	struct Decoder { virtual UInt32 decode(shared<Buffer>& pBuffer, const SocketAddress& address, const shared<Socket>& pSocket) = 0; };

	enum Type {
		TYPE_STREAM = SOCK_STREAM,
		TYPE_DATAGRAM = SOCK_DGRAM
	};

	enum ShutdownType {
		SHUTDOWN_RECV = 0,
		SHUTDOWN_SEND = 1,
		SHUTDOWN_BOTH = 2
	};

	enum {
		BACKLOG_MAX = 200 // blacklog maximum, see http://tangentsoft.net/wskfaq/advanced.html#backlog
	};

	/*!
	Creates a Socket which supports IPv4 and IPv6 */
	Socket(Type type);

	virtual ~Socket();

	const	Type		type;

	virtual bool		isSecure() const { return false; }

	Time				recvTime() const { return _recvTime.load(); }
	UInt64				recvByteRate() const { return _recvByteRate; }
	Time				sendTime() const { return _sendTime.load(); }
	UInt64				sendByteRate() const { return _sendByteRate; }

	UInt32				recvBufferSize() const { return _recvBufferSize; }
	UInt32				sendBufferSize() const { return _sendBufferSize; }

	virtual UInt32		available() const;
	virtual UInt64		queueing() const { return _queueing; }

	operator NET_SOCKET() const { return _sockfd; }
	
	const SocketAddress& address() const;
	const SocketAddress& peerAddress() const { return _peerAddress; }

	bool setSendBufferSize(Exception& ex, int size);
	bool getSendBufferSize(Exception& ex, int& size) const { return getOption(ex,SOL_SOCKET, SO_SNDBUF, size); }
	
	bool setRecvBufferSize(Exception& ex, int size);
	bool getRecvBufferSize(Exception& ex, int& size) const { return getOption(ex, SOL_SOCKET, SO_RCVBUF, size); }

	bool setNoDelay(Exception& ex, bool value) { return setOption(ex,IPPROTO_TCP, TCP_NODELAY, value ? 1 : 0); }
	bool getNoDelay(Exception& ex, bool& value) const { return getOption(ex, IPPROTO_TCP, TCP_NODELAY, value); }

	bool setKeepAlive(Exception& ex, bool value) { return setOption(ex, SOL_SOCKET, SO_KEEPALIVE, value ? 1 : 0); }
	bool getKeepAlive(Exception& ex, bool& value) const { return getOption(ex,SOL_SOCKET, SO_KEEPALIVE, value); }

	bool setReuseAddress(Exception& ex, bool value) { return setOption(ex, SOL_SOCKET, SO_REUSEADDR, value ? 1 : 0); }
	bool getReuseAddress(Exception& ex, bool& value) const { return getOption(ex, SOL_SOCKET, SO_REUSEADDR, value); }

	bool setOOBInline(Exception& ex, bool value) { return setOption(ex, SOL_SOCKET, SO_OOBINLINE, value ? 1 : 0); }
	bool getOOBInline(Exception& ex, bool& value) const { return getOption(ex, SOL_SOCKET, SO_OOBINLINE, value); }

	bool setBroadcast(Exception& ex, bool value) { return setOption(ex, SOL_SOCKET, SO_BROADCAST, value ? 1 : 0); }
	bool getBroadcast(Exception& ex, bool& value) const { return getOption(ex, SOL_SOCKET, SO_BROADCAST, value); }

	bool setLinger(Exception& ex, bool on, int seconds);
	bool getLinger(Exception& ex, bool& on, int& seconds) const;
	
	void setReusePort(bool value);
	bool getReusePort() const;

	bool setNonBlockingMode(Exception& ex, bool value);
	bool getNonBlockingMode() const { return _nonBlockingMode; }

	bool		 accept(Exception& ex, shared<Socket>& pSocket);

	virtual bool connect(Exception& ex, const SocketAddress& address, UInt16 timeout=0);
	bool		 bind(Exception& ex, const SocketAddress& address);
	/*
	Bind on any available port */
	bool		 bind(Exception& ex, const IPAddress& ip=IPAddress::Wildcard()) { return bind(ex, SocketAddress(ip, 0)); }
	bool		 listen(Exception& ex, int backlog = SOMAXCONN);
	bool		 shutdown(ShutdownType type = SHUTDOWN_BOTH);
	
	int			 receive(Exception& ex, void* buffer, UInt32 size, int flags = 0) { return receive(ex, buffer, size, flags, NULL); }
	int			 receiveFrom(Exception& ex, void* buffer, UInt32 size, SocketAddress& address, int flags = 0)  { return receive(ex, buffer, size, flags, &address); }

	int			 send(Exception& ex, const void* data, UInt32 size, int flags = 0) { return sendTo(ex, data, size, SocketAddress::Wildcard(), flags); }
	virtual int	 sendTo(Exception& ex, const void* data, UInt32 size, const SocketAddress& address, int flags=0);

	/*!
	Sequential and safe writing, can queue data if can't send immediatly (flush required on onFlush event)
	Returns size of data sent immediatly (or -1 if error, for TCP socket a SHUTDOWN_SEND is done, so socket will be disconnected) */
	int			 write(Exception& ex, const Packet& packet, int flags = 0) { return write(ex, packet, SocketAddress::Wildcard(), flags); }
	int			 write(Exception& ex, const Packet& packet, const SocketAddress& address, int flags = 0);

	virtual bool flush(Exception& ex);

	template <typename ...Args>
	static void SetException(Exception& ex, int error, Args&&... args) {
		ex.set<Ex::Net::Socket>(Net::ErrorToMessage(error), std::forward<Args>(args)...).code = error;
	}

protected:

	// Create a socket from Socket::accept
	Socket(NET_SOCKET sockfd, const sockaddr& addr);
	virtual Socket* newSocket(Exception& ex, NET_SOCKET sockfd, const sockaddr& addr) { return new Socket(sockfd, (sockaddr&)addr); }
	virtual int		receive(Exception& ex, void* buffer, UInt32 size, int flags, SocketAddress* pAddress);


	void send(UInt32 count) { _sendTime = Time::Now(); _sendByteRate += count; }
	void receive(UInt32 count) { _recvTime = Time::Now(); _recvByteRate += count; }

private:
	void init();

	template<typename Type>
	bool getOption(Exception& ex, int level, int option, Type& value) const {
		if (_sockex) {
			ex = _sockex;
			return false;
		}
        NET_SOCKLEN length(sizeof(value));
		if (::getsockopt(_sockfd, level, option, reinterpret_cast<char*>(&value), &length) != -1)
			return true;
		SetException(ex, Net::LastError()," (level=",level,", option=",option,", length=",length,")");
		return false;
	}

	template<typename Type>
	bool setOption(Exception& ex, int level, int option, Type value) {
		if (_sockex) {
			ex = _sockex;
			return false;
		}
        NET_SOCKLEN length(sizeof(value));
		if (::setsockopt(_sockfd, level, option, reinterpret_cast<const char*>(&value), length) != -1)
			return true;
		SetException(ex, Net::LastError()," (level=",level,", option=",option,", length=",length,")");
		return false;
	}

	struct Sending : Packet, virtual Object {
		Sending(const Packet& packet, const SocketAddress& address, int flags) : Packet(std::move(packet)), address(address), flags(flags) {}

		const SocketAddress address;
		const int			flags;
	};

	Exception					_sockex;
	NET_SOCKET					_sockfd;

	volatile bool				_nonBlockingMode;

	mutable std::mutex			_mutexSending;
	std::deque<Sending>			_sendings;
	std::atomic<UInt64>			_queueing;

	SocketAddress				_peerAddress;
	mutable SocketAddress		_address;

	std::atomic<Int64>			_recvTime;
	ByteRate					_recvByteRate;
	std::atomic<Int64>			_sendTime;
	ByteRate					_sendByteRate;

	mutable std::atomic<int>	_recvBufferSize;
	mutable std::atomic<int>	_sendBufferSize;

//// Used by IOSocket /////////////////////
	shared<Decoder>				pDecoder;
	OnReceived					onReceived;
	OnAccept					onAccept;
	OnError						onError;
	OnFlush						onFlush;
	OnDisconnection				onDisconnection;

	UInt16						_threadReceive;
	std::atomic<UInt32>			_receiving;
	std::atomic<UInt8>			_reading;
	const Handler*				_pHandler;
	bool						_listening; // no need to protect this variable because listen() have to be called before IOSocket subscription!

#if !defined(_WIN32)
	weak<Socket>*				_pWeakThis;
	bool						_firstWritable;
#endif

	friend struct IOSocket;
};


} // namespace Base
